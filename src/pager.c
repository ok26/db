#define _POSIX_C_SOURCE 200809L
// 64 bit file offsets on 32 bit systems
#define _FILE_OFFSET_BITS 64

#include "pager.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint16_t bytes_to_u16_be(const uint8_t b[2]) {
    return (uint16_t)(((uint16_t)b[0] << 8) |
                      ((uint16_t)b[1]));
}

uint32_t bytes_to_u32_be(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |
           ((uint32_t)b[3]);
}

void u16_to_bytes_be(uint8_t b[2], uint16_t v) {
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xFF);
}

void u32_to_bytes_be(uint8_t b[4], uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v & 0xFF);
}

void page_init(Page *page, uint32_t page_id, uint8_t page_type) {
    memset(page, 0, sizeof(Page));
    page->page_id = page_id;
    page->page_type = page_type;
    if (page_type == PAGE_TYPE_DATA) {
        page->data.free_space_end = DATA_PAGE_DATA_SIZE;
    }
}

uint16_t data_page_slot_count(const DataPage *page) {
    return page->free_space_start / SLOT_ENTRY_SIZE;
}

SlotEntry data_page_get_slot(const DataPage *page, uint16_t slot_id) {
    const uint8_t *entry = page->data + (size_t)slot_id * SLOT_ENTRY_SIZE;
    SlotEntry slot;
    slot.offset = bytes_to_u16_be(entry);
    slot.length = bytes_to_u16_be(entry + 0x2);
    slot.flags = entry[0x4];
    return slot;
}

void data_page_set_slot(DataPage *page, uint16_t slot_id, SlotEntry slot) {
    uint8_t *entry = page->data + (size_t)slot_id * SLOT_ENTRY_SIZE;
    u16_to_bytes_be(entry, slot.offset);
    u16_to_bytes_be(entry + 0x2, slot.length);
    entry[0x4] = slot.flags;
}


// ---- Encoding ----

static DBStatus bpt_page_encode(const BPTPage *page, uint8_t buffer[PAGE_SIZE]) {
    if (page->is_leaf > 1) return DB_ERR_INVALID;
    if (page->num_keys > (page->is_leaf ? MAX_ENTRIES_LEAF : MAX_KEYS)) {
        return DB_ERR_INVALID;
    }

    buffer[0x1] = page->is_leaf;
    u16_to_bytes_be(&buffer[0x2], page->num_keys);

    size_t offset = BPT_PAGE_HEADER_SIZE;
    if (page->is_leaf) {
        const LeafPage *leaf = &page->leaf;
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 4) {
            u32_to_bytes_be(&buffer[offset], leaf->keys[i]);
        }
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 6) {
            u32_to_bytes_be(&buffer[offset], leaf->rids[i].page_id);
            u16_to_bytes_be(&buffer[offset + 4], leaf->rids[i].slot_id);
        }
        u32_to_bytes_be(&buffer[offset], leaf->next_page_id);
    }
    else {
        const InternalPage *internal = &page->internal;
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 4) {
            u32_to_bytes_be(&buffer[offset], internal->keys[i]);
        }
        for (uint32_t i = 0; i <= page->num_keys; i++, offset += 4) {
            u32_to_bytes_be(&buffer[offset], internal->children[i]);
        }
    }
    return DB_OK;
}

static DBStatus data_page_encode(const DataPage *page, uint8_t buffer[PAGE_SIZE]) {
    buffer[0x1] = page->flags;
    u16_to_bytes_be(&buffer[0x2], page->occupied_slots);
    u16_to_bytes_be(&buffer[0x4], page->free_space_start);
    u16_to_bytes_be(&buffer[0x6], page->free_space_end);
    u16_to_bytes_be(&buffer[0x8], page->fragmented_bytes);
    u32_to_bytes_be(&buffer[0xA], page->prev_free_page);
    u32_to_bytes_be(&buffer[0xE], page->next_free_page);
    memcpy(&buffer[DATA_PAGE_HEADER_SIZE], page->data, DATA_PAGE_DATA_SIZE);
    return DB_OK;
}

