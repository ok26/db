#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "record_manager.h"

// ---- Single data page ----

// Free bytes in the page, including holes that need a compaction to be used
static uint32_t free_bytes(const DataPage *page) {
    return (uint32_t)(page->free_space_end - page->free_space_start) +
        page->fragmented_bytes;
}

// Bytes needed to store a record of the given size in the page
static uint32_t space_needed(const DataPage *page, uint32_t size) {
    // A free slot entry is reused if there is one
    uint8_t has_free_slot = page->occupied_slots < data_page_slot_count(page);
    return size + (has_free_slot ? 0 : SLOT_ENTRY_SIZE);
}

// Moves all records to the end of the page so that the holes left by
// deleted records become one contiguous free area. Slot ids do not change.
static void compact_page(DataPage *page) {
    uint8_t records[DATA_PAGE_DATA_SIZE];
    uint32_t end = DATA_PAGE_DATA_SIZE;

    for (uint16_t i = 0; i < data_page_slot_count(page); i++) {
        SlotEntry slot = data_page_get_slot(page, i);
        if (slot.flags & SLOT_FLAG_FREE) continue;

        end -= slot.length;
        memcpy(records + end, page->data + slot.offset, slot.length);
        slot.offset = (uint16_t)end;
        data_page_set_slot(page, i, slot);
    }

    memcpy(page->data + end, records + end, DATA_PAGE_DATA_SIZE - end);
    page->free_space_end = (uint16_t)end;
    page->fragmented_bytes = 0;
}

// Stores a record in a page that has at least space_needed() free bytes
static uint16_t page_insert(DataPage *page, const void *data, uint16_t size,
        uint8_t flags) {
    uint16_t slot_count = data_page_slot_count(page);
    uint16_t slot_id = slot_count;
    if (page->occupied_slots < slot_count) {
        for (uint16_t i = 0; i < slot_count; i++) {
            if (data_page_get_slot(page, i).flags & SLOT_FLAG_FREE) {
                slot_id = i;
                break;
            }
        }
    }

    uint32_t directory_end = page->free_space_start +
        (slot_id == slot_count ? SLOT_ENTRY_SIZE : 0);
    if (directory_end + size > page->free_space_end) {
        compact_page(page);
    }

    page->free_space_end -= size;
    if (size > 0) memcpy(page->data + page->free_space_end, data, size);
    if (slot_id == slot_count) page->free_space_start += SLOT_ENTRY_SIZE;

    SlotEntry slot = { page->free_space_end, size, flags };
    data_page_set_slot(page, slot_id, slot);
    page->occupied_slots++;
    return slot_id;
}

static void page_delete(DataPage *page, uint16_t slot_id) {
    SlotEntry slot = data_page_get_slot(page, slot_id);
    page->fragmented_bytes += slot.length;

    SlotEntry free_slot = { 0, 0, SLOT_FLAG_FREE };
    data_page_set_slot(page, slot_id, free_slot);
    page->occupied_slots--;

    // Free slots at the end of the directory can be given back
    while (page->free_space_start > 0) {
        SlotEntry last = data_page_get_slot(page, data_page_slot_count(page) - 1);
        if (!(last.flags & SLOT_FLAG_FREE)) break;
        page->free_space_start -= SLOT_ENTRY_SIZE;
    }
}


// ---- Free-space list ----

static DBStatus pin_list_page(BufferManager *bm, uint32_t page_id, Page **page) {
    DBStatus rc = buffer_manager_get_page(bm, page_id, page);
    if (rc == DB_ERR_INVALID) rc = DB_ERR_CORRUPT;
    if (rc != DB_OK) return rc;

    if ((*page)->page_type != PAGE_TYPE_DATA ||
            !((*page)->data.flags & DATA_PAGE_FLAG_IN_FREE_LIST)) {
        fprintf(stderr, "record_manager: free-space list is corrupt at page %u\n",
            page_id);
        buffer_manager_unpin(bm, *page);
        *page = NULL;
        return DB_ERR_CORRUPT;
    }
    return DB_OK;
}

