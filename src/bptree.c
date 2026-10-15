#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bptree.h"
#include "record_manager.h"
#include "util.h"

// Trees deeper than this are treated as corrupt, which also protects
// against cycles. Even with the smallest possible order 2^32 keys only
// need about 32 levels.
#define BPT_MAX_HEIGHT 64

// The tree only remembers page ids, never Page pointers, as a page can be
// evicted from memory as soon as it is unpinned.
struct BPTree {
    BufferManager *bm;
    uint32_t root_page_id;
};

// Pages only point downwards, so the way from the root to a leaf is
// recorded to be able to find parents and siblings when nodes are split
// or merged. For every internal node on the way the index of the child
// that was followed is stored.
typedef struct PathEntry {
    uint32_t page_id;
    uint32_t child_idx;
} PathEntry;

typedef struct Path {
    PathEntry entries[BPT_MAX_HEIGHT];
    uint32_t depth;
} Path;


// ---- Helpers ----

static DBStatus pin_node(BPTree *bpt, uint32_t page_id, Page **page) {
    DBStatus rc = buffer_manager_get_page(bpt->bm, page_id, page);
    if (rc == DB_ERR_INVALID) rc = DB_ERR_CORRUPT;
    if (rc != DB_OK) return rc;

    if ((*page)->page_type != PAGE_TYPE_BPT) {
        fprintf(stderr, "bptree: page %u is not a b+tree page\n", page_id);
        buffer_manager_unpin(bpt->bm, *page);
        *page = NULL;
        return DB_ERR_CORRUPT;
    }
    return DB_OK;
}

static void unpin(BPTree *bpt, Page *page) {
    if (page) buffer_manager_unpin(bpt->bm, page);
}

static void mark_dirty(BPTree *bpt, Page *page) {
    buffer_manager_mark_dirty(bpt->bm, page);
}

static uint8_t rid_equal(RID a, RID b) {
    return a.page_id == b.page_id && a.slot_id == b.slot_id;
}

static void leaf_insert_at(BPTPage *node, uint32_t idx, uint32_t key, RID rid) {
    LeafPage *leaf = &node->leaf;
    uint32_t moved = node->num_keys - idx;
    memmove(&leaf->keys[idx + 1], &leaf->keys[idx], moved * sizeof(uint32_t));
    memmove(&leaf->rids[idx + 1], &leaf->rids[idx], moved * sizeof(RID));
    leaf->keys[idx] = key;
    leaf->rids[idx] = rid;
    node->num_keys++;
}

static void leaf_remove_at(BPTPage *node, uint32_t idx) {
    LeafPage *leaf = &node->leaf;
    uint32_t moved = node->num_keys - idx - 1;
    memmove(&leaf->keys[idx], &leaf->keys[idx + 1], moved * sizeof(uint32_t));
    memmove(&leaf->rids[idx], &leaf->rids[idx + 1], moved * sizeof(RID));
    node->num_keys--;
}

// Inserts keys[idx] = key and children[idx + 1] = right_child
static void internal_insert_at(BPTPage *node, uint32_t idx, uint32_t key,
        uint32_t right_child) {
    InternalPage *internal = &node->internal;
    uint32_t moved = node->num_keys - idx;
    memmove(&internal->keys[idx + 1], &internal->keys[idx], moved * sizeof(uint32_t));
    memmove(&internal->children[idx + 2], &internal->children[idx + 1],
        moved * sizeof(uint32_t));
    internal->keys[idx] = key;
    internal->children[idx + 1] = right_child;
    node->num_keys++;
}

// Removes keys[idx] and children[idx + 1]
static void internal_remove_at(BPTPage *node, uint32_t idx) {
    InternalPage *internal = &node->internal;
    uint32_t moved = node->num_keys - idx - 1;
    memmove(&internal->keys[idx], &internal->keys[idx + 1], moved * sizeof(uint32_t));
    memmove(&internal->children[idx + 1], &internal->children[idx + 2],
        moved * sizeof(uint32_t));
    node->num_keys--;
}

static uint8_t can_lend(const BPTPage *node) {
    if (node->is_leaf) return node->num_keys > MIN_ENTRIES_LEAF;
    return node->num_keys + 1 > MIN_CHILDREN;
}

