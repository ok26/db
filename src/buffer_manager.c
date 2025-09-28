#include <stdlib.h>
#include <stdio.h>

#include "buffer_manager.h"
#include "util.h"

#define MAX_CACHED_PAGES 256

struct BufferManager {
    Map *cached_pages;
    Heap *available_pages;
    uint32_t used_space;
    char *db_file_path;
};

void add_page_to_cache(BufferManager *bm, Page *page) {
    if (rbt_size(bm->cached_pages) >= MAX_CACHED_PAGES) {
        // TODO: deletion
        return;
    }

    rbt_insert(bm->cached_pages, page->header.page_id, (void*)page);
}

BufferManager *buffer_manager_init(char *db_file_path) {
    BufferManager *bm = malloc(sizeof(BufferManager));
    bm->cached_pages = rbt_init();
    bm->available_pages = new_minheap();
    bm->used_space = 0;
    bm->db_file_path = db_file_path;
    return bm;
}

void buffer_manager_free(BufferManager *bm) {
    rbt_free(bm->cached_pages);
    heap_free(bm->available_pages);
    // free(bm->db_file_path); Passer should handle this
    free(bm);
}

Page *buffer_manager_new_page(BufferManager *bm, uint8_t is_leaf) {
    Page *page = malloc(sizeof(Page));
    PageHeader header;
    header.is_leaf = is_leaf;
    header.num_keys = 0;

    if (!heap_is_empty(bm->available_pages)) {
        header.page_id = heap_top(bm->available_pages);
        heap_pop(bm->available_pages);

        // Update the info of freed pages (might be done when writing back to db)
    }
    else {
        header.page_id = bm->used_space++;
    }

    if (is_leaf) {
        page->leaf->next_page_id = 0;
    }

    add_page_to_cache(bm, page);
    return page;
}

Page *buffer_manager_get_page(BufferManager *bm, uint32_t page_id) {
    if (page_id == 0) {
        // Page-id 0 indicates NULL-page, page 0 is read with special function
        return NULL;
    }
    
    Page *page = rbt_get(bm->cached_pages, page_id);
    if (page) {
        return page;
    }

    page = read_page_from_db(bm, page_id);
    if (!page) {
        printf("Invalid page-id: %u\n", page_id);
        return NULL;
    }

    add_page_to_cache(bm, page);
    return page;
}

uint64_t buffer_manager_request_slot(size_t size) {
    // TODO, will find a free slot

    // Placeholder
    uint32_t slot_id = 0, page_id = 0;
    return (uint64_t)page_id + (uint64_t)slot_id << 32ULL;
}