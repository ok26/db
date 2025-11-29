#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Debug
#include <assert.h>

#include "bptree.h"
#include "util.h"

// Remove comment for full debug
// #define FULL_DEBUG

// Does not have ownership of buffer-manager
struct BPTree {
    // Very important change this for page_id as it will just be flushed otherwise
    Page *root;
    Stack *parent_stack;
    BufferManager *bm;
};

// Declarations
void internal_insert(BPTree *bpt, uint32_t key, uint32_t page_id,
    Page *page, Stack *parent_stack);


BPTree *bpt_read(BufferManager *bm, uint32_t root_page_id) {
    BPTree *bpt = malloc(sizeof(BPTree));
    bpt->root = buffer_manager_get_page(bm, root_page_id);
    bpt->bm = bm;
    bpt->parent_stack = stack_init();
    return bpt;
}

BPTree *bpt_new(BufferManager *bm) {
    BPTree *bpt = malloc(sizeof(BPTree));
    bpt->root = buffer_manager_new_bpt_page(bm, LEAF);

    // TODO: Update the root bpt when we create a new bpt

    bpt->parent_stack = stack_init();
    bpt->bm = bm;
    return bpt;
}

void bpt_free(BPTree *bpt) {
    stack_free(bpt->parent_stack);
    free(bpt);
}

Page *search(BPTree *bpt, uint32_t key) {
    Page *page = bpt->root;
    stack_clear(bpt->parent_stack);
    while (!page->header.is_leaf) {
        stack_push(bpt->parent_stack, page->header.page_id);
        InternalPage *internal_page = page->internal;
        uint32_t idx = upper_bound(internal_page->keys, page->header.num_keys, key);
        page = buffer_manager_get_page(bpt->bm, internal_page->children[idx]);
    }
    return page;
}

uint32_t bpt_height(BPTree *bpt) {
    Page *page = bpt->root;
    if (!page) return 0;
    uint32_t h = 1;
    while (!page->header.is_leaf) {
        h++;
        page = buffer_manager_get_page(bpt->bm, page->internal->children[0]);
    }
    return h;
}

void promote_key(BPTree *bpt, uint32_t key, uint32_t left_id, 
    uint32_t right_id, Stack *parent_stack) {

    if (stack_is_empty(parent_stack)) {
        // Split root
        Page *new_root = buffer_manager_new_bpt_page(bpt->bm, INTERNAL);
        new_root->header.num_keys = 1;
        new_root->internal->keys[0] = key;
        new_root->internal->children[0] = left_id;
        new_root->internal->children[1] = right_id;
        bpt->root = new_root;
    }
    else {
        // Insert into parent
        Page *parent = buffer_manager_get_page(bpt->bm, stack_top(parent_stack));
        stack_pop(parent_stack);
        internal_insert(bpt, key, right_id, parent, parent_stack);
    }
}

void internal_insert(BPTree *bpt, uint32_t key, uint32_t page_id,
    Page *page, Stack *parent_stack) {

    // Insert into node
    for (uint32_t i = 0; i <= page->header.num_keys; i++) {
        
        if (i == page->header.num_keys) {
            page->internal->keys[page->header.num_keys] = key;
            page->internal->children[page->header.num_keys + 1] = page_id;
            break;
        }

        if (page->internal->keys[i] > key) {
            memmove(&page->internal->keys[i + 1], &page->internal->keys[i],
                (page->header.num_keys - i) * sizeof(uint32_t));
            memmove(&page->internal->children[i + 2], &page->internal->children[i + 1],
                (page->header.num_keys - i) * sizeof(uint32_t));
            page->internal->keys[i] = key;
            page->internal->children[i + 1] = page_id;
            break;
        }
    }

    page->header.num_keys++;

    if (page->header.num_keys <= MAX_KEYS) {
        return;
    }

    // Split Node
    uint32_t split_idx = page->header.num_keys / 2;
    uint32_t promoted_key = page->internal->keys[split_idx];
    Page *right_page = buffer_manager_new_bpt_page(bpt->bm, INTERNAL);    

    memcpy(&right_page->internal->keys[0], &page->internal->keys[split_idx + 1],
        (page->header.num_keys - split_idx - 1) * sizeof(uint32_t));
    memcpy(&right_page->internal->children[0], &page->internal->children[split_idx + 1],
        (page->header.num_keys - split_idx) * sizeof(uint32_t));

    right_page->header.num_keys = page->header.num_keys - split_idx;
    page->header.num_keys = split_idx;
    
    // Ignore top key, new leftmost-key splits two values
    right_page->header.num_keys--;

    promote_key(bpt, promoted_key, page->header.page_id,
        right_page->header.page_id, parent_stack);
}