static uint8_t is_underfull(const BPTPage *node) {
    if (node->is_leaf) return node->num_keys < MIN_ENTRIES_LEAF;
    return node->num_keys + 1 < MIN_CHILDREN;
}

// Walks from the root down to the leaf that should contain key. The leaf is
// returned pinned.
static DBStatus find_leaf(BPTree *bpt, uint32_t key, Path *path, Page **leaf) {
    path->depth = 0;
    uint32_t page_id = bpt->root_page_id;

    for (;;) {
        Page *page;
        DBStatus rc = pin_node(bpt, page_id, &page);
        if (rc != DB_OK) return rc;

        BPTPage *node = &page->bpt;
        if (node->is_leaf) {
            *leaf = page;
            return DB_OK;
        }

        if (path->depth == BPT_MAX_HEIGHT) {
            fprintf(stderr, "bptree: tree is deeper than %d levels\n", BPT_MAX_HEIGHT);
            unpin(bpt, page);
            return DB_ERR_CORRUPT;
        }

        uint32_t idx = upper_bound(node->internal.keys, node->num_keys, key);
        path->entries[path->depth].page_id = page_id;
        path->entries[path->depth].child_idx = idx;
        path->depth++;
        page_id = node->internal.children[idx];
        unpin(bpt, page);
    }
}


// ---- Opening and closing ----

DBStatus bpt_create(BufferManager *bm, BPTree **bpt_out) {
    *bpt_out = NULL;
    BPTree *bpt = malloc(sizeof(BPTree));
    if (!bpt) return DB_ERR_NOMEM;

    Page *root;
    DBStatus rc = buffer_manager_new_page(bm, PAGE_TYPE_BPT, &root);
    if (rc != DB_OK) {
        free(bpt);
        return rc;
    }

    // The page is zeroed, only is_leaf has to be set
    root->bpt.is_leaf = BPT_LEAF;
    bpt->bm = bm;
    bpt->root_page_id = root->page_id;
    buffer_manager_unpin(bm, root);

    *bpt_out = bpt;
    return DB_OK;
}

DBStatus bpt_open(BufferManager *bm, uint32_t root_page_id, BPTree **bpt_out) {
    *bpt_out = NULL;

    Page *root;
    DBStatus rc = buffer_manager_get_page(bm, root_page_id, &root);
    if (rc != DB_OK) return rc;
    uint8_t is_bpt = root->page_type == PAGE_TYPE_BPT;
    buffer_manager_unpin(bm, root);
    if (!is_bpt) return DB_ERR_INVALID;

    BPTree *bpt = malloc(sizeof(BPTree));
    if (!bpt) return DB_ERR_NOMEM;
    bpt->bm = bm;
    bpt->root_page_id = root_page_id;

    *bpt_out = bpt;
    return DB_OK;
}

void bpt_close(BPTree *bpt) {
    free(bpt);
}

uint32_t bpt_root_page_id(BPTree *bpt) {
    return bpt->root_page_id;
}


// ---- Lookup ----

DBStatus bpt_get(BPTree *bpt, uint32_t key, void **data, uint32_t *size) {
    *data = NULL;
    *size = 0;

    Path path;
    Page *leaf;
    DBStatus rc = find_leaf(bpt, key, &path, &leaf);
    if (rc != DB_OK) return rc;

    BPTPage *node = &leaf->bpt;
    uint32_t idx = lower_bound(node->leaf.keys, node->num_keys, key);
    if (idx == node->num_keys || node->leaf.keys[idx] != key) {
        unpin(bpt, leaf);
        return DB_NOT_FOUND;
    }

    RID rid = node->leaf.rids[idx];
    unpin(bpt, leaf);

    rc = record_read(bpt->bm, rid, data, size);
    // The key exists, so a missing record means the tree is broken
    if (rc == DB_NOT_FOUND) rc = DB_ERR_CORRUPT;
    return rc;
}

