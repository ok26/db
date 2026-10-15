#ifndef RECORD_MANAGER_H
#define RECORD_MANAGER_H

#include <stdint.h>
#include "buffer_manager.h"

// The record manager stores variable sized records (byte strings) in data
// pages and hands out record ids (RID = page_id, slot_id) to find them again.
// A RID stays valid until the record is deleted, even if the record is moved
// around inside its page when the page is compacted.
//
// Records larger than MAX_INLINE_RECORD_SIZE are stored in a chain of
// overflow pages, see pager.h for the layout.
//
// Data pages with at least FREE_SPACE_THRESHOLD free bytes are kept in a
// doubly linked list (head stored in the header meta slot
// DB_META_FREE_SPACE_LIST) so that new records can be placed in pages that
// already exist. Pages that become empty are returned to the freelist.

#define MAX_INLINE_RECORD_SIZE (DATA_PAGE_DATA_SIZE - SLOT_ENTRY_SIZE)
#define FREE_SPACE_THRESHOLD 128
// Pages of the free-space list looked at before a new page is allocated
#define FREE_SPACE_SEARCH_LIMIT 8

DBStatus record_insert(BufferManager *bm, const void *data, uint32_t size, RID *rid);
// Reads a record into a newly allocated buffer which the caller has to free.
// Returns DB_NOT_FOUND if there is no record with this RID.
DBStatus record_read(BufferManager *bm, RID rid, void **data, uint32_t *size);
DBStatus record_delete(BufferManager *bm, RID rid);
// Replaces the content of a record. The record keeps its RID if the new
// content fits in the old space, otherwise it is moved and new_rid differs.
DBStatus record_update(BufferManager *bm, RID rid, const void *data,
    uint32_t size, RID *new_rid);

#endif