void leaf_insert(BPTree *bpt, uint32_t key, void *data,
    size_t size, Page *page, Stack *parent_stack) {

    RID rid = buffer_manager_request_slot(bpt->bm, size, data);
    for (uint32_t i = 0; i <= page->header.num_keys; i++) {

        if (i == page->header.num_keys) {
            page->leaf->keys[page->header.num_keys] = key;
            page->leaf->page_ids[page->header.num_keys] = rid.page_id;
            page->leaf->slot_ids[page->header.num_keys] = rid.slot_id;
            break;
        }

        if (page->leaf->keys[i] > key) {
            memmove(&page->leaf->keys[i + 1], &page->leaf->keys[i],
                (page->header.num_keys - i) * sizeof(uint32_t));
            memmove(&page->leaf->page_ids[i + 1], &page->leaf->page_ids[i], 
                (page->header.num_keys - i) * sizeof(uint32_t));
            memmove(&page->leaf->slot_ids[i + 1], &page->leaf->slot_ids[i], 
                (page->header.num_keys - i) * sizeof(uint16_t));
            page->leaf->keys[i] = key;
            page->leaf->page_ids[i] = rid.page_id;
            page->leaf->slot_ids[i] = rid.slot_id;
            break;
        }
    }

    page->header.num_keys++;

    if (page->header.num_keys <= MAX_ENTRIES_LEAF) {
        return;
    }

    // Else, split node
    uint32_t split_idx = page->header.num_keys / 2;
    uint32_t promoted_key = page->leaf->keys[split_idx];
    Page *right_leaf = buffer_manager_new_bpt_page(bpt->bm, LEAF);

    memcpy(&right_leaf->leaf->keys[0], &page->leaf->keys[split_idx], 
        (page->header.num_keys - split_idx) * sizeof(uint32_t));
    memcpy(&right_leaf->leaf->page_ids[0], &page->leaf->page_ids[split_idx],
        (page->header.num_keys - split_idx) * sizeof(uint32_t));
    memcpy(&right_leaf->leaf->slot_ids[0], &page->leaf->slot_ids[split_idx],
        (page->header.num_keys - split_idx) * sizeof(uint16_t));

    right_leaf->header.num_keys = page->header.num_keys - split_idx;
    page->header.num_keys = split_idx;
    right_leaf->leaf->next_page_id = page->leaf->next_page_id;
    page->leaf->next_page_id = right_leaf->header.page_id;

    promote_key(bpt, promoted_key, page->header.page_id, 
        right_leaf->header.page_id, parent_stack);
}

void bpt_insert(BPTree *bpt, uint32_t key, void *data, size_t size) {
    Page *leaf = search(bpt, key);
    uint32_t keyidx = lower_bound(leaf->leaf->keys, leaf->header.num_keys, key);
    if (keyidx != leaf->header.num_keys && leaf->leaf->keys[keyidx] == key) {
        // Overwrite old value
        RID rid = buffer_manager_request_slot(bpt->bm, size, data);
        leaf->leaf->page_ids[keyidx] = rid.page_id;
        leaf->leaf->slot_ids[keyidx] = rid.slot_id;
        return;
    }

    leaf_insert(bpt, key, data, size, leaf, bpt->parent_stack);
}

// Currently returs nothing as a placeholder
void *bpt_get(BPTree *bpt, uint32_t key) {
    Page *leaf = search(bpt, key);
    uint32_t idx = lower_bound(leaf->leaf->keys, leaf->header.num_keys, key);
    if (idx != leaf->header.num_keys && leaf->leaf->keys[idx] == key) {
        RID rid = {
            leaf->leaf->page_ids[idx],
            leaf->leaf->slot_ids[idx]
        };
        return buffer_manager_get_data(bpt->bm, rid);
    }
    else {
        return NULL;
    }
}

void bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high, 
    void (*callback)(uint32_t key, void *data)) {
    
    if (key_high < key_low) {
        return;
    }

    Page *leaf = search(bpt, key_low);

    while (leaf) {
        for (uint32_t i = 0; i < leaf->header.num_keys; i++) {
            if (leaf->leaf->keys[i] > key_high) return;
        
            if (leaf->leaf->keys[i] >= key_low) {
                RID rid = {
                    leaf->leaf->page_ids[i],
                    leaf->leaf->slot_ids[i]
                };
                void *data = buffer_manager_get_data(bpt->bm, rid);
                callback(leaf->leaf->keys[i], data);
            }
        }
        leaf = buffer_manager_get_page(bpt->bm, leaf->leaf->next_page_id);
    }
}

// Helper to bpt_delete
void bpt_update_separators(BPTree *bpt, uint32_t new_separator, 
        uint32_t old_separator) {
    
    uint32_t keyidx = 0;
    while (keyidx == 0 && !stack_is_empty(bpt->parent_stack)) {
        Page *parent = buffer_manager_get_page(bpt->bm,
            stack_top(bpt->parent_stack));
        stack_pop(bpt->parent_stack);
        
        // If child is leftmost child then finished
        if (old_separator < parent->internal->keys[0]) {
            return;
        }

        uint32_t pidx = lower_bound(parent->internal->keys,
            parent->header.num_keys, old_separator);
        assert(parent->internal->keys[pidx] == old_separator);
        parent->internal->keys[pidx] = new_separator;
        keyidx = pidx;
    }
}