static DBStatus free_list_remove(BufferManager *bm, Page *page) {
    DataPage *data_page = &page->data;
    Page *other;

    if (data_page->prev_free_page != NULL_PAGE_ID) {
        DBStatus rc = pin_list_page(bm, data_page->prev_free_page, &other);
        if (rc != DB_OK) return rc;
        other->data.next_free_page = data_page->next_free_page;
        buffer_manager_mark_dirty(bm, other);
        buffer_manager_unpin(bm, other);
    }
    else {
        buffer_manager_set_meta(bm, DB_META_FREE_SPACE_LIST,
            data_page->next_free_page);
    }

    if (data_page->next_free_page != NULL_PAGE_ID) {
        DBStatus rc = pin_list_page(bm, data_page->next_free_page, &other);
        if (rc != DB_OK) return rc;
        other->data.prev_free_page = data_page->prev_free_page;
        buffer_manager_mark_dirty(bm, other);
        buffer_manager_unpin(bm, other);
    }

    data_page->prev_free_page = NULL_PAGE_ID;
    data_page->next_free_page = NULL_PAGE_ID;
    data_page->flags &= (uint8_t)~DATA_PAGE_FLAG_IN_FREE_LIST;
    buffer_manager_mark_dirty(bm, page);
    return DB_OK;
}

static DBStatus free_list_push(BufferManager *bm, Page *page) {
    DataPage *data_page = &page->data;
    uint32_t head = buffer_manager_get_meta(bm, DB_META_FREE_SPACE_LIST);

    if (head != NULL_PAGE_ID) {
        Page *head_page;
        DBStatus rc = pin_list_page(bm, head, &head_page);
        if (rc != DB_OK) return rc;
        head_page->data.prev_free_page = page->page_id;
        buffer_manager_mark_dirty(bm, head_page);
        buffer_manager_unpin(bm, head_page);
    }

    data_page->prev_free_page = NULL_PAGE_ID;
    data_page->next_free_page = head;
    data_page->flags |= DATA_PAGE_FLAG_IN_FREE_LIST;
    buffer_manager_set_meta(bm, DB_META_FREE_SPACE_LIST, page->page_id);
    buffer_manager_mark_dirty(bm, page);
    return DB_OK;
}

// Adds the page to, or removes it from, the free-space list depending on
// how much free space it has
static DBStatus free_list_update(BufferManager *bm, Page *page) {
    uint8_t listed = page->data.flags & DATA_PAGE_FLAG_IN_FREE_LIST;
    uint8_t should_be_listed = free_bytes(&page->data) >= FREE_SPACE_THRESHOLD;

    if (should_be_listed && !listed) return free_list_push(bm, page);
    if (!should_be_listed && listed) return free_list_remove(bm, page);
    return DB_OK;
}


// ---- Overflow pages ----

static DBStatus pin_overflow_page(BufferManager *bm, uint32_t page_id, Page **page) {
    DBStatus rc = buffer_manager_get_page(bm, page_id, page);
    if (rc == DB_ERR_INVALID) rc = DB_ERR_CORRUPT;
    if (rc != DB_OK) return rc;

    if ((*page)->page_type != PAGE_TYPE_OVERFLOW) {
        fprintf(stderr, "record_manager: page %u is not an overflow page\n", page_id);
        buffer_manager_unpin(bm, *page);
        *page = NULL;
        return DB_ERR_CORRUPT;
    }
    return DB_OK;
}

static DBStatus overflow_free(BufferManager *bm, uint32_t page_id) {
    while (page_id != NULL_PAGE_ID) {
        Page *page;
        DBStatus rc = pin_overflow_page(bm, page_id, &page);
        if (rc != DB_OK) return rc;

        page_id = page->overflow.next_page_id;
        rc = buffer_manager_free_page(bm, page);
        if (rc != DB_OK) {
            buffer_manager_unpin(bm, page);
            return rc;
        }
    }
    return DB_OK;
}

