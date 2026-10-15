#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "buffer_manager.h"

#define NO_FRAME (-1)

typedef struct Frame {
    // Has to be the first member so that a Page* handed out to the user can
    // be converted back into its Frame* (see frame_of)
    Page page;          // page.page_id == NULL_PAGE_ID if the frame is empty
    uint32_t pin_count;
    uint8_t dirty;
    int32_t hash_next;  // Next frame in the same page table bucket
    int32_t lru_prev;   // Neighbours in the LRU list, or in the list
    int32_t lru_next;   // of empty frames (only lru_next is used there)
} Frame;

struct BufferManager {
    Pager *pager;
    DBHeader header;
    uint8_t header_dirty;

    Frame *frames;
    uint32_t frame_count;

    // Page table, a hash map from page_id to frame index. Frames that hash
    // to the same bucket are chained through Frame.hash_next
    int32_t *buckets;
    uint32_t bucket_shift;

    // Frames that do not hold any page
    int32_t empty_frames;

    // Unpinned frames that hold a page, from least to most recently used.
    // Pages are evicted from the head.
    int32_t lru_head;
    int32_t lru_tail;

    uint32_t cached_pages;
    uint32_t pinned_pages;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writes;
};


// ---- Page table ----

static uint32_t bucket_of(BufferManager *bm, uint32_t page_id) {
    // Fibonacci hashing, the top bits of the product are the bucket
    return (uint32_t)(page_id * 2654435769u) >> bm->bucket_shift;
}

static Frame *table_lookup(BufferManager *bm, uint32_t page_id) {
    int32_t idx = bm->buckets[bucket_of(bm, page_id)];
    while (idx != NO_FRAME) {
        if (bm->frames[idx].page.page_id == page_id) return &bm->frames[idx];
        idx = bm->frames[idx].hash_next;
    }
    return NULL;
}

static void table_insert(BufferManager *bm, int32_t idx) {
    uint32_t bucket = bucket_of(bm, bm->frames[idx].page.page_id);
    bm->frames[idx].hash_next = bm->buckets[bucket];
    bm->buckets[bucket] = idx;
}

static void table_remove(BufferManager *bm, int32_t idx) {
    int32_t *link = &bm->buckets[bucket_of(bm, bm->frames[idx].page.page_id)];
    while (*link != idx) {
        assert(*link != NO_FRAME);
        link = &bm->frames[*link].hash_next;
    }
    *link = bm->frames[idx].hash_next;
}


// ---- LRU list ----

static void lru_remove(BufferManager *bm, int32_t idx) {
    Frame *f = &bm->frames[idx];
    if (f->lru_prev != NO_FRAME) bm->frames[f->lru_prev].lru_next = f->lru_next;
    else bm->lru_head = f->lru_next;
    if (f->lru_next != NO_FRAME) bm->frames[f->lru_next].lru_prev = f->lru_prev;
    else bm->lru_tail = f->lru_prev;
    f->lru_prev = f->lru_next = NO_FRAME;
}

static void lru_push_back(BufferManager *bm, int32_t idx) {
    Frame *f = &bm->frames[idx];
    f->lru_prev = bm->lru_tail;
    f->lru_next = NO_FRAME;
    if (bm->lru_tail != NO_FRAME) bm->frames[bm->lru_tail].lru_next = idx;
    else bm->lru_head = idx;
    bm->lru_tail = idx;
}


// ---- Frames ----

static Frame *frame_of(BufferManager *bm, Page *page) {
    Frame *f = (Frame*)page;
    assert(f >= bm->frames && f < bm->frames + bm->frame_count);
    assert(f->page.page_id != NULL_PAGE_ID);
    return f;
}

static int32_t frame_index(BufferManager *bm, Frame *f) {
    return (int32_t)(f - bm->frames);
}

static void pin(BufferManager *bm, Frame *f) {
    if (f->pin_count == 0) {
        lru_remove(bm, frame_index(bm, f));
        bm->pinned_pages++;
    }
    f->pin_count++;
}

// Finds a frame that can be used for a new page, evicting the least
// recently used page if there is no empty frame
static DBStatus acquire_frame(BufferManager *bm, int32_t *idx) {
    if (bm->empty_frames != NO_FRAME) {
        *idx = bm->empty_frames;
        bm->empty_frames = bm->frames[*idx].lru_next;
        return DB_OK;
    }

    if (bm->lru_head == NO_FRAME) {
        fprintf(stderr, "buffer_manager: all %u frames are pinned\n", bm->frame_count);
        return DB_ERR_FULL;
    }

    int32_t victim = bm->lru_head;
    Frame *f = &bm->frames[victim];
    if (f->dirty) {
        DBStatus rc = pager_write_page(bm->pager, &f->page);
        if (rc != DB_OK) return rc;
        f->dirty = 0;
        bm->writes++;
    }

    lru_remove(bm, victim);
    table_remove(bm, victim);
    f->page.page_id = NULL_PAGE_ID;
    bm->cached_pages--;
    bm->evictions++;
    *idx = victim;
    return DB_OK;
}

