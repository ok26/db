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

typedef struct CachedPage {
    uint32_t page_id;
    uint32_t order;
} CachedPage;

uint8_t free_page_greater(void *a, void *b) {
    return ((FreeDataPage*)a)->free_space > ((FreeDataPage*)b)->free_space;
}

uint8_t cached_page_lesser(void *a, void *b) {
    return ((CachedPage*)a)->page_id < ((CachedPage*)b)->page_id;
}

struct BufferManager {

    // Map of form (key; value): (uint32_t(page_id); **Page(page))
    Map *cached_pages;

    // Heap with elements of type CachedPages sorted on CachedPages.order (lesser)
    Heap *cached_pages_queue;

    // Heap with elements of type uint32_t (lesser)
    Heap *available_pages;

    // Heap with elements of type FreeDataPage sorted on FreeDataPage.free_space (greater)
    Heap *nonfull_data_pages;

    uint32_t used_space;
    uint32_t max_order_page_queue;
    char *db_file_path;
};

void add_page_to_cache(BufferManager *bm, void *page, uint32_t page_id) {

    if (rbt_size(bm->cached_pages) >= MAX_CACHED_PAGES) {
        // When cache overflows, write back 20%ish to db
        uint32_t cnt = MAX_CACHED_PAGES * (uint32_t)80 / (uint32_t)100;
        for (int i = 0; i < cnt; i++) {
            CachedPage cached_page = *(CachedPage*)heap_top(bm->cached_pages_queue);
            heap_pop(bm->cached_pages_queue);
            Page *page = buffer_manager_get_page(bm, cached_page.page_id);
            write_page_to_db(bm->db_file_path, (void*)page);
            buffer_manager_free_page(bm, cached_page.page_id);
        }
    }
    // printf("Adding page (%d) to cache\n", page_id);

    
    CachedPage p = {
        page_id,
        bm->max_order_page_queue++
    };
    heap_insert(bm->cached_pages_queue, (void*)&p);

    // Inserts a pointer to the page- as the value
    rbt_insert(bm->cached_pages, page_id, &page, sizeof(void*));
}

BufferManager *buffer_manager_init(char *db_file_path) {
    BufferManager *bm = malloc(sizeof(BufferManager));
    bm->cached_pages = rbt_init();
    bm->cached_pages_queue = new_heap(cached_page_lesser, sizeof(CachedPage));
    bm->available_pages = new_minheap();
    bm->nonfull_data_pages = new_heap(free_page_greater, sizeof(FreeDataPage));
    bm->used_space = 1;
    bm->max_order_page_queue = 0;
    bm->db_file_path = db_file_path;
    return bm;
}

void buffer_manager_free(BufferManager *bm) {
    while (!heap_is_empty(bm->cached_pages_queue)) {
        CachedPage page = *(CachedPage*)heap_top(bm->cached_pages_queue);
        heap_pop(bm->cached_pages_queue);
        buffer_manager_free_page(bm, page.page_id);
    }
    rbt_free(bm->cached_pages);
    heap_free(bm->cached_pages_queue);
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
    void *page = buffer_manager_get_page(bm, page_id);
    uint8_t page_type = *(uint8_t*)page;
    if (page_type == DATA_PAGE) {
        DataPage *data_page = page;
        free(data_page->data);
        free(data_page);

        // Update that this page actually is available now
    }
    else if (page_type == BPT_PAGE) {
        Page *bpt_page = page;
        if (bpt_page->header.is_leaf) free(bpt_page->leaf);
        else free(bpt_page->internal);
        free(bpt_page);

        // Update that this page actually is available now
    }
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
                    page->occupied_slots++;

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
            page->occupied_slots += 1;

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
    page->occupied_slots = 1;
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

// Helper to find element in heap sequentially
uint8_t _validator(void *a, void *b) {
    return ((FreeDataPage*)a)->page_id == *(uint32_t*)b;
}

void buffer_manager_free_data(BufferManager *bm, RID rid) {
    DataPage *page = buffer_manager_get_page(bm, rid.page_id);
    page->occupied_slots--;
    if (page->occupied_slots == 0) {
        buffer_manager_free_page(bm, rid.page_id);
    }
    else {
        SlotEntry slot_entry = get_slot_entryi(page, rid.slot_id);
        slot_entry.flags = SLOT_FLAG_FREE;
        write_slot_entryi(page->data, slot_entry, rid.slot_id);
        FreeDataPage *free_page = heap_find_sequentially(bm->nonfull_data_pages, 
            _validator, rid.page_id);
        free_page->free_space += slot_entry.length;
    }
}

// Does not yet handle overflow pages
void *buffer_manager_get_data(BufferManager *bm, RID rid) {
    DataPage *page = buffer_manager_get_page(bm, rid.page_id);
    SlotEntry s = get_slot_entryi(page, rid.slot_id);
    void *data = (page->data + s.offset);
    return data;
}

void buffer_manager_flush_cache(BufferManager *bm) {
    while (!heap_is_empty(bm->cached_pages_queue)) {
        CachedPage cached_page = *(CachedPage*)heap_top(bm->cached_pages_queue);
        heap_pop(bm->cached_pages_queue);
        void **page = rbt_get(bm->cached_pages, cached_page.page_id);
        write_page_to_db(bm->db_file_path, *page);
        rbt_delete(bm->cached_pages, cached_page.page_id, sizeof(void*));
    }
}