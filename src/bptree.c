#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bptree.h"
#include "util.h"

// Remove comment for full debug
// #define FULL_DEBUG

// Does not have ownership of buffer-manager
struct BPTree {
    Page *root;
    Stack *parent_stack;
    BufferManager *bm;
};

BPTree *bpt_read(BufferManager *bm, uint32_t root_page_id) {
    BPTree *bpt = malloc(sizeof(BPTree));
    bpt->root = buffer_manager_get_page(bm, root_page_id);
    bpt->parent_stack = stack_init();
    return bpt;
}

BPTree *bpt_new(BufferManager *bm) {
    BPTree *bpt = malloc(sizeof(BPTree));
    bpt->root = buffer_manager_new_page(bm, LEAF);

    // TODO: Update the root bpt when we create a new bpt

    bpt->parent_stack = stack_init();
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
    uint32_t h = 0;
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
        Page *new_root = buffer_manager_new_page(bpt->bm, INTERNAL);
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
    Page *right_page = buffer_manager_new_page(bpt->bm, INTERNAL);    

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

    for (uint32_t i = 0; i <= page->header.num_keys; i++) {

        if (i == page->header.num_keys) {
            uint64_t rid = buffer_manager_request_slot(size);
            page->leaf->keys[page->header.num_keys] = key;
            page->leaf->rids[page->header.num_keys] = rid;
            break;
        }

        if (page->leaf->keys[i] > key) {
            uint64_t rid = buffer_manager_request_slot(size);
            memmove(&page->leaf->keys[i + 1], &page->leaf->keys[i],
                (page->header.num_keys - i) * sizeof(uint32_t));
            memmove(&page->leaf->rids[i + 1], &page->leaf->rids[i], 
                (page->header.num_keys - i) * sizeof(uint64_t));
            page->leaf->keys[i] = key;
            page->leaf->rids[i] = rid;
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
    Page *right_leaf = buffer_manager_new_page(bpt->bm, LEAF);

    memcpy(&right_leaf->leaf->keys[0], &page->leaf->keys[split_idx], 
        (page->header.num_keys - split_idx) * sizeof(uint32_t));
    memcpy(&right_leaf->leaf->rids[0], &page->leaf->rids[split_idx],
        (page->header.num_keys - split_idx) * sizeof(uint64_t));

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
        leaf->leaf->rids[keyidx + 1] = buffer_manager_request_slot(size);
        return;
    }

    leaf_insert(bpt, key, data, size, leaf, bpt->parent_stack);
}

// Currently returs rid as a placeholder
uint64_t bpt_get(BPTree *bpt, uint32_t key) {
    Page *leaf = search(bpt, key);
    uint32_t idx = lower_bound(leaf->leaf->keys, leaf->header.num_keys, key);
    if (idx != leaf->header.num_keys && leaf->leaf->keys[idx] == key) {
        return leaf->leaf->rids[idx + 1];
    }
    else {
        return 0;
    }
}

void bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high, 
    void (*callback)(uint32_t key, uint64_t value)) {
    
    if (key_high < key_low) {
        return;
    }

    Page *leaf = search(bpt, key_low);

    while (leaf) {
        for (uint32_t i = 0; i < leaf->header.num_keys; i++) {
            if (leaf->leaf->keys[i] > key_high) return;
        
            if (leaf->leaf->keys[i] >= key_low) {
                callback(leaf->leaf->keys[i], leaf->leaf->rids[i]);
            }
        }
        leaf = buffer_manager_get_page(bpt->bm, leaf->leaf->next_page_id);
    }
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