DBStatus bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high,
        BPTRangeCallback callback, void *ctx) {
    if (key_high < key_low) {
        return DB_OK;
    }

    Path path;
    Page *leaf;
    DBStatus rc = find_leaf(bpt, key_low, &path, &leaf);
    if (rc != DB_OK) return rc;

    uint32_t idx = lower_bound(leaf->bpt.leaf.keys, leaf->bpt.num_keys, key_low);
    for (;;) {
        BPTPage *node = &leaf->bpt;
        for (; idx < node->num_keys; idx++) {
            uint32_t key = node->leaf.keys[idx];
            if (key > key_high) {
                unpin(bpt, leaf);
                return DB_OK;
            }

            void *data;
            uint32_t size;
            rc = record_read(bpt->bm, node->leaf.rids[idx], &data, &size);
            if (rc != DB_OK) {
                unpin(bpt, leaf);
                return rc == DB_NOT_FOUND ? DB_ERR_CORRUPT : rc;
            }

            int stop = callback(key, data, size, ctx);
            free(data);
            if (stop) {
                unpin(bpt, leaf);
                return DB_OK;
            }
        }

        uint32_t next_page_id = node->leaf.next_page_id;
        unpin(bpt, leaf);
        if (next_page_id == NULL_PAGE_ID) return DB_OK;

        rc = pin_node(bpt, next_page_id, &leaf);
        if (rc != DB_OK) return rc;
        if (!leaf->bpt.is_leaf) {
            unpin(bpt, leaf);
            return DB_ERR_CORRUPT;
        }
        idx = 0;
    }
}

DBStatus bpt_height(BPTree *bpt, uint32_t *height) {
    *height = 0;
    uint32_t page_id = bpt->root_page_id;
    for (uint32_t h = 1; h <= BPT_MAX_HEIGHT; h++) {
        Page *page;
        DBStatus rc = pin_node(bpt, page_id, &page);
        if (rc != DB_OK) return rc;

        uint8_t is_leaf = page->bpt.is_leaf;
        if (!is_leaf) page_id = page->bpt.internal.children[0];
        unpin(bpt, page);

        if (is_leaf) {
            *height = h;
            return DB_OK;
        }
    }
    return DB_ERR_CORRUPT;
}

DBStatus bpt_empty(BPTree *bpt, uint8_t *empty) {
    Page *root;
    DBStatus rc = pin_node(bpt, bpt->root_page_id, &root);
    if (rc != DB_OK) return rc;
    *empty = root->bpt.is_leaf && root->bpt.num_keys == 0;
    unpin(bpt, root);
    return DB_OK;
}


// ---- Insertion ----

// Moves the upper half of an overflowing node into a new right sibling.
// The separator and the new page id have to be inserted into the parent.
static DBStatus split_node(BPTree *bpt, Page *page, uint32_t *separator,
        uint32_t *right_page_id) {
    Page *right_page;
    DBStatus rc = buffer_manager_new_page(bpt->bm, PAGE_TYPE_BPT, &right_page);
    if (rc != DB_OK) return rc;

    BPTPage *left = &page->bpt;
    BPTPage *right = &right_page->bpt;
    right->is_leaf = left->is_leaf;
    uint16_t split_idx = left->num_keys / 2;

    if (left->is_leaf) {
        // The first key of the right leaf is copied up as the separator
        uint16_t moved = left->num_keys - split_idx;
        memcpy(right->leaf.keys, &left->leaf.keys[split_idx], moved * sizeof(uint32_t));
        memcpy(right->leaf.rids, &left->leaf.rids[split_idx], moved * sizeof(RID));
        right->num_keys = moved;
        left->num_keys = split_idx;

        right->leaf.next_page_id = left->leaf.next_page_id;
        left->leaf.next_page_id = right_page->page_id;
        *separator = right->leaf.keys[0];
    }
    else {
        // The middle key is moved up as the separator, it is kept in
        // neither of the two nodes
        uint16_t moved = left->num_keys - split_idx - 1;
        *separator = left->internal.keys[split_idx];
        memcpy(right->internal.keys, &left->internal.keys[split_idx + 1],
            moved * sizeof(uint32_t));
        memcpy(right->internal.children, &left->internal.children[split_idx + 1],
            (moved + 1) * sizeof(uint32_t));
        right->num_keys = moved;
        left->num_keys = split_idx;
    }

    *right_page_id = right_page->page_id;
    mark_dirty(bpt, page);
    // New pages are already dirty
    unpin(bpt, right_page);
    return DB_OK;
}