// Part of bpt_delete
void bpt_remove_internal_separator(BPTree *bpt, uint32_t separator, 
    Page *node) {
    
    InternalPage *internal = node->internal;
    PageHeader *header = &node->header;
    uint32_t keyidx = lower_bound(internal->keys, header->num_keys, separator);
    if (keyidx == header->num_keys || internal->keys[keyidx] != separator) {
        // Should be unreachable
        printf("DEBUG: unreachable cannot find separator\n");
        return;
    }

    memmove(&internal->keys[keyidx], &internal->keys[keyidx + 1], 
        (header->num_keys - (keyidx + 1)) * sizeof(uint32_t));
    memmove(&internal->children[keyidx + 1], &internal->children[keyidx + 2],
        (header->num_keys - (keyidx + 1)) * sizeof(uint32_t));
    header->num_keys--;

    // Update separators if separator was removed
    if (keyidx == 0) {
        // In a valid B+Tree, a non-root internal node must never become empty.
        // If this invariant is violated it indicates a bug elsewhere. Fail fast.
        assert(header->num_keys > 0 || stack_is_empty(bpt->parent_stack));
        if (!stack_is_empty(bpt->parent_stack)) {
            Stack *temp = stack_clone(bpt->parent_stack);
            bpt_update_separators(bpt, internal->keys[0], separator);
            stack_free(bpt->parent_stack);
            bpt->parent_stack = temp;
        }
    }

    // Every interal node has (num_keys + 1) children
    if (header->num_keys + 1 >= MIN_CHILDREN) {
        return;
    }

    if (stack_is_empty(bpt->parent_stack)) {
        if (header->num_keys == 0) {
            // Decrease height
            Page *new_root = buffer_manager_get_page(bpt->bm, 
                internal->children[0]);
            buffer_manager_free_page(bpt->bm, node->header.page_id);
            bpt->root = new_root;
        }
        return;
    }

    // Borrow
    Page *parent = buffer_manager_get_page(bpt->bm, 
        stack_top(bpt->parent_stack));

    // Find the child index for this node using upper_bound (same logic as search)
    uint32_t child_idx = lower_bound(parent->internal->keys,
        parent->header.num_keys, internal->keys[0]);
    Page *left_sibling = NULL;
    if (parent->internal->keys[child_idx] == internal->keys[0]) {
        uint32_t left_sibling_page_id = parent->internal->children[child_idx];
        left_sibling = buffer_manager_get_page(bpt->bm, left_sibling_page_id);
    }

    if (left_sibling && left_sibling->header.num_keys + 1 > MIN_CHILDREN) {
        // printf("Borrowing from left sibling internal\n");
        uint32_t borrow_idx_child = left_sibling->header.num_keys - 1;
        uint32_t borrowed_child_page_id = 
            left_sibling->internal->children[borrow_idx_child + 1];
        left_sibling->header.num_keys--;
        
        // New separator-key is leftmost key of leftmost child of "node" which was
        // "ignored before when being leftmost"
        Page *left_most_child = buffer_manager_get_page(bpt->bm, internal->children[0]);
        uint32_t new_key;
        if (left_most_child->header.is_leaf)
            new_key = left_most_child->leaf->keys[0];
        else 
            new_key = left_most_child->internal->keys[0];

        memmove(&internal->keys[1], &internal->keys[0],
            header->num_keys * sizeof(uint32_t));
        memmove(&internal->children[1], &internal->children[0],
            (header->num_keys + 1) * sizeof(uint32_t));
        internal->keys[0] = new_key;
        internal->children[0] = borrowed_child_page_id;
        header->num_keys++;
        bpt_update_separators(bpt, internal->keys[0], internal->keys[1]);
        return;
    }

    uint32_t right_sibling_idx;
    Page *right_sibling = NULL;
    // child_idx is the index of the current node in parent's children
    right_sibling_idx = child_idx + 1;

    if (right_sibling_idx < parent->header.num_keys + 1) {
        uint32_t right_sibling_page_id = parent->internal->children[right_sibling_idx];
        right_sibling = buffer_manager_get_page(bpt->bm, right_sibling_page_id);
    }

    if (right_sibling && right_sibling->header.num_keys + 1 > MIN_CHILDREN) {
        // printf("Borrowing from right sibling internal\n");
        uint32_t borrow_idx_child = 0;
        uint32_t borrowed_child_page_id = 
            right_sibling->internal->children[borrow_idx_child];
        
        InternalPage *right_internal = right_sibling->internal;
        uint32_t old_separator = right_internal->keys[0];
        memmove(&right_internal->keys[0], &right_internal->keys[1],
            (right_sibling->header.num_keys - 1) * sizeof(uint32_t));
        memmove(&right_internal->children[0], &right_internal->children[1],
            right_sibling->header.num_keys * sizeof(uint32_t));
        right_sibling->header.num_keys--;
        bpt_update_separators(bpt, right_internal->keys[0], old_separator);

        // New key is leftmost key of borrowed child
        Page *borrowed_child = 
            buffer_manager_get_page(bpt->bm, borrowed_child_page_id);
        uint32_t new_key;
        if (borrowed_child->header.is_leaf)
            new_key = borrowed_child->leaf->keys[0];
        else
            new_key = borrowed_child->internal->keys[0];
        
        internal->keys[header->num_keys] = new_key;
        internal->children[header->num_keys + 1] = borrowed_child_page_id;
        header->num_keys++;
        return;
    }

    // Merge
    if (left_sibling) {
        // printf("Merging with left sibling internal\n");
        uint32_t removed_separator = internal->keys[0];
        InternalPage *left_internal = left_sibling->internal;
        memcpy(&left_internal->keys[left_sibling->header.num_keys],
            &internal->keys[0], node->header.num_keys * sizeof(uint32_t));
        memcpy(&left_internal->children[left_sibling->header.num_keys + 1],
            &internal->children[0], (node->header.num_keys + 1) * sizeof(uint32_t));
        left_sibling->header.num_keys += node->header.num_keys;
        buffer_manager_free_page(bpt->bm, node->header.page_id);

        // Recursively remove separator from parent
        Page *parent = buffer_manager_get_page(bpt->bm, stack_top(bpt->parent_stack));
        stack_pop(bpt->parent_stack);
        bpt_remove_internal_separator(bpt, removed_separator, parent);
        return;
    }

    if (right_sibling) {
        // printf("Merging with right sibling internal\n");
        uint32_t removed_separator = right_sibling->internal->keys[0];
        InternalPage *right_internal = right_sibling->internal;
        memcpy(&internal->keys[node->header.num_keys], &right_internal->keys[0],
            right_sibling->header.num_keys * sizeof(uint32_t));
        memcpy(&internal->children[node->header.num_keys + 1], &right_internal->children[0],
            (right_sibling->header.num_keys + 1) * sizeof(uint32_t));
        node->header.num_keys += right_sibling->header.num_keys;
        buffer_manager_free_page(bpt->bm, right_sibling->header.page_id);

        // Recursively remove separator from parent
        Page *parent = buffer_manager_get_page(bpt->bm, stack_top(bpt->parent_stack));
        stack_pop(bpt->parent_stack);
        bpt_remove_internal_separator(bpt, removed_separator, parent);
        return;
    }

    // Unreachable as of the same reason as in bpt_delete
    printf("DEBUG: Should never reach");
}