static DBStatus overflow_page_encode(const OverflowPage *page,
        uint8_t buffer[PAGE_SIZE]) {
    if (page->length > OVERFLOW_PAGE_DATA_SIZE) return DB_ERR_INVALID;
    u32_to_bytes_be(&buffer[0x1], page->next_page_id);
    u16_to_bytes_be(&buffer[0x5], page->length);
    memcpy(&buffer[OVERFLOW_PAGE_HEADER_SIZE], page->data, page->length);
    return DB_OK;
}

DBStatus page_encode(const Page *page, uint8_t buffer[PAGE_SIZE]) {
    memset(buffer, 0, PAGE_SIZE);
    buffer[0x0] = page->page_type;

    switch (page->page_type) {
        case PAGE_TYPE_BPT:
            return bpt_page_encode(&page->bpt, buffer);

        case PAGE_TYPE_DATA:
            return data_page_encode(&page->data, buffer);

        case PAGE_TYPE_OVERFLOW:
            return overflow_page_encode(&page->overflow, buffer);

        case PAGE_TYPE_FREE:
            u32_to_bytes_be(&buffer[0x1], page->free.next_free_page);
            return DB_OK;

        default:
            return DB_ERR_INVALID;
    }
}


// ---- Decoding ----

static DBStatus bpt_page_decode(const uint8_t buffer[PAGE_SIZE], BPTPage *page) {
    page->is_leaf = buffer[0x1];
    page->num_keys = bytes_to_u16_be(&buffer[0x2]);
    if (page->is_leaf > 1) return DB_ERR_CORRUPT;
    if (page->num_keys > (page->is_leaf ? MAX_ENTRIES_LEAF : MAX_KEYS)) {
        return DB_ERR_CORRUPT;
    }

    size_t offset = BPT_PAGE_HEADER_SIZE;
    if (page->is_leaf) {
        LeafPage *leaf = &page->leaf;
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 4) {
            leaf->keys[i] = bytes_to_u32_be(&buffer[offset]);
        }
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 6) {
            leaf->rids[i].page_id = bytes_to_u32_be(&buffer[offset]);
            leaf->rids[i].slot_id = bytes_to_u16_be(&buffer[offset + 4]);
        }
        leaf->next_page_id = bytes_to_u32_be(&buffer[offset]);
    }
    else {
        InternalPage *internal = &page->internal;
        for (uint32_t i = 0; i < page->num_keys; i++, offset += 4) {
            internal->keys[i] = bytes_to_u32_be(&buffer[offset]);
        }
        for (uint32_t i = 0; i <= page->num_keys; i++, offset += 4) {
            internal->children[i] = bytes_to_u32_be(&buffer[offset]);
        }
    }
    return DB_OK;
}

static DBStatus data_page_decode(const uint8_t buffer[PAGE_SIZE], DataPage *page) {
    page->flags = buffer[0x1];
    page->occupied_slots = bytes_to_u16_be(&buffer[0x2]);
    page->free_space_start = bytes_to_u16_be(&buffer[0x4]);
    page->free_space_end = bytes_to_u16_be(&buffer[0x6]);
    page->fragmented_bytes = bytes_to_u16_be(&buffer[0x8]);
    page->prev_free_page = bytes_to_u32_be(&buffer[0xA]);
    page->next_free_page = bytes_to_u32_be(&buffer[0xE]);
    memcpy(page->data, &buffer[DATA_PAGE_HEADER_SIZE], DATA_PAGE_DATA_SIZE);

    if ((page->flags & ~DATA_PAGE_FLAG_IN_FREE_LIST) != 0 ||
            page->free_space_start % SLOT_ENTRY_SIZE != 0 ||
            page->free_space_start > page->free_space_end ||
            page->free_space_end > DATA_PAGE_DATA_SIZE) {
        return DB_ERR_CORRUPT;
    }

    // Every live record has to be inside the record area, and the record
    // area has to consist of exactly the live records and the holes
    uint32_t live_slots = 0;
    uint32_t live_bytes = 0;
    for (uint16_t i = 0; i < data_page_slot_count(page); i++) {
        SlotEntry slot = data_page_get_slot(page, i);
        if (slot.flags == SLOT_FLAG_FREE) continue;
        if ((slot.flags & ~SLOT_FLAG_OVERFLOW) != 0 ||
                slot.offset < page->free_space_end ||
                (uint32_t)slot.offset + slot.length > DATA_PAGE_DATA_SIZE ||
                ((slot.flags & SLOT_FLAG_OVERFLOW) && slot.length != OVERFLOW_STUB_SIZE)) {
            return DB_ERR_CORRUPT;
        }
        live_slots++;
        live_bytes += slot.length;
    }

    if (live_slots != page->occupied_slots ||
            live_bytes + page->fragmented_bytes !=
                (uint32_t)(DATA_PAGE_DATA_SIZE - page->free_space_end)) {
        return DB_ERR_CORRUPT;
    }
    return DB_OK;
}

