#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "pager.h"

// The buffer manager keeps a fixed number of pages (frames) in memory and
// is the only way the layers above access pages.
//
// Pages are pinned while they are used:
//  - buffer_manager_get_page / buffer_manager_new_page return a pinned page
//  - the pointer stays valid until the page is unpinned
//  - a modified page has to be marked dirty before it is unpinned
//  - every pin has to be matched with exactly one unpin
//    (or with buffer_manager_free_page)
// Only unpinned pages can be evicted from memory, dirty pages are written
// back to the file when they are evicted or when the cache is flushed.
//
// It also owns the database header (page 0) and allocates/frees pages.
// Freed pages are kept in a linked list on disk (the freelist) and are
// reused before the file is grown.

#define BUFFER_MANAGER_DEFAULT_FRAMES 1024
// Fewer frames than this could make the b+tree run out of frames even
// though it releases its pages as soon as possible
#define BUFFER_MANAGER_MIN_FRAMES 8

// Slots in the header available through buffer_manager_get/set_meta
// Head of the list of data pages with free space, used by record_manager
#define DB_META_FREE_SPACE_LIST 0
// Slots from here on are not used by the storage engine
#define DB_META_FIRST_USER 1

typedef struct BufferManagerStats {
    uint32_t frame_count;
    uint32_t cached_pages;   // Frames that currently hold a page
    uint32_t pinned_pages;   // Pages with pin_count > 0
    uint32_t page_count;     // Pages in the database incl. the header
    uint32_t free_pages;     // Pages in the freelist
    uint64_t hits;           // get_page calls served from memory
    uint64_t misses;         // get_page calls that had to read the file
    uint64_t evictions;
    uint64_t writes;         // Pages written to the file
} BufferManagerStats;

typedef struct BufferManager BufferManager;

// Opens a database file, creating it if it does not exist.
// frame_count = 0 uses BUFFER_MANAGER_DEFAULT_FRAMES.
DBStatus buffer_manager_open(const char *db_file_path, uint32_t frame_count,
    BufferManager **bm);
// Flushes and closes the database. The BufferManager is freed even if the
// flush fails, the failure is returned.
DBStatus buffer_manager_close(BufferManager *bm);
// Writes all dirty pages and the header to the file and syncs it
DBStatus buffer_manager_flush(BufferManager *bm);

DBStatus buffer_manager_get_page(BufferManager *bm, uint32_t page_id, Page **page);
// Allocates a new page of the given type, initialized with page_init()
DBStatus buffer_manager_new_page(BufferManager *bm, uint8_t page_type, Page **page);
void buffer_manager_unpin(BufferManager *bm, Page *page);
void buffer_manager_mark_dirty(BufferManager *bm, Page *page);
// Returns a page to the freelist. The page must be pinned exactly once
// (by the caller), that pin is released.
DBStatus buffer_manager_free_page(BufferManager *bm, Page *page);

uint32_t buffer_manager_get_meta(BufferManager *bm, uint32_t idx);
void buffer_manager_set_meta(BufferManager *bm, uint32_t idx, uint32_t value);

void buffer_manager_stats(BufferManager *bm, BufferManagerStats *stats);

#endif