// Moves the content of the root into a new page, the root becomes an
// internal node without keys that has the new page as its only child.
// This way the root page id never changes when the tree grows.
static DBStatus grow_root(BPTree *bpt, Page *root, Page **child) {
    DBStatus rc = buffer_manager_new_page(bpt->bm, PAGE_TYPE_BPT, child);
    if (rc != DB_OK) return rc;

    (*child)->bpt = root->bpt;
    root->bpt.is_leaf = BPT_INTERNAL;
    root->bpt.num_keys = 0;
    root->bpt.internal.children[0] = (*child)->page_id;
    mark_dirty(bpt, root);
    return DB_OK;
}

// page is pinned and has one key too many. Splits it and inserts the
// separator into the parent, and continues upwards as long as the parent
// overflows. Consumes the pin of page.
static DBStatus split_upwards(BPTree *bpt, Path *path, Page *page) {
    for (;;) {
        if (path->depth == 0) {
            // page is the root
            Page *child;
            DBStatus rc = grow_root(bpt, page, &child);
            unpin(bpt, page);
            if (rc != DB_OK) return rc;

            path->entries[0].page_id = bpt->root_page_id;
            path->entries[0].child_idx = 0;
            path->depth = 1;
            page = child;
        }

        uint32_t separator, right_page_id;
        DBStatus rc = split_node(bpt, page, &separator, &right_page_id);
        unpin(bpt, page);
        if (rc != DB_OK) return rc;

        path->depth--;
        PathEntry parent_entry = path->entries[path->depth];
        Page *parent;
        rc = pin_node(bpt, parent_entry.page_id, &parent);
        if (rc != DB_OK) return rc;

        internal_insert_at(&parent->bpt, parent_entry.child_idx, separator, right_page_id);
        mark_dirty(bpt, parent);
        if (parent->bpt.num_keys <= MAX_KEYS) {
            unpin(bpt, parent);
            return DB_OK;
        }
        page = parent;
    }
}

DBStatus bpt_insert(BPTree *bpt, uint32_t key, const void *data, uint32_t size) {
    Path path;
    Page *leaf;
    DBStatus rc = find_leaf(bpt, key, &path, &leaf);
    if (rc != DB_OK) return rc;

    BPTPage *node = &leaf->bpt;
    uint32_t idx = lower_bound(node->leaf.keys, node->num_keys, key);
    if (idx < node->num_keys && node->leaf.keys[idx] == key) {
        // Overwrite the old record
        RID rid = node->leaf.rids[idx];
        RID new_rid;
        rc = record_update(bpt->bm, rid, data, size, &new_rid);
        if (rc == DB_NOT_FOUND) rc = DB_ERR_CORRUPT;
        if (rc == DB_OK && !rid_equal(rid, new_rid)) {
            node->leaf.rids[idx] = new_rid;
            mark_dirty(bpt, leaf);
        }
        unpin(bpt, leaf);
        return rc;
    }

    RID rid;
    rc = record_insert(bpt->bm, data, size, &rid);
    if (rc != DB_OK) {
        unpin(bpt, leaf);
        return rc;
    }

    leaf_insert_at(node, idx, key, rid);
    mark_dirty(bpt, leaf);
    if (node->num_keys <= MAX_ENTRIES_LEAF) {
        unpin(bpt, leaf);
        return DB_OK;
    }

    return split_upwards(bpt, &path, leaf);
}


// ---- Deletion ----

// node is child idx of parent and left is child idx - 1. Moves the last
// entry of left into node.
static void borrow_from_left(BPTPage *node, BPTPage *left, BPTPage *parent,
        uint32_t idx) {
    if (node->is_leaf) {
        uint32_t last = left->num_keys - 1;
        leaf_insert_at(node, 0, left->leaf.keys[last], left->leaf.rids[last]);
        left->num_keys--;
        parent->internal.keys[idx - 1] = node->leaf.keys[0];
        return;
    }

    // Rotate through the parent: the separator moves down into node and
    // the last key of left moves up to become the new separator
    InternalPage *internal = &node->internal;
    memmove(&internal->keys[1], &internal->keys[0], node->num_keys * sizeof(uint32_t));
    memmove(&internal->children[1], &internal->children[0],
        (node->num_keys + 1) * sizeof(uint32_t));
    internal->keys[0] = parent->internal.keys[idx - 1];
    internal->children[0] = left->internal.children[left->num_keys];
    parent->internal.keys[idx - 1] = left->internal.keys[left->num_keys - 1];
    left->num_keys--;
    node->num_keys++;
}