static DBStatus overflow_write(BufferManager *bm, const uint8_t *data,
        uint32_t size, uint32_t *first_page) {
    uint32_t chunks = (size + OVERFLOW_PAGE_DATA_SIZE - 1) / OVERFLOW_PAGE_DATA_SIZE;
    uint32_t next = NULL_PAGE_ID;

    // The chain is written back to front so that the next page is
    // always known when a page is filled
    for (uint32_t chunk = chunks; chunk-- > 0;) {
        uint32_t offset = chunk * OVERFLOW_PAGE_DATA_SIZE;
        uint32_t length = size - offset;
        if (length > OVERFLOW_PAGE_DATA_SIZE) length = OVERFLOW_PAGE_DATA_SIZE;

        Page *page;
        DBStatus rc = buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &page);
        if (rc != DB_OK) {
            overflow_free(bm, next);
            return rc;
        }

        page->overflow.next_page_id = next;
        page->overflow.length = (uint16_t)length;
        memcpy(page->overflow.data, data + offset, length);
        next = page->page_id;
        // New pages are already dirty
        buffer_manager_unpin(bm, page);
    }

    *first_page = next;
    return DB_OK;
}

static DBStatus overflow_read(BufferManager *bm, uint32_t page_id, uint32_t size,
        void **data) {
    uint8_t *buffer = malloc(size ? size : 1);
    if (!buffer) return DB_ERR_NOMEM;

    uint32_t done = 0;
    while (page_id != NULL_PAGE_ID) {
        Page *page;
        DBStatus rc = pin_overflow_page(bm, page_id, &page);
        if (rc != DB_OK) {
            free(buffer);
            return rc;
        }

        uint16_t length = page->overflow.length;
        if (length == 0 || length > size - done) {
            fprintf(stderr, "record_manager: overflow chain is corrupt at page %u\n",
                page_id);
            buffer_manager_unpin(bm, page);
            free(buffer);
            return DB_ERR_CORRUPT;
        }

        memcpy(buffer + done, page->overflow.data, length);
        done += length;
        page_id = page->overflow.next_page_id;
        buffer_manager_unpin(bm, page);
    }

    if (done != size) {
        fprintf(stderr, "record_manager: overflow chain is too short\n");
        free(buffer);
        return DB_ERR_CORRUPT;
    }

    *data = buffer;
    return DB_OK;
}


// ---- Records ----

// Pins the data page of a record and returns its slot entry
static DBStatus pin_record(BufferManager *bm, RID rid, Page **page, SlotEntry *slot) {
    DBStatus rc = buffer_manager_get_page(bm, rid.page_id, page);
    if (rc == DB_ERR_INVALID) return DB_NOT_FOUND;
    if (rc != DB_OK) return rc;

    DataPage *data_page = &(*page)->data;
    if ((*page)->page_type != PAGE_TYPE_DATA ||
            rid.slot_id >= data_page_slot_count(data_page)) {
        buffer_manager_unpin(bm, *page);
        *page = NULL;
        return DB_NOT_FOUND;
    }

    *slot = data_page_get_slot(data_page, rid.slot_id);
    if (slot->flags & SLOT_FLAG_FREE) {
        buffer_manager_unpin(bm, *page);
        *page = NULL;
        return DB_NOT_FOUND;
    }
    return DB_OK;
}

static DBStatus insert_inline(BufferManager *bm, const void *data, uint16_t size,
        uint8_t flags, RID *rid) {
    // First fit among the first pages of the free-space list
    Page *page = NULL;
    uint32_t page_id = buffer_manager_get_meta(bm, DB_META_FREE_SPACE_LIST);
    for (uint32_t steps = 0; page_id != NULL_PAGE_ID &&
            steps < FREE_SPACE_SEARCH_LIMIT; steps++) {
        DBStatus rc = pin_list_page(bm, page_id, &page);
        if (rc != DB_OK) return rc;
        if (free_bytes(&page->data) >= space_needed(&page->data, size)) break;

        page_id = page->data.next_free_page;
        buffer_manager_unpin(bm, page);
        page = NULL;
    }

    if (!page) {
        DBStatus rc = buffer_manager_new_page(bm, PAGE_TYPE_DATA, &page);
        if (rc != DB_OK) return rc;
    }

    rid->page_id = page->page_id;
    rid->slot_id = page_insert(&page->data, data, size, flags);
    buffer_manager_mark_dirty(bm, page);

    DBStatus rc = free_list_update(bm, page);
    buffer_manager_unpin(bm, page);
    return rc;
}