void bpt_delete(BPTree *bpt, uint32_t key) {
    Page *page = search(bpt, key);
    if (!page) return;
    LeafPage *leaf = page->leaf;
    uint32_t keyidx = lower_bound(leaf->keys, page->header.num_keys, key);
    if (keyidx == page->header.num_keys || leaf->keys[keyidx] != key) {
        // Value does not exist, skip
        return;
    }

    RID rid = {
        leaf->page_ids[keyidx],
        leaf->slot_ids[keyidx]
    };
    buffer_manager_free_data(bpt->bm, rid);
    memmove(&leaf->keys[keyidx], &leaf->keys[keyidx + 1],
        (page->header.num_keys - (keyidx + 1)) * sizeof(uint32_t));
    memmove(&leaf->page_ids[keyidx], &leaf->page_ids[keyidx + 1],
        (page->header.num_keys - (keyidx + 1)) * sizeof(uint32_t));
    memmove(&leaf->slot_ids[keyidx], &leaf->slot_ids[keyidx + 1],
        (page->header.num_keys - (keyidx + 1)) * sizeof(uint16_t));
    page->header.num_keys -= 1;
    
    // Update separators if separator was removed
    if (keyidx == 0) {
        // In a valid B+Tree, a non-root leaf must not become empty. If it does,
        // that indicates a bug elsewhere; fail fast so it can be diagnosed.
        assert(page->header.num_keys > 0 || stack_is_empty(bpt->parent_stack));
        if (!stack_is_empty(bpt->parent_stack)) {
            Stack *temp = stack_clone(bpt->parent_stack);
            bpt_update_separators(bpt, leaf->keys[0], key);
            stack_free(bpt->parent_stack);
            bpt->parent_stack = temp;
        }
    }

    // If leaf is root the boundary does not have to hold
    if (page->header.num_keys >= MIN_ENTRIES_LEAF || 
            stack_is_empty(bpt->parent_stack)) {
        return;
    }

    // Otherwise try borrow (first with left sibling)
    Page *parent = buffer_manager_get_page(bpt->bm, 
        stack_top(bpt->parent_stack));
    
    // Determine the child index of this leaf, then pick the immediate left sibling
    uint32_t child_idx = lower_bound(parent->internal->keys,
        parent->header.num_keys, leaf->keys[0]);
    Page *left_sibling = NULL;
    if (parent->internal->keys[child_idx] == leaf->keys[0]) {
        uint32_t left_sibling_page_id = parent->internal->children[child_idx];
        left_sibling = buffer_manager_get_page(bpt->bm, left_sibling_page_id);
        assert(left_sibling->leaf->next_page_id == page->header.page_id);
    }
    
    if (left_sibling && left_sibling->header.num_keys > MIN_ENTRIES_LEAF) {
        // printf("Borrowing from left sibling\n");
        uint32_t borrow_idx = left_sibling->header.num_keys - 1;
        uint32_t borrowed_key = left_sibling->leaf->keys[borrow_idx];
        uint32_t borrowed_page_id = left_sibling->leaf->page_ids[borrow_idx];
        uint32_t borrowed_slot_id = left_sibling->leaf->slot_ids[borrow_idx];
        left_sibling->header.num_keys--;

        memmove(&leaf->keys[1], &leaf->keys[0],
            page->header.num_keys * sizeof(uint32_t));
        memmove(&leaf->page_ids[1], &leaf->page_ids[0], 
            page->header.num_keys * sizeof(uint32_t));
        memmove(&leaf->slot_ids[1], &leaf->slot_ids[0], 
            page->header.num_keys * sizeof(uint16_t));
        leaf->keys[0] = borrowed_key;
        leaf->page_ids[0] = borrowed_page_id;
        leaf->slot_ids[0] = borrowed_slot_id;
        page->header.num_keys++;

        bpt_update_separators(bpt, leaf->keys[0], leaf->keys[1]);
        return;
    }

    // Otherwise the right sibling
    Page *right_sibling = NULL;
    // Make sure next leaf has the same parent (has to be a sibling i think)
    if (parent->internal->keys[parent->header.num_keys - 1] != leaf->keys[0]) {
        right_sibling = buffer_manager_get_page(bpt->bm, leaf->next_page_id);
        if (right_sibling && right_sibling->header.num_keys > MIN_ENTRIES_LEAF) {
            // printf("Borrowing from right sibling\n");
            uint32_t borrow_idx = 0;
            uint32_t borrowed_key = right_sibling->leaf->keys[borrow_idx];
            uint32_t borrowed_page_id = right_sibling->leaf->page_ids[borrow_idx];
            uint32_t borrowed_slot_id = right_sibling->leaf->slot_ids[borrow_idx];
            memmove(&right_sibling->leaf->keys[0], &right_sibling->leaf->keys[1],
                (right_sibling->header.num_keys - 1) * sizeof(uint32_t));
            memmove(&right_sibling->leaf->page_ids[0], &right_sibling->leaf->page_ids[1],
                (right_sibling->header.num_keys - 1) * sizeof(uint32_t));
            memmove(&right_sibling->leaf->slot_ids[0], &right_sibling->leaf->slot_ids[1],
                (right_sibling->header.num_keys - 1) * sizeof(uint16_t));
            right_sibling->header.num_keys--;

            leaf->keys[page->header.num_keys] = borrowed_key;
            leaf->page_ids[page->header.num_keys] = borrowed_page_id;
            leaf->slot_ids[page->header.num_keys] = borrowed_slot_id;
            page->header.num_keys++;

            bpt_update_separators(bpt, right_sibling->leaf->keys[0], borrowed_key);
            return;
        }
    }

    // If nothing else works, merge with sibling (try left first)
    if (left_sibling) {
        // printf("Merging with left sibling\n");
        uint32_t removed_separator = leaf->keys[0];
        LeafPage *left_leaf = left_sibling->leaf;
        memcpy(&left_leaf->keys[left_sibling->header.num_keys],
            &leaf->keys[0], page->header.num_keys * sizeof(uint32_t));
        memcpy(&left_leaf->page_ids[left_sibling->header.num_keys],
            &leaf->page_ids[0], page->header.num_keys * sizeof(uint32_t));
        memcpy(&left_leaf->slot_ids[left_sibling->header.num_keys],
            &leaf->slot_ids[0], page->header.num_keys * sizeof(uint16_t));
        left_sibling->header.num_keys += page->header.num_keys;
        left_leaf->next_page_id = leaf->next_page_id;
        buffer_manager_free_page(bpt->bm, page->header.page_id);

        // Remove separator from parent
        Page *parent = buffer_manager_get_page(bpt->bm, stack_top(bpt->parent_stack));
        stack_pop(bpt->parent_stack);
        bpt_remove_internal_separator(bpt, removed_separator, parent);
        return;
    }
    
    // Else right sibling
    if (right_sibling) {
        // printf("Merging with right sibling\n");
        uint32_t removed_separator = right_sibling->leaf->keys[0];
        LeafPage *right_leaf = right_sibling->leaf;
        memcpy(&leaf->keys[page->header.num_keys], &right_leaf->keys[0], 
            right_sibling->header.num_keys * sizeof(uint32_t));
        memcpy(&leaf->page_ids[page->header.num_keys], &right_leaf->page_ids[0], 
            right_sibling->header.num_keys * sizeof(uint32_t));
        memcpy(&leaf->slot_ids[page->header.num_keys], &right_leaf->slot_ids[0], 
            right_sibling->header.num_keys * sizeof(uint16_t));
        page->header.num_keys += right_sibling->header.num_keys;
        leaf->next_page_id = right_leaf->next_page_id;
        buffer_manager_free_page(bpt->bm, right_sibling->header.page_id);

        // Remove separator from parent
        Page *parent = buffer_manager_get_page(bpt->bm, stack_top(bpt->parent_stack));
        stack_pop(bpt->parent_stack);
        bpt_remove_internal_separator(bpt, removed_separator, parent);
        return;
    }
    
    // Unreachable as the leaf has to have atleast one sibling 
    // if the tree is valid (Follows as every internal node apart
    // from the root has to have atleast MIN_CHILDREN children and
    // if the root ever has only one child, that child becomes the new root)
    printf("DEBUG: Should never reach");
    return;
}