// node is child idx of parent and right is child idx + 1. Moves the first
// entry of right into node.
static void borrow_from_right(BPTPage *node, BPTPage *right, BPTPage *parent,
        uint32_t idx) {
    if (node->is_leaf) {
        leaf_insert_at(node, node->num_keys, right->leaf.keys[0], right->leaf.rids[0]);
        leaf_remove_at(right, 0);
        parent->internal.keys[idx] = right->leaf.keys[0];
        return;
    }

    InternalPage *internal = &node->internal;
    InternalPage *right_internal = &right->internal;
    internal->keys[node->num_keys] = parent->internal.keys[idx];
    internal->children[node->num_keys + 1] = right_internal->children[0];
    parent->internal.keys[idx] = right_internal->keys[0];
    memmove(&right_internal->keys[0], &right_internal->keys[1],
        (right->num_keys - 1) * sizeof(uint32_t));
    memmove(&right_internal->children[0], &right_internal->children[1],
        right->num_keys * sizeof(uint32_t));
    right->num_keys--;
    node->num_keys++;
}

// Appends right to left. parent->keys[separator_idx] separates the two nodes,
// it is removed from the parent together with the pointer to right.
static void merge_nodes(BPTPage *left, BPTPage *right, BPTPage *parent,
        uint32_t separator_idx) {
    if (left->is_leaf) {
        memcpy(&left->leaf.keys[left->num_keys], right->leaf.keys,
            right->num_keys * sizeof(uint32_t));
        memcpy(&left->leaf.rids[left->num_keys], right->leaf.rids,
            right->num_keys * sizeof(RID));
        left->num_keys += right->num_keys;
        left->leaf.next_page_id = right->leaf.next_page_id;
    }
    else {
        // The separator moves down between the keys of the two nodes
        InternalPage *internal = &left->internal;
        internal->keys[left->num_keys] = parent->internal.keys[separator_idx];
        memcpy(&internal->keys[left->num_keys + 1], right->internal.keys,
            right->num_keys * sizeof(uint32_t));
        memcpy(&internal->children[left->num_keys + 1], right->internal.children,
            (right->num_keys + 1) * sizeof(uint32_t));
        left->num_keys += 1 + right->num_keys;
    }

    internal_remove_at(parent, separator_idx);
}

// The root has no keys left and a single child. The child is copied into
// the root (so that the root page id stays the same) and freed.
static DBStatus collapse_root(BPTree *bpt, Page *root) {
    Page *child;
    DBStatus rc = pin_node(bpt, root->bpt.internal.children[0], &child);
    if (rc != DB_OK) return rc;

    root->bpt = child->bpt;
    mark_dirty(bpt, root);
    rc = buffer_manager_free_page(bpt->bm, child);
    if (rc != DB_OK) unpin(bpt, child);
    return rc;
}