DBStatus record_insert(BufferManager *bm, const void *data, uint32_t size, RID *rid) {
    if (!data && size > 0) return DB_ERR_INVALID;

    if (size <= MAX_INLINE_RECORD_SIZE) {
        return insert_inline(bm, data, (uint16_t)size, SLOT_FLAG_NONE, rid);
    }

    uint32_t first_page;
    DBStatus rc = overflow_write(bm, data, size, &first_page);
    if (rc != DB_OK) return rc;

    uint8_t stub[OVERFLOW_STUB_SIZE];
    u32_to_bytes_be(stub, first_page);
    u32_to_bytes_be(stub + 4, size);
    rc = insert_inline(bm, stub, OVERFLOW_STUB_SIZE, SLOT_FLAG_OVERFLOW, rid);
    if (rc != DB_OK) {
        overflow_free(bm, first_page);
    }
    return rc;
}

DBStatus record_read(BufferManager *bm, RID rid, void **data, uint32_t *size) {
    *data = NULL;
    *size = 0;

    Page *page;
    SlotEntry slot;
    DBStatus rc = pin_record(bm, rid, &page, &slot);
    if (rc != DB_OK) return rc;

    const uint8_t *record = page->data.data + slot.offset;
    if (slot.flags & SLOT_FLAG_OVERFLOW) {
        uint32_t first_page = bytes_to_u32_be(record);
        uint32_t total_size = bytes_to_u32_be(record + 4);
        buffer_manager_unpin(bm, page);

        rc = overflow_read(bm, first_page, total_size, data);
        if (rc == DB_OK) *size = total_size;
        return rc;
    }

    uint8_t *buffer = malloc(slot.length ? slot.length : 1);
    if (!buffer) {
        buffer_manager_unpin(bm, page);
        return DB_ERR_NOMEM;
    }
    memcpy(buffer, record, slot.length);
    buffer_manager_unpin(bm, page);

    *data = buffer;
    *size = slot.length;
    return DB_OK;
}

DBStatus record_delete(BufferManager *bm, RID rid) {
    Page *page;
    SlotEntry slot;
    DBStatus rc = pin_record(bm, rid, &page, &slot);
    if (rc != DB_OK) return rc;

    uint32_t overflow_page = NULL_PAGE_ID;
    if (slot.flags & SLOT_FLAG_OVERFLOW) {
        overflow_page = bytes_to_u32_be(page->data.data + slot.offset);
    }

    page_delete(&page->data, rid.slot_id);
    buffer_manager_mark_dirty(bm, page);

    if (page->data.occupied_slots == 0) {
        // The page is empty, give it back
        if (page->data.flags & DATA_PAGE_FLAG_IN_FREE_LIST) {
            rc = free_list_remove(bm, page);
        }
        if (rc == DB_OK) rc = buffer_manager_free_page(bm, page);
        if (rc != DB_OK) buffer_manager_unpin(bm, page);
    }
    else {
        rc = free_list_update(bm, page);
        buffer_manager_unpin(bm, page);
    }

    if (rc == DB_OK && overflow_page != NULL_PAGE_ID) {
        rc = overflow_free(bm, overflow_page);
    }
    return rc;
}

DBStatus record_update(BufferManager *bm, RID rid, const void *data,
        uint32_t size, RID *new_rid) {
    if (!data && size > 0) return DB_ERR_INVALID;

    Page *page;
    SlotEntry slot;
    DBStatus rc = pin_record(bm, rid, &page, &slot);
    if (rc != DB_OK) return rc;

    if (!(slot.flags & SLOT_FLAG_OVERFLOW) && size <= slot.length) {
        // Fits in the old space, the leftover becomes a hole
        DataPage *data_page = &page->data;
        if (size > 0) memcpy(data_page->data + slot.offset, data, size);
        data_page->fragmented_bytes += (uint16_t)(slot.length - size);
        slot.length = (uint16_t)size;
        data_page_set_slot(data_page, rid.slot_id, slot);
        buffer_manager_mark_dirty(bm, page);

        rc = free_list_update(bm, page);
        buffer_manager_unpin(bm, page);
        *new_rid = rid;
        return rc;
    }
    buffer_manager_unpin(bm, page);

    // Insert the new version before deleting the old one so that the old
    // record is kept if the insert fails
    RID moved;
    rc = record_insert(bm, data, size, &moved);
    if (rc != DB_OK) return rc;

    rc = record_delete(bm, rid);
    if (rc != DB_OK) {
        record_delete(bm, moved);
        return rc;
    }

    *new_rid = moved;
    return DB_OK;
}
