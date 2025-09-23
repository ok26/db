#include <stdlib.h>
#include <stdio.h>
#include "buffer_manager.h"
#include "bptree.h"
#include "util.h"

#define MAX_CACHED_PAGES 256

struct BufferManager {
    Map *cached_pages;
    Heap *freed_pages;
    uint32_t used_space;
    char *db_file_path;
};

void add_page_to_cache(BufferManager *bm, Page *page) {
    if (rbt_size(bm->cached_pages) >= MAX_CACHED_PAGES) {
        // TODO: deletion
        return;
    }

    rbt_insert(bm->cached_pages, page->page_id, (void*)page);
}

void write_page_to_db(BufferManager *bm, Page *page) {

}

Page *read_page_from_db(BufferManager *bm, uint32_t page_id) {
    // There will be different types of pages, 
    // Structure of the pages will be determined here:
    // All pages will start with some sort of header
    // Initially maybe just with what type of page it is
    return NULL;
}

BufferManager *buffer_manager_init(char *db_file_path) {
    BufferManager *bm = malloc(sizeof(BufferManager));
    bm->cached_pages = rbt_init();
    bm->freed_pages = new_minheap();
    bm->used_space = 0;
    bm->db_file_path = db_file_path;
    return bm;
}

void buffer_manager_free(BufferManager *bm) {
    rbt_free(bm->cached_pages);
    heap_free(bm->freed_pages);
    // free(bm->db_file_path); Passer should handle this
    free(bm);
}

Page *buffer_manager_new_page(BufferManager *bm, uint8_t is_leaf) {
    Page *page = malloc(sizeof(Page));
    page->is_leaf = is_leaf;
    page->num_keys = 0;

    uint32_t page_id;
    if (!heap_is_empty(bm->freed_pages)) {
        page_id = heap_top(bm->freed_pages);
        heap_pop(bm->freed_pages);

        // Update the info of freed pages (might be done when writing back to db)
    }
    else {
        page_id = bm->used_space++;
    }
    page->page_id = page_id;

    add_page_to_cache(bm, page);
    return page;
}

Page *buffer_manager_get_page(BufferManager *bm, uint32_t page_id) {
    Page *page = rbt_get(bm->cached_pages, page_id);
    if (page) {
        return page;
    }

    Page *page = read_page_from_db(bm, page_id);
    if (!page) {
        printf("Invalid page-id: %u\n", page_id);
        return NULL;
    }

    add_page_to_cache(bm, page);
    return page;
}