int bpt_empty(BPTree *bpt) {
    return !bpt->root || bpt->root->header.num_keys == 0;
}

// WARNING, DO NOT USE unless familiar with tree
// Reads the entirety of the data linked to the root
int bpt_verify_recursively(BPTree *bpt, Page *node, int lower, int upper) {
    int root = lower == 0 && upper == INT32_MAX;

    if (node->header.is_leaf) {
        // First of all assert that the seperator is correct
        if (lower != 0) assert(node->leaf->keys[0] == lower);
        for (int i = 1; i < node->header.num_keys; i++) {
            assert(node->leaf->keys[i] > node->leaf->keys[i - 1]);
            assert(node->leaf->keys[i] >= lower && node->leaf->keys[i] < upper);
        }

        if (!root) {
            assert(node->header.num_keys >= MIN_ENTRIES_LEAF);
            assert(node->header.num_keys <= MAX_ENTRIES_LEAF);
        }

        // Return height of subtree
        return 1;
    }
    else {
        // First of all assert seperator is correct
        if (lower != 0) assert(node->internal->keys[0] == lower);
        Page *left_child = buffer_manager_get_page(bpt->bm, node->internal->children[0]);

        // Assert order
        assert(node->internal->keys[0] >= lower);

        // Assert leftmost child
        int sub_tree_height = bpt_verify_recursively(bpt, left_child, lower, 
            node->internal->keys[0]);
        
        // For checking next_page_id
        Page *prev_child = left_child;

        for (int i = 0; i < node->header.num_keys; i++) {

            // Assert order
            if (i != 0) assert(node->internal->keys[i] > node->internal->keys[i - 1]);

            int child_lower = node->internal->keys[i];
            int child_upper = i == node->header.num_keys - 1 ? 
                upper : node->internal->keys[i + 1];
            Page *child = buffer_manager_get_page(bpt->bm, node->internal->children[i + 1]);

            // Assert child
            int child_tree_height = bpt_verify_recursively(bpt, 
                child, child_lower, child_upper);
            
            // Assert sub_tree_heights
            assert(child_tree_height == sub_tree_height);

            // Assert linked list
            if (child->header.is_leaf) {
                assert(prev_child->leaf->next_page_id == child->header.page_id);
                prev_child = child;
            }
        }

        if (!root) {
            assert(node->header.num_keys + 1 >= MIN_CHILDREN);
            assert(node->header.num_keys + 1 <= MAX_CHILDREN);
        }
    }
}