static DBStatus overflow_page_decode(const uint8_t buffer[PAGE_SIZE],
        OverflowPage *page) {
    page->next_page_id = bytes_to_u32_be(&buffer[0x1]);
    page->length = bytes_to_u16_be(&buffer[0x5]);
    if (page->length > OVERFLOW_PAGE_DATA_SIZE) return DB_ERR_CORRUPT;
    memcpy(page->data, &buffer[OVERFLOW_PAGE_HEADER_SIZE], page->length);
    return DB_OK;
}

DBStatus page_decode(const uint8_t buffer[PAGE_SIZE], uint32_t page_id, Page *page) {
    page_init(page, page_id, buffer[0x0]);

    DBStatus rc;
    switch (page->page_type) {
        case PAGE_TYPE_BPT:
            rc = bpt_page_decode(buffer, &page->bpt);
            break;

        case PAGE_TYPE_DATA:
            rc = data_page_decode(buffer, &page->data);
            break;

        case PAGE_TYPE_OVERFLOW:
            rc = overflow_page_decode(buffer, &page->overflow);
            break;

        case PAGE_TYPE_FREE:
            page->free.next_free_page = bytes_to_u32_be(&buffer[0x1]);
            rc = DB_OK;
            break;

        default:
            rc = DB_ERR_CORRUPT;
            break;
    }

    if (rc != DB_OK) {
        fprintf(stderr, "pager: page %u (type 0x%x) is corrupt\n",
            page_id, page->page_type);
    }
    return rc;
}

void header_encode(const DBHeader *header, uint8_t buffer[PAGE_SIZE]) {
    memset(buffer, 0, PAGE_SIZE);
    memcpy(&buffer[0x00], DB_MAGIC, DB_MAGIC_SIZE);
    u32_to_bytes_be(&buffer[0x08], PAGE_SIZE);
    u32_to_bytes_be(&buffer[0x0C], header->page_count);
    u32_to_bytes_be(&buffer[0x10], header->freelist_head);
    u32_to_bytes_be(&buffer[0x14], header->freelist_count);
    for (uint32_t i = 0; i < DB_META_COUNT; i++) {
        u32_to_bytes_be(&buffer[0x18 + 4 * i], header->meta[i]);
    }
}

DBStatus header_decode(const uint8_t buffer[PAGE_SIZE], DBHeader *header) {
    if (memcmp(&buffer[0x00], DB_MAGIC, DB_MAGIC_SIZE) != 0) {
        fprintf(stderr, "pager: not a database file (bad magic)\n");
        return DB_ERR_CORRUPT;
    }
    if (bytes_to_u32_be(&buffer[0x08]) != PAGE_SIZE) {
        fprintf(stderr, "pager: database uses a different page size\n");
        return DB_ERR_CORRUPT;
    }

    header->page_count = bytes_to_u32_be(&buffer[0x0C]);
    header->freelist_head = bytes_to_u32_be(&buffer[0x10]);
    header->freelist_count = bytes_to_u32_be(&buffer[0x14]);
    for (uint32_t i = 0; i < DB_META_COUNT; i++) {
        header->meta[i] = bytes_to_u32_be(&buffer[0x18 + 4 * i]);
    }

    if (header->page_count == 0 ||
            header->freelist_head >= header->page_count ||
            header->freelist_count >= header->page_count ||
            (header->freelist_head == NULL_PAGE_ID) != (header->freelist_count == 0)) {
        fprintf(stderr, "pager: database header is corrupt\n");
        return DB_ERR_CORRUPT;
    }
    return DB_OK;
}


