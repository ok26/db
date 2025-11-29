#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "buffer_manager.h"
#include "util.h"

#define MAX_CACHED_PAGES 256

#define MINIMUM_FREE_SPACE 0x4 + SLOT_ENTRY_SIZE
typedef struct FreeDataPage {
    uint16_t free_space;
    uint32_t page_id;
} FreeDataPage;

uint8_t free_page_greater(void *a, void *b) {
    return ((FreeDataPage*)a)->free_space > ((FreeDataPage*)b)->free_space;
}

struct BufferManager {
    Map *cached_pages;
    Heap *cached_pages_page_ids;
    Heap *available_pages;
    Heap *nonfull_data_pages;
    uint32_t used_space;
    char *db_file_path;
};

// When writing back to db, use first in first out (queue). Easily achieved if 
// entries into bm->cached_pages_page_ids are structs of type {page, order}
// where the heap compares the second field (order)
void add_page_to_cache(BufferManager *bm, void *page, uint32_t page_id) {
    if (rbt_size(bm->cached_pages) >= MAX_CACHED_PAGES) {
        printf("SCHEISSE\n");
        // TODO: deletion
        return;
    }
    // printf("Adding page (%d) to cache\n", page_id);

    // Inserts a pointer to the page- as the value
    rbt_insert(bm->cached_pages, page_id, &page, sizeof(void*));
    heap_insert(bm->cached_pages_page_ids, (void*)&page_id);
}

BufferManager *buffer_manager_init(char *db_file_path) {
    BufferManager *bm = malloc(sizeof(BufferManager));
    bm->cached_pages = rbt_init();
    bm->cached_pages_page_ids = new_minheap();
    bm->available_pages = new_minheap();
    bm->nonfull_data_pages = new_heap(free_page_greater, sizeof(FreeDataPage));
    bm->used_space = 1;
    bm->db_file_path = db_file_path;
    return bm;
}

// Pages are not freed lol
void buffer_manager_free(BufferManager *bm) {
    // This will double free lots of data, add a flag to 
    // structs if they should free their own data or not
    rbt_free(bm->cached_pages);
    heap_free(bm->cached_pages_page_ids);
    heap_free(bm->available_pages);
    heap_free(bm->nonfull_data_pages);
    // free(bm->db_file_path); Passer should handle this
    free(bm);
}

Page *buffer_manager_new_bpt_page(BufferManager *bm, uint8_t is_leaf) {
    // printf("New bpt-page(%d)\n", bm->used_space);
    Page *page = malloc(sizeof(Page));
    PageHeader header;
    header.is_leaf = is_leaf;
    header.num_keys = 0;
    header.page_type = BPT_PAGE;

    if (!heap_is_empty(bm->available_pages)) {
        header.page_id = *(uint32_t*)heap_top(bm->available_pages);
        heap_pop(bm->available_pages);

        // Update the info of freed pages in root (might be done when writing back to db)
    }
    else {
        header.page_id = bm->used_space++;
    }

    if (is_leaf) {
        page->leaf = malloc(sizeof(LeafPage));
        page->leaf->next_page_id = 0;
    }
    else {
        page->internal = malloc(sizeof(InternalPage));
    }

    page->header = header;

    add_page_to_cache(bm, page, page->header.page_id);
    return page;
}

void *buffer_manager_get_page(BufferManager *bm, uint32_t page_id) {
    if (page_id == 0) {
        // Page-id 0 indicates NULL-page, page 0 is read with special function
        return NULL;
    }
    
    void **page_ptr = rbt_get(bm->cached_pages, page_id);
    // printf("DEBUG: %d, %p\n", page_id, page_ptr);
    if (page_ptr) {
        // printf("Found cached page (%d)\n", page_id);
        return *page_ptr;
    }

    void *page = read_page_from_db(bm->db_file_path, page_id);
    if (!page) {
        printf("Invalid page-id: %u\n", page_id);
        return NULL;
    }

    add_page_to_cache(bm, page, page_id);
    return page;
}

void buffer_manager_free_page(BufferManager *bm, uint32_t page_id) {

}


#define SLOT_ENTRY_SIZE 0x5

typedef struct SlotEntry {
    uint16_t offset;
    uint16_t length;
    uint8_t flags;
} SlotEntry;

SlotEntry get_slot_entryi(DataPage *page, uint32_t i) {

    if ((i + 1) * SLOT_ENTRY_SIZE > page->free_space_start) {
        SlotEntry invalid;
        invalid.flags = SLOT_FLAG_INVALID;
        return invalid;
    }

    SlotEntry s;
    s.offset = bytes_to_u16_be((page->data + i * SLOT_ENTRY_SIZE));
    s.length = bytes_to_u16_be((page->data + i * SLOT_ENTRY_SIZE + 0x2));
    s.flags = *(uint8_t*)(page->data + i * SLOT_ENTRY_SIZE + 0x4);
    return s;
}