// page is pinned, it is not the root and has one key too few. Fixes it by
// borrowing from or merging with a sibling. If a merge leaves the parent
// with too few keys, the parent is returned pinned in underfull_parent so
// that it can be fixed next. Consumes the pin of page.
static DBStatus rebalance_step(BPTree *bpt, Path *path, Page *page,
        Page **underfull_parent) {
    *underfull_parent = NULL;
    path->depth--;
    PathEntry parent_entry = path->entries[path->depth];
    uint32_t idx = parent_entry.child_idx;
    Page *parent = NULL, *left = NULL, *right = NULL;

    DBStatus rc = pin_node(bpt, parent_entry.page_id, &parent);
    if (rc != DB_OK) goto out;

    BPTPage *p = &parent->bpt;
    if (p->is_leaf || idx > p->num_keys ||
            p->internal.children[idx] != page->page_id) {
        fprintf(stderr, "bptree: page %u is not the parent of page %u\n",
            parent->page_id, page->page_id);
        rc = DB_ERR_CORRUPT;
        goto out;
    }

    if (idx > 0) {
        rc = pin_node(bpt, p->internal.children[idx - 1], &left);
        if (rc != DB_OK) goto out;
        if (can_lend(&left->bpt)) {
            borrow_from_left(&page->bpt, &left->bpt, p, idx);
            mark_dirty(bpt, page);
            mark_dirty(bpt, left);
            mark_dirty(bpt, parent);
            goto out;
        }
    }

    if (idx < p->num_keys) {
        rc = pin_node(bpt, p->internal.children[idx + 1], &right);
        if (rc != DB_OK) goto out;
        if (can_lend(&right->bpt)) {
            borrow_from_right(&page->bpt, &right->bpt, p, idx);
            mark_dirty(bpt, page);
            mark_dirty(bpt, right);
            mark_dirty(bpt, parent);
            goto out;
        }
    }

    // No sibling can lend an entry, so merge with one of them. The merged
    // node fits as one of them has MIN entries and the other MIN - 1.
    if (left) {
        merge_nodes(&left->bpt, &page->bpt, p, idx - 1);
        mark_dirty(bpt, left);
        rc = buffer_manager_free_page(bpt->bm, page);
        if (rc != DB_OK) goto out;
        page = NULL;
    }
    else if (right) {
        merge_nodes(&page->bpt, &right->bpt, p, idx);
        mark_dirty(bpt, page);
        rc = buffer_manager_free_page(bpt->bm, right);
        if (rc != DB_OK) goto out;
        right = NULL;
    }
    else {
        fprintf(stderr, "bptree: page %u has no siblings\n", page->page_id);
        rc = DB_ERR_CORRUPT;
        goto out;
    }
    mark_dirty(bpt, parent);

    // The merged node may be the child that collapse_root frees, so it
    // must not be pinned anymore
    unpin(bpt, page);
    unpin(bpt, left);
    unpin(bpt, right);
    page = left = right = NULL;

    if (path->depth == 0) {
        // The parent is the root, it may have few keys but not zero
        if (p->num_keys == 0) rc = collapse_root(bpt, parent);
    }
    else if (is_underfull(p)) {
        *underfull_parent = parent;
        parent = NULL;
    }

out:
    unpin(bpt, page);
    unpin(bpt, left);
    unpin(bpt, right);
    unpin(bpt, parent);
    return rc;
}

DBStatus bpt_delete(BPTree *bpt, uint32_t key) {
    Path path;
    Page *leaf;
    DBStatus rc = find_leaf(bpt, key, &path, &leaf);
    if (rc != DB_OK) return rc;

    BPTPage *node = &leaf->bpt;
    uint32_t idx = lower_bound(node->leaf.keys, node->num_keys, key);
    if (idx == node->num_keys || node->leaf.keys[idx] != key) {
        unpin(bpt, leaf);
        return DB_NOT_FOUND;
    }

    rc = record_delete(bpt->bm, node->leaf.rids[idx]);
    if (rc != DB_OK) {
        unpin(bpt, leaf);
        return rc == DB_NOT_FOUND ? DB_ERR_CORRUPT : rc;
    }

    leaf_remove_at(node, idx);
    mark_dirty(bpt, leaf);

    // The separators in the parents do not have to be updated even if the
    // smallest key of the leaf was removed. They only have to be bounds:
    // every key in children[i] < keys[i] <= every key in children[i + 1]
    Page *page = leaf;
    if (path.depth == 0 || !is_underfull(node)) {
        unpin(bpt, page);
        return DB_OK;
    }

    while (page) {
        Page *underfull_parent;
        rc = rebalance_step(bpt, &path, page, &underfull_parent);
        if (rc != DB_OK) return rc;
        page = underfull_parent;
    }
    return DB_OK;
}


// ---- Destroy ----

