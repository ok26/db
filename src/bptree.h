#ifndef BPTREE_H
#define BPTREE_H

#include <stdint.h>
#include <stdio.h>
#include "buffer_manager.h"

// B+tree mapping uint32_t keys to records of arbitrary size.
//
// Internal nodes and leaves are pages of type PAGE_TYPE_BPT. Leaves store
// (key, RID) pairs, the records themselves are stored by the record manager
// in data pages. Leaves are linked through next_page_id for range scans.
//
// The root page of a tree never changes: when the root splits its content is
// moved into a new page below it, and when the tree shrinks the last child is
// copied back into the root. The root page id can therefore be stored
// somewhere (e.g. in a header meta slot or a catalog) to open the tree again.
//
// The tree does not own the buffer manager. Operations are not atomic if
// they fail with an I/O error halfway, there is no journal yet.

typedef struct BPTree BPTree;

// Return non-zero to stop the range query. data is only valid during the call.
typedef int (*BPTRangeCallback)(uint32_t key, const void *data, uint32_t size,
    void *ctx);

typedef struct BPTStats {
    uint32_t height;
    uint64_t key_count;
    uint64_t leaf_count;
    uint64_t internal_count;
} BPTStats;

// Creates a new, empty tree
DBStatus bpt_create(BufferManager *bm, BPTree **bpt);
// Opens an existing tree by its root page id
DBStatus bpt_open(BufferManager *bm, uint32_t root_page_id, BPTree **bpt);
// Frees the handle, the tree itself stays in the database
void bpt_close(BPTree *bpt);
// Deletes all pages and records of the tree and frees the handle
DBStatus bpt_destroy(BPTree *bpt);
uint32_t bpt_root_page_id(BPTree *bpt);

// Inserts a key, or replaces the record if the key already exists
DBStatus bpt_insert(BPTree *bpt, uint32_t key, const void *data, uint32_t size);
// Reads the record of a key into a newly allocated buffer that the caller
// has to free. Returns DB_NOT_FOUND if the key does not exist.
DBStatus bpt_get(BPTree *bpt, uint32_t key, void **data, uint32_t *size);
// Returns DB_NOT_FOUND if the key does not exist
DBStatus bpt_delete(BPTree *bpt, uint32_t key);
// Calls the callback for every key in [key_low, key_high] in ascending
// order. The tree must not be modified from within the callback.
DBStatus bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high,
    BPTRangeCallback callback, void *ctx);

DBStatus bpt_height(BPTree *bpt, uint32_t *height);
DBStatus bpt_empty(BPTree *bpt, uint8_t *empty);

// Walks the whole tree (and reads every record) and checks all invariants:
// key order and separator bounds, node fill, equal leaf depth and the leaf
// chain. Returns DB_ERR_CORRUPT and prints the problem if one is broken.
// stats may be NULL.
DBStatus bpt_verify(BPTree *bpt, BPTStats *stats);
// Prints the keys of every node, one node per line, indented by depth
DBStatus bpt_print(BPTree *bpt, FILE *out);

#endif