void write_slot_entryi(uint8_t *slot_directory, SlotEntry slot_entry, uint32_t i) {
    u16_to_bytes_be((slot_directory + i * SLOT_ENTRY_SIZE), slot_entry.offset);
    u16_to_bytes_be((slot_directory + i * SLOT_ENTRY_SIZE + 0x2), slot_entry.length);
    *(uint8_t*)(slot_directory + i * SLOT_ENTRY_SIZE + 0x4) = slot_entry.flags; 
}

// Does not yet handle overflow pages
RID buffer_manager_request_slot(BufferManager *bm, size_t size, void *data) {
    if (!heap_is_empty(bm->nonfull_data_pages)) {
        FreeDataPage free_page = *(FreeDataPage*)heap_top(bm->nonfull_data_pages);
        // printf("Found free page(%d)\n", free_page.page_id);
        fflush(stdout);
        if (free_page.free_space >= size + SLOT_ENTRY_SIZE) {

            heap_pop(bm->nonfull_data_pages);
            DataPage *page = buffer_manager_get_page(bm, free_page.page_id);

            for (int i = 0;; i++) {
                uint8_t *slot_directory = page->data;
                SlotEntry s = get_slot_entryi(page, i);
                
                if (s.flags & SLOT_FLAG_INVALID) {
                    break;
                }

                else if (s.flags & SLOT_FLAG_FREE && s.length >= size) {
                    s.flags = SLOT_FLAG_NONE;
                    s.length = size;
                    write_slot_entryi(slot_directory, s, i);
                    memcpy((page->data + s.offset), data, size);

                    // Size of free-space kept consistent as a slot was reused
                    heap_insert(bm->nonfull_data_pages, &free_page);

                    RID rid;
                    rid.page_id = free_page.page_id;
                    rid.slot_id = i;
                    return rid;
                }
            }

            // Did not find a free slot, initialize new in free-space instead
            SlotEntry s;
            s.offset = page->free_space_end - size;
            s.length = size;
            s.flags = SLOT_FLAG_NONE;
            uint16_t slot_id = page->free_space_start / SLOT_ENTRY_SIZE;
            write_slot_entryi(page->data, s, slot_id);
            page->free_space_start += SLOT_ENTRY_SIZE;
            memcpy((page->data + s.offset), data, size);
            page->free_space_end -= size;
            page->num_slots += 1;

            // If page is still non-full
            uint32_t free_space = page->free_space_end - page->free_space_start;
            if (free_space >= MINIMUM_FREE_SPACE) {
                FreeDataPage free_page;
                free_page.page_id = page->page_id;
                free_page.free_space = free_space;
                heap_insert(bm->nonfull_data_pages, &free_page);
            }

            RID rid;
            rid.page_id = free_page.page_id;
            rid.slot_id = slot_id;
            return rid;
        }
    }

    // Else: Allocate new page
    // printf("New data page (%d)\n", bm->used_space);
    DataPage *page = malloc(sizeof(DataPage));
    if (!heap_is_empty(bm->available_pages)) {
        page->page_id = *(uint32_t*)heap_top(bm->available_pages);
        heap_pop(bm->available_pages);
    }
    else {
        page->page_id = bm->used_space++;
    }

    page->page_type = DATA_PAGE;
    page->num_slots = 1;
    page->free_space_start = 0x0;
    page->free_space_end = PAGE_SIZE - DATA_PAGE_HEADER_SIZE;
    // Allocate full free-space area for the data region (not sizeof of the integer expression)
    page->data = malloc(PAGE_SIZE - DATA_PAGE_HEADER_SIZE);
    SlotEntry s;
    s.offset = page->free_space_end - size;
    s.length = size;
    s.flags = SLOT_FLAG_NONE;
    write_slot_entryi(page->data, s, 0);
    page->free_space_start += SLOT_ENTRY_SIZE;
    memcpy((page->data + s.offset), data, size);
    page->free_space_end -= size;

    // If page is non-full
    uint32_t free_space = page->free_space_end - page->free_space_start;
    if (free_space >= MINIMUM_FREE_SPACE) {
        FreeDataPage free_page;
        free_page.page_id = page->page_id;
        free_page.free_space = free_space;
        heap_insert(bm->nonfull_data_pages, &free_page);
    }

    add_page_to_cache(bm, page, page->page_id);

    RID rid;
    rid.page_id = page->page_id;
    rid.slot_id = 0;
    return rid;
}

void buffer_manager_free_data(BufferManager *bm, RID rid) {

}

// Does not yet handle overflow pages
void *buffer_manager_get_data(BufferManager *bm, RID rid) {
    DataPage *page = buffer_manager_get_page(bm, rid.page_id);
    SlotEntry s = get_slot_entryi(page, rid.slot_id);
    void *data = (page->data + s.offset);
    return data;
}

// Dumb solution while deletion does not exist
void buffer_manager_flush_cache(BufferManager *bm) {
    while (!heap_is_empty(bm->cached_pages_page_ids)) {
        uint32_t page_id = *(uint32_t*)heap_top(bm->cached_pages_page_ids);
        heap_pop(bm->cached_pages_page_ids);
        void **page = rbt_get(bm->cached_pages, page_id);
        write_page_to_db(bm->db_file_path, *page);
    }

    // Dumb
    rbt_free(bm->cached_pages);
    bm->cached_pages = rbt_init();
}