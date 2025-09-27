#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "buffer_manager.h"
#include "util.h"

#define PAGE_SIZE (off_t)4096
#define MAX_CACHED_PAGES 256

#define BPT_PAGE (uint8_t)0x0
#define DATA_PAGE (uint8_t)0x1
#define OVERFLOW_PAGE (uint8_t)0x2

#define SLOT_FLAG_FREE (uint8_t)0x1
#define SLOT_FLAG_OVERFLOW (uint8_t)0x2

struct BufferManager {
    Map *cached_pages;
    Heap *available_pages;
    uint32_t used_space;
    char *db_file_path;
};

uint16_t bytes_to_u16_be(uint8_t b[2]) {
    return ((uint16_t)b[0] << 8) |
           ((uint16_t)b[1]);
}

uint32_t bytes_to_u32_be(uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |
           ((uint32_t)b[3]);
}

uint64_t bytes_to_u64_be(uint8_t b[8]) {
    return ((uint64_t)b[0] << 56) |
           ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) |
           ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) |
           ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  |
           ((uint64_t)b[7]);
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

void u64_to_bytes_be(uint8_t b[8], uint64_t v) {
    b[0] = (uint8_t)(v >> 56);
    b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40);
    b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24);
    b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >> 8);
    b[7] = (uint8_t)(v & 0xFF);
}

void add_page_to_cache(BufferManager *bm, Page *page) {
    if (rbt_size(bm->cached_pages) >= MAX_CACHED_PAGES) {
        // TODO: deletion
        return;
    }

    rbt_insert(bm->cached_pages, page->header.page_id, (void*)page);
}

void bpt_page_to_db(Page *page, uint8_t buffer[PAGE_SIZE]) {
    buffer[0x1] = page->header.is_leaf;
    u16_to_bytes_be(&buffer[0x2], page->header.num_keys);

    if (page->header.is_leaf) {
        off_t offset = 0x4;
        memcpy(&buffer[offset], page->leaf->keys,
            sizeof(uint32_t) * page->header.num_keys);
        offset += sizeof(uint32_t) * page->header.num_keys;
        for (int i = 0; i < page->header.num_keys; i++) {
            u32_to_bytes_be(&buffer[offset], page->leaf->page_ids[i]);
            offset += sizeof(uint32_t);
            u16_to_bytes_be(&buffer[offset], page->leaf->slot_ids[i]);
            offset += sizeof(uint16_t);
        }
        u32_to_bytes_be(&buffer[offset], page->leaf->next_page_id);
    }
    else {
        off_t offset = 0x4;
        memcpy(&buffer[offset], page->internal->keys,
            sizeof(uint32_t) * page->header.num_keys);
        offset += sizeof(uint32_t) * page->header.num_keys;
        memcpy(&buffer[offset], page->internal->children,
            sizeof(uint32_t) * (page->header.num_keys + 1));
    }

    return;
}

void data_page_to_db(DataPage *page, uint8_t buffer[PAGE_SIZE]) {
    u16_to_bytes_be(&buffer[0x1], page->num_slots);
    u16_to_bytes_be(&buffer[0x3], page->free_space_start);
    u16_to_bytes_be(&buffer[0x5], page->free_space_end);
    memcpy(&buffer[0x7], page->slot_directory, 0x5 * page->num_slots);
    memcpy(&buffer[page->free_space_end], page->records,
        PAGE_SIZE - page->free_space_end);
    return;
}

void write_page_to_db(BufferManager *bm, void *page, uint8_t page_type) {

    uint8_t buffer[PAGE_SIZE];
    buffer[0] = page_type;
    uint32_t page_id;
    switch (buffer[0]) {
        case BPT_PAGE:
            Page *bpt_page = page;
            page_id = bpt_page->header.page_id;
            bpt_page_to_db(bpt_page, buffer);
            break;
        
        case DATA_PAGE:
            DataPage *data_page = page;
            page_id = data_page->page_id;
            data_page_to_db(data_page, buffer);
            break;
        
        default:
            printf("Invalid page: %p", page);
            return;
    }
    
    int fd = open(bm->db_file_path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return;
    }

    off_t offset = (off_t)page_id * PAGE_SIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        close(fd);
        return;
    }

    ssize_t written = write(fd, buffer, PAGE_SIZE);
    if (written != PAGE_SIZE) {
        perror("write");
    }

    close(fd);
    return;
}