void bpt_verify_tree(BPTree *bpt) {
    bpt_verify_recursively(bpt, bpt->root, 0, INT32_MAX);
}

/*
// Warning, reads entire tree
void bpt_print(BPTree *bpt) {
    if (!bpt->root) return;

    Page *queue[1024];
    uint32_t front = 0, back = 0;

    uint32_t nodes_in_level = 1;
    uint32_t nodes_next_level = 0;

    queue[back++] = bpt->root;

    uint32_t level = 0;
    printf("L0 ");
    while (front < back) {
        Page *node = queue[front++];
        nodes_in_level--;

        // Print node keys
        printf("[");
#ifdef FULL_DEBUG
        printf("%p | ", node->pointers[0]);
#endif
        for (uint32_t i = 0; i < node->header.num_keys; i++) {
#ifdef FULL_DEBUG
            printf("%u-%u-%u-", node->keys[i], node->is_leaf, node->num_keys);
            // printf("%p", node->pointers[i + 1]);
            printf("%p", node->parent);
#else
            printf("%u", node->keys[i]);
#endif
            if (i < node->num_keys - 1) printf(" | ");
        }
        printf("] ");

        // Enqueue children if internal
        if (!node->is_leaf) {
            for (uint32_t i = 0; i <= node->num_keys; i++) {
                queue[back++] = buffer_manager_get_page(bpt->bm, node->pointers[i]);
                nodes_next_level++;
            }
        }

        // End of a level
        if (nodes_in_level == 0) {
            printf("\n");
            nodes_in_level = nodes_next_level;
            nodes_next_level = 0;
            level++;
            if (nodes_in_level != 0) {
                printf("L%d ", level);
            }
        }
    }
}
*/