// Returns an acquired frame that could not be used to the empty frames
static void release_frame(BufferManager *bm, int32_t idx) {
    bm->frames[idx].page.page_id = NULL_PAGE_ID;
    bm->frames[idx].lru_next = bm->empty_frames;
    bm->empty_frames = idx;
}

// Makes an acquired frame (with page.page_id set) visible as a pinned page
static void install_frame(BufferManager *bm, int32_t idx, uint8_t dirty) {
    Frame *f = &bm->frames[idx];
    table_insert(bm, idx);
    f->pin_count = 1;
    f->dirty = dirty;
    f->lru_prev = f->lru_next = NO_FRAME;
    bm->cached_pages++;
    bm->pinned_pages++;
}


// ---- Public interface ----

DBStatus buffer_manager_open(const char *db_file_path, uint32_t frame_count,
        BufferManager **bm_out) {
    *bm_out = NULL;
    if (frame_count == 0) frame_count = BUFFER_MANAGER_DEFAULT_FRAMES;
    if (frame_count < BUFFER_MANAGER_MIN_FRAMES || frame_count > (1u << 24)) {
        return DB_ERR_INVALID;
    }

    BufferManager *bm = calloc(1, sizeof(BufferManager));
    if (!bm) return DB_ERR_NOMEM;

    DBStatus rc = pager_open(db_file_path, &bm->pager);
    if (rc != DB_OK) goto fail;

    uint64_t file_size;
    rc = pager_file_size(bm->pager, &file_size);
    if (rc != DB_OK) goto fail;

    if (file_size == 0) {
        // New database, only the header page exists
        bm->header.page_count = 1;
        rc = pager_write_header(bm->pager, &bm->header);
    }
    else {
        rc = pager_read_header(bm->pager, &bm->header);
    }
    if (rc != DB_OK) goto fail;

    // Page table with at least two buckets per frame
    uint32_t bucket_bits = 1;
    while ((1u << bucket_bits) < 2 * frame_count) bucket_bits++;
    bm->bucket_shift = 32 - bucket_bits;
    bm->buckets = malloc(sizeof(int32_t) << bucket_bits);
    bm->frames = calloc(frame_count, sizeof(Frame));
    if (!bm->buckets || !bm->frames) {
        rc = DB_ERR_NOMEM;
        goto fail;
    }
    for (uint32_t i = 0; i < (1u << bucket_bits); i++) bm->buckets[i] = NO_FRAME;

    bm->frame_count = frame_count;
    for (uint32_t i = 0; i < frame_count; i++) {
        bm->frames[i].lru_next = i + 1 < frame_count ? (int32_t)(i + 1) : NO_FRAME;
    }
    bm->empty_frames = 0;
    bm->lru_head = bm->lru_tail = NO_FRAME;

    *bm_out = bm;
    return DB_OK;

fail:
    pager_close(bm->pager);
    free(bm->buckets);
    free(bm->frames);
    free(bm);
    return rc;
}

DBStatus buffer_manager_flush(BufferManager *bm) {
    for (uint32_t i = 0; i < bm->frame_count; i++) {
        Frame *f = &bm->frames[i];
        if (f->page.page_id == NULL_PAGE_ID || !f->dirty) continue;
        DBStatus rc = pager_write_page(bm->pager, &f->page);
        if (rc != DB_OK) return rc;
        f->dirty = 0;
        bm->writes++;
    }

    if (bm->header_dirty) {
        DBStatus rc = pager_write_header(bm->pager, &bm->header);
        if (rc != DB_OK) return rc;
        bm->header_dirty = 0;
    }

    return pager_sync(bm->pager);
}

DBStatus buffer_manager_close(BufferManager *bm) {
    if (!bm) return DB_OK;
    if (bm->pinned_pages != 0) {
        fprintf(stderr, "buffer_manager: closing with %u pinned pages\n",
            bm->pinned_pages);
    }

    DBStatus rc = buffer_manager_flush(bm);
    pager_close(bm->pager);
    free(bm->buckets);
    free(bm->frames);
    free(bm);
    return rc;
}