uint32_t page_id_from_rid(uint64_t rid) {
    return (uint32_t)(rid & 0xffffffff);
}

uint16_t slot_id_from_rid(uint64_t rid) {
    return (uint16_t)(rid >> 32);
}

uint64_t rid_from_ids(uint32_t page_id, uint16_t slot_id) {
    return (uint64_t)page_id + (uint64_t)slot_id << 32ULL;
}

Page *get_bpt_page(uint8_t raw_page[PAGE_SIZE], uint32_t page_id) {
    PageHeader header;
    header.is_leaf = raw_page[0x1];
    header.num_keys = bytes_to_u16_be(&raw_page[0x2]);
    header.page_id = page_id;

    Page *page = malloc(sizeof(Page));
    page->header = header;
    
    if (page->header.is_leaf) {
        LeafPage *leaf = malloc(sizeof(LeafPage));
        off_t offset = 0x4;

        memcpy(leaf->keys, &raw_page[offset], 
            sizeof(uint32_t) * header.num_keys);
        offset += sizeof(uint32_t) * header.num_keys;

        for (int i = 0; i < header.num_keys; i++) {
            uint32_t pid = bytes_to_u32_be(&raw_page[offset]);
            uint16_t sid = bytes_to_u16_be(&raw_page[offset + sizeof(uint32_t)]);
            leaf->page_ids[i] = pid;
            leaf->slot_ids[i] = sid;
            offset += sizeof(uint32_t) + sizeof(uint16_t);
        }

        leaf->next_page_id = bytes_to_u32_be(&raw_page[offset]);

        page->leaf = leaf;
    }
    else {
        InternalPage *internal = malloc(sizeof(InternalPage));
        off_t offset = 0x4;

        memcpy(internal->keys, &raw_page[offset],
            sizeof(uint32_t) * header.num_keys);
        offset += sizeof(uint32_t) * header.num_keys;

        memcpy(internal->children, &raw_page[offset],
            sizeof(uint32_t) * (header.num_keys + 1));

        page->internal = internal;
    }

    return page;
}

DataPage *get_data_page(uint8_t raw_page[PAGE_SIZE], uint32_t page_id) {
    DataPage *page = malloc(sizeof(DataPage));
    page->page_id = page_id;
    page->num_slots = bytes_to_u16_be(&raw_page[0x1]);
    page->free_space_start = bytes_to_u16_be(&raw_page[0x3]);
    page->free_space_end = bytes_to_u16_be(&raw_page[0x5]);

    page->slot_directory = malloc(0x5 * page->num_slots);
    memcpy(page->slot_directory, &raw_page[0x7], 0x5 * page->num_slots);
    page->records = malloc(PAGE_SIZE - page->free_space_end);
    memcpy(page->records, &raw_page[page->free_space_end],
        PAGE_SIZE - page->free_space_end);

    return page;
}

void *read_page_from_db(BufferManager *bm, uint32_t page_id) {
    int fd = open(bm->db_file_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    off_t offset = (off_t)page_id * PAGE_SIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        close(fd);
        return NULL;
    }

    uint8_t buffer[PAGE_SIZE];
    ssize_t r = read(fd, buffer, PAGE_SIZE);
    if (r < 0) {
        perror("read");
        close(fd);
        return NULL;
    }

    close(fd);

    switch (buffer[0]) {
        case BPT_PAGE:
            return (void*)get_bpt_page(buffer, page_id);
        
        case DATA_PAGE:
            return (void*)get_data_page(buffer, page_id);
        
        default:
            printf("Invalid page with id: %u", page_id);
            break;
    }

    return NULL;
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