static DBStatus destroy_node(BPTree *bpt, uint32_t page_id, uint32_t depth) {
    if (depth >= BPT_MAX_HEIGHT) return DB_ERR_CORRUPT;

    Page *page;
    DBStatus rc = pin_node(bpt, page_id, &page);
    if (rc != DB_OK) return rc;

    if (page->bpt.is_leaf) {
        for (uint32_t i = 0; i < page->bpt.num_keys; i++) {
            rc = record_delete(bpt->bm, page->bpt.leaf.rids[i]);
            if (rc != DB_OK) {
                unpin(bpt, page);
                return rc;
            }
        }
    }
    else {
        // Copy the children so that the page does not have to stay pinned
        // during the recursion
        uint32_t children[MAX_CHILDREN + 1];
        uint32_t child_count = page->bpt.num_keys + 1u;
        memcpy(children, page->bpt.internal.children, child_count * sizeof(uint32_t));
        unpin(bpt, page);

        for (uint32_t i = 0; i < child_count; i++) {
            rc = destroy_node(bpt, children[i], depth + 1);
            if (rc != DB_OK) return rc;
        }

        rc = pin_node(bpt, page_id, &page);
        if (rc != DB_OK) return rc;
    }

    rc = buffer_manager_free_page(bpt->bm, page);
    if (rc != DB_OK) unpin(bpt, page);
    return rc;
}

DBStatus bpt_destroy(BPTree *bpt) {
    DBStatus rc = destroy_node(bpt, bpt->root_page_id, 0);
    free(bpt);
    return rc;
}


// ---- Debugging ----

typedef struct VerifyState {
    uint8_t seen_leaf;
    uint32_t leaf_depth;     // All leaves have to be at the same depth
    uint32_t expected_leaf;  // next_page_id of the previous leaf
    BPTStats stats;
} VerifyState;

#define VERIFY_FAIL(...) do { \
        fprintf(stderr, "bpt_verify: page %u: ", page_id); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        return DB_ERR_CORRUPT; \
    } while (0)

// Checks a single node: key order, separator bounds [lower, upper) and fill
static DBStatus verify_node_content(uint32_t page_id, const BPTPage *node,
        uint8_t is_root, uint64_t lower, uint64_t upper) {
    if (node->num_keys > (node->is_leaf ? MAX_ENTRIES_LEAF : MAX_KEYS)) {
        VERIFY_FAIL("node has %u keys, max is %u", node->num_keys,
            node->is_leaf ? MAX_ENTRIES_LEAF : MAX_KEYS);
    }

    const uint32_t *keys = node->is_leaf ? node->leaf.keys : node->internal.keys;
    for (uint32_t i = 0; i < node->num_keys; i++) {
        if (keys[i] < lower || keys[i] >= upper) {
            VERIFY_FAIL("key %u is outside of [%llu, %llu)", keys[i],
                (unsigned long long)lower, (unsigned long long)upper);
        }
        if (i > 0 && keys[i] <= keys[i - 1]) {
            VERIFY_FAIL("keys are not sorted at index %u", i);
        }
    }

    if (node->is_leaf) {
        if (!is_root && node->num_keys < MIN_ENTRIES_LEAF) {
            VERIFY_FAIL("leaf has %u entries, min is %u",
                node->num_keys, MIN_ENTRIES_LEAF);
        }
        return DB_OK;
    }

    if (node->num_keys == 0) VERIFY_FAIL("internal node without keys");
    if (!is_root && node->num_keys + 1 < MIN_CHILDREN) {
        VERIFY_FAIL("internal node has %u children, min is %u",
            node->num_keys + 1, MIN_CHILDREN);
    }
    return DB_OK;
}