DBStatus buffer_manager_get_page(BufferManager *bm, uint32_t page_id, Page **page) {
    *page = NULL;
    if (page_id == NULL_PAGE_ID || page_id >= bm->header.page_count) {
        return DB_ERR_INVALID;
    }

    Frame *f = table_lookup(bm, page_id);
    if (f) {
        pin(bm, f);
        bm->hits++;
        *page = &f->page;
        return DB_OK;
    }

    int32_t idx;
    DBStatus rc = acquire_frame(bm, &idx);
    if (rc != DB_OK) return rc;

    f = &bm->frames[idx];
    rc = pager_read_page(bm->pager, page_id, &f->page);
    if (rc != DB_OK) {
        release_frame(bm, idx);
        return rc;
    }

    install_frame(bm, idx, 0);
    bm->misses++;
    *page = &f->page;
    return DB_OK;
}

DBStatus buffer_manager_new_page(BufferManager *bm, uint8_t page_type, Page **page) {
    *page = NULL;
    if (page_type != PAGE_TYPE_BPT && page_type != PAGE_TYPE_DATA &&
            page_type != PAGE_TYPE_OVERFLOW) {
        return DB_ERR_INVALID;
    }

    if (bm->header.freelist_head != NULL_PAGE_ID) {
        // Reuse a page from the freelist
        uint32_t page_id = bm->header.freelist_head;
        Page *free_page;
        DBStatus rc = buffer_manager_get_page(bm, page_id, &free_page);
        if (rc != DB_OK) return rc;

        uint32_t next = free_page->free.next_free_page;
        if (free_page->page_type != PAGE_TYPE_FREE || next >= bm->header.page_count ||
                bm->header.freelist_count == 0) {
            fprintf(stderr, "buffer_manager: freelist is corrupt at page %u\n", page_id);
            buffer_manager_unpin(bm, free_page);
            return DB_ERR_CORRUPT;
        }

        bm->header.freelist_head = next;
        bm->header.freelist_count--;
        bm->header_dirty = 1;

        page_init(free_page, page_id, page_type);
        buffer_manager_mark_dirty(bm, free_page);
        *page = free_page;
        return DB_OK;
    }

    // Grow the file. The page is only written once it is evicted or flushed
    if (bm->header.page_count == UINT32_MAX) {
        return DB_ERR_FULL;
    }

    int32_t idx;
    DBStatus rc = acquire_frame(bm, &idx);
    if (rc != DB_OK) return rc;

    uint32_t page_id = bm->header.page_count++;
    bm->header_dirty = 1;

    Frame *f = &bm->frames[idx];
    page_init(&f->page, page_id, page_type);
    install_frame(bm, idx, 1);
    *page = &f->page;
    return DB_OK;
}

void buffer_manager_unpin(BufferManager *bm, Page *page) {
    Frame *f = frame_of(bm, page);
    assert(f->pin_count > 0);
    f->pin_count--;
    if (f->pin_count == 0) {
        bm->pinned_pages--;
        lru_push_back(bm, frame_index(bm, f));
    }
}

void buffer_manager_mark_dirty(BufferManager *bm, Page *page) {
    Frame *f = frame_of(bm, page);
    assert(f->pin_count > 0);
    f->dirty = 1;
}

DBStatus buffer_manager_free_page(BufferManager *bm, Page *page) {
    Frame *f = frame_of(bm, page);
    if (f->pin_count != 1) {
        fprintf(stderr, "buffer_manager: cannot free page %u, it is pinned %u times\n",
            page->page_id, f->pin_count);
        return DB_ERR_INVALID;
    }

    uint32_t page_id = page->page_id;
    page_init(page, page_id, PAGE_TYPE_FREE);
    page->free.next_free_page = bm->header.freelist_head;
    bm->header.freelist_head = page_id;
    bm->header.freelist_count++;
    bm->header_dirty = 1;

    f->dirty = 1;
    buffer_manager_unpin(bm, page);
    return DB_OK;
}

uint32_t buffer_manager_get_meta(BufferManager *bm, uint32_t idx) {
    assert(idx < DB_META_COUNT);
    return bm->header.meta[idx];
}

void buffer_manager_set_meta(BufferManager *bm, uint32_t idx, uint32_t value) {
    assert(idx < DB_META_COUNT);
    if (bm->header.meta[idx] != value) {
        bm->header.meta[idx] = value;
        bm->header_dirty = 1;
    }
}

void buffer_manager_stats(BufferManager *bm, BufferManagerStats *stats) {
    stats->frame_count = bm->frame_count;
    stats->cached_pages = bm->cached_pages;
    stats->pinned_pages = bm->pinned_pages;
    stats->page_count = bm->header.page_count;
    stats->free_pages = bm->header.freelist_count;
    stats->hits = bm->hits;
    stats->misses = bm->misses;
    stats->evictions = bm->evictions;
    stats->writes = bm->writes;
}