// ---- File I/O ----

struct Pager {
    int fd;
};

DBStatus pager_open(const char *db_file_path, Pager **pager) {
    *pager = NULL;
    Pager *p = malloc(sizeof(Pager));
    if (!p) return DB_ERR_NOMEM;

    p->fd = open(db_file_path, O_RDWR | O_CREAT, 0644);
    if (p->fd < 0) {
        fprintf(stderr, "pager: open %s: %s\n", db_file_path, strerror(errno));
        free(p);
        return DB_ERR_IO;
    }

    *pager = p;
    return DB_OK;
}

void pager_close(Pager *pager) {
    if (!pager) return;
    close(pager->fd);
    free(pager);
}

DBStatus pager_file_size(Pager *pager, uint64_t *size) {
    struct stat st;
    if (fstat(pager->fd, &st) != 0) {
        fprintf(stderr, "pager: fstat: %s\n", strerror(errno));
        return DB_ERR_IO;
    }
    *size = (uint64_t)st.st_size;
    return DB_OK;
}

static DBStatus read_block(Pager *pager, uint32_t page_id, uint8_t buffer[PAGE_SIZE]) {
    off_t offset = (off_t)page_id * PAGE_SIZE;
    size_t done = 0;
    while (done < PAGE_SIZE) {
        ssize_t r = pread(pager->fd, buffer + done, PAGE_SIZE - done,
            offset + (off_t)done);
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "pager: read page %u: %s\n", page_id, strerror(errno));
            return DB_ERR_IO;
        }
        if (r == 0) {
            // The page is past the end of the file
            fprintf(stderr, "pager: page %u is missing from the file\n", page_id);
            return DB_ERR_CORRUPT;
        }
        done += (size_t)r;
    }
    return DB_OK;
}

static DBStatus write_block(Pager *pager, uint32_t page_id,
        const uint8_t buffer[PAGE_SIZE]) {
    off_t offset = (off_t)page_id * PAGE_SIZE;
    size_t done = 0;
    while (done < PAGE_SIZE) {
        ssize_t w = pwrite(pager->fd, buffer + done, PAGE_SIZE - done,
            offset + (off_t)done);
        if (w < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "pager: write page %u: %s\n", page_id, strerror(errno));
            return DB_ERR_IO;
        }
        done += (size_t)w;
    }
    return DB_OK;
}

DBStatus pager_read_header(Pager *pager, DBHeader *header) {
    uint8_t buffer[PAGE_SIZE];
    DBStatus rc = read_block(pager, 0, buffer);
    if (rc != DB_OK) return rc;
    return header_decode(buffer, header);
}

DBStatus pager_write_header(Pager *pager, const DBHeader *header) {
    uint8_t buffer[PAGE_SIZE];
    header_encode(header, buffer);
    return write_block(pager, 0, buffer);
}

DBStatus pager_read_page(Pager *pager, uint32_t page_id, Page *page) {
    if (page_id == NULL_PAGE_ID) return DB_ERR_INVALID;

    uint8_t buffer[PAGE_SIZE];
    DBStatus rc = read_block(pager, page_id, buffer);
    if (rc != DB_OK) return rc;
    return page_decode(buffer, page_id, page);
}

DBStatus pager_write_page(Pager *pager, const Page *page) {
    if (page->page_id == NULL_PAGE_ID) return DB_ERR_INVALID;

    uint8_t buffer[PAGE_SIZE];
    DBStatus rc = page_encode(page, buffer);
    if (rc != DB_OK) {
        fprintf(stderr, "pager: refusing to write invalid page %u\n", page->page_id);
        return rc;
    }
    return write_block(pager, page->page_id, buffer);
}

DBStatus pager_sync(Pager *pager) {
    if (fsync(pager->fd) != 0) {
        fprintf(stderr, "pager: fsync: %s\n", strerror(errno));
        return DB_ERR_IO;
    }
    return DB_OK;
}