// Checks the subtree rooted at page_id, every key in it has to be in
// [lower, upper)
static DBStatus verify_node(BPTree *bpt, uint32_t page_id, uint32_t depth,
        uint64_t lower, uint64_t upper, VerifyState *state) {
    if (depth >= BPT_MAX_HEIGHT) VERIFY_FAIL("tree is too deep");

    Page *page;
    DBStatus rc = pin_node(bpt, page_id, &page);
    if (rc != DB_OK) return rc;

    // Copy what is needed so that the page does not have to stay pinned
    // while the children and records are read
    BPTPage *node = &page->bpt;
    uint8_t is_leaf = node->is_leaf;
    uint32_t count = node->num_keys;
    uint32_t next_page_id = NULL_PAGE_ID;
    RID rids[MAX_ENTRIES_LEAF];
    uint32_t keys[MAX_KEYS];
    uint32_t children[MAX_CHILDREN];

    rc = verify_node_content(page_id, node, depth == 0, lower, upper);
    if (rc == DB_OK && is_leaf) {
        next_page_id = node->leaf.next_page_id;
        memcpy(rids, node->leaf.rids, count * sizeof(RID));
    }
    else if (rc == DB_OK) {
        memcpy(keys, node->internal.keys, count * sizeof(uint32_t));
        memcpy(children, node->internal.children, (count + 1) * sizeof(uint32_t));
    }
    unpin(bpt, page);
    if (rc != DB_OK) return rc;

    if (is_leaf) {
        if (state->seen_leaf) {
            if (depth != state->leaf_depth) {
                VERIFY_FAIL("leaf at depth %u, expected %u", depth, state->leaf_depth);
            }
            if (page_id != state->expected_leaf) {
                VERIFY_FAIL("previous leaf points to %u", state->expected_leaf);
            }
        }
        state->seen_leaf = 1;
        state->leaf_depth = depth;
        state->expected_leaf = next_page_id;
        state->stats.leaf_count++;
        state->stats.key_count += count;

        for (uint32_t i = 0; i < count; i++) {
            void *data;
            uint32_t size;
            rc = record_read(bpt->bm, rids[i], &data, &size);
            if (rc != DB_OK) {
                VERIFY_FAIL("record (%u, %u) of entry %u cannot be read: %s",
                    rids[i].page_id, rids[i].slot_id, i, db_status_str(rc));
            }
            free(data);
        }
        return DB_OK;
    }

    state->stats.internal_count++;
    for (uint32_t i = 0; i <= count; i++) {
        uint64_t child_lower = i == 0 ? lower : keys[i - 1];
        uint64_t child_upper = i == count ? upper : keys[i];
        rc = verify_node(bpt, children[i], depth + 1, child_lower, child_upper, state);
        if (rc != DB_OK) return rc;
    }
    return DB_OK;
}

DBStatus bpt_verify(BPTree *bpt, BPTStats *stats) {
    VerifyState state;
    memset(&state, 0, sizeof(state));

    uint32_t page_id = bpt->root_page_id;
    DBStatus rc = verify_node(bpt, page_id, 0, 0, (uint64_t)UINT32_MAX + 1, &state);
    if (rc != DB_OK) return rc;

    if (state.expected_leaf != NULL_PAGE_ID) {
        VERIFY_FAIL("last leaf points to page %u", state.expected_leaf);
    }

    state.stats.height = state.leaf_depth + 1;
    if (stats) *stats = state.stats;
    return DB_OK;
}

static DBStatus print_node(BPTree *bpt, uint32_t page_id, uint32_t depth, FILE *out) {
    if (depth >= BPT_MAX_HEIGHT) return DB_ERR_CORRUPT;

    Page *page;
    DBStatus rc = pin_node(bpt, page_id, &page);
    if (rc != DB_OK) return rc;

    BPTPage *node = &page->bpt;
    fprintf(out, "%*s%s %u:", (int)(2 * depth), "",
        node->is_leaf ? "leaf" : "internal", page_id);
    const uint32_t *keys = node->is_leaf ? node->leaf.keys : node->internal.keys;
    for (uint32_t i = 0; i < node->num_keys; i++) fprintf(out, " %u", keys[i]);
    if (node->is_leaf) fprintf(out, " -> %u", node->leaf.next_page_id);
    fprintf(out, "\n");

    uint32_t child_count = node->is_leaf ? 0 : node->num_keys + 1u;
    uint32_t children[MAX_CHILDREN + 1];
    memcpy(children, node->internal.children, child_count * sizeof(uint32_t));
    unpin(bpt, page);

    for (uint32_t i = 0; i < child_count; i++) {
        rc = print_node(bpt, children[i], depth + 1, out);
        if (rc != DB_OK) return rc;
    }
    return DB_OK;
}

DBStatus bpt_print(BPTree *bpt, FILE *out) {
    return print_node(bpt, bpt->root_page_id, 0, out);
}
