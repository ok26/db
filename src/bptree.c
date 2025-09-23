#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bptree.h"
#include "util.h"
#include "buffer_manager.h"

// Remove comment for full debug
// #define FULL_DEBUG


// BPTrees should share buffermanagers if they manage the same memory
// In other words the buffermanager should possibly be passed to the
// bpt initializer

struct BPTree {
    Page *root;
    Stack *parent_stack;
};

BPTree *bpt_init() {
    BPTree *bpt = malloc(sizeof(BPTree));
    bpt->root = buffer_manager_new_page(1);
    bpt->parent_stack = stack_init();
    return bpt;
}

void bpt_free(BPTree *bpt) {
    stack_free(bpt->parent_stack);
    buffer_manager_free();
    free(bpt);
}

Page *search(BPTree *bpt, uint32_t key) {
    Page *node = bpt->root;
    stack_clear(bpt->parent_stack);
    while (!node->is_leaf) {
        stack_push(bpt->parent_stack, node->page_id);
        uint32_t idx = upper_bound(node->keys, node->num_keys, key);
        node = buffer_manager_get_page(node->pointers[idx]);
    }
    return node;
}

uint32_t bpt_height(BPTree *bpt) {
    Page *node = bpt->root;
    uint32_t h = 0;
    while (!node->is_leaf) {
        h++;
        node = buffer_manager_get_page(node->pointers[0]);
    }
    return h;
}

Page *internal_insert(uint32_t key, uint32_t pointer, 
    Page *node, Stack *parent_stack) {

    // Insert into node
    for (uint32_t i = 0; i <= node->num_keys; i++) {
        
        if (i == node->num_keys) {
            node->keys[node->num_keys] = key;
            node->pointers[node->num_keys + 1] = pointer;
            break;
        }

        if (node->keys[i] > key) {
            memmove(&node->keys[i + 1], &node->keys[i],
                (node->num_keys - i) * sizeof(uint32_t));
            memmove(&node->pointers[i + 2], &node->pointers[i + 1], 
                (node->num_keys - i) * sizeof(uint32_t));
            node->keys[i] = key;
            node->pointers[i + 1] = pointer;
            break;
        }
    }

    node->num_keys++;

    if (node->num_keys <= MAX_KEYS) {
        // Return NULL, indicating no new root
        return NULL;
    }

    // Split Node
    uint32_t spidx = node->num_keys / 2;
    Page *r_node = buffer_manager_new_page(node->is_leaf);
    

    if (node->is_leaf) {
        memcpy(&r_node->keys[0], &node->keys[spidx], 
            (node->num_keys - spidx) * sizeof(uint32_t));
        memcpy(&r_node->pointers[1], &node->pointers[spidx + 1],
            (node->num_keys - spidx) * sizeof(uint32_t));
    }
    else {
        memcpy(&r_node->keys[0], &node->keys[spidx + 1],
            (node->num_keys - spidx - 1) * sizeof(uint32_t));
        memcpy(&r_node->pointers[0], &node->pointers[spidx + 1],
            (node->num_keys - spidx) * sizeof(uint32_t));
    }
    
    

    r_node->num_keys = node->num_keys - spidx;
    node->num_keys = spidx;
    
    if (node->is_leaf) {
        r_node->next_page_id = node->next_page_id;
        node->next_page_id = r_node->page_id;    
    }
    else {
        // Ignore top key, new leftmost-key splits two values
        r_node->num_keys--;
    }

    // Split root or insert into parent
    if (stack_is_empty(parent_stack)) {
        Page *root = buffer_manager_new_page(0);
        root->num_keys = 1;
        root->keys[0] = node->keys[node->num_keys];
        root->pointers[0] = node->page_id;
        root->pointers[1] = r_node->page_id;

        return root;
    }
    else {
        uint32_t prom_key = node->keys[node->num_keys];
        Page *parent = buffer_manager_get_page(stack_top(parent_stack));
        stack_pop(parent_stack);
        return internal_insert(prom_key, r_node->page_id, parent, parent_stack);
    }    
}

void bpt_insert(BPTree *bpt, uint32_t key, uint32_t data) {
    Page *leaf = search(bpt, key);
    uint32_t keyidx = lower_bound(leaf->keys, leaf->num_keys, key);
    if (keyidx != leaf->num_keys && leaf->keys[keyidx] == key) {
        // Overwrite old value
        leaf->pointers[keyidx + 1] = data;
        return;
    }

    Page *new_root = internal_insert(key, data, leaf, bpt->parent_stack);
    if (new_root) {
        bpt->root = new_root;
    }
}

uint32_t bpt_get(BPTree *bpt, uint32_t key) {
    Page *leaf = search(bpt, key);
    uint32_t idx = lower_bound(leaf->keys, leaf->num_keys, key);
    if (idx != leaf->num_keys && leaf->keys[idx] == key) {
        return leaf->pointers[idx + 1];
    }
    else {
        return 0;
    }
}

void bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high, 
    void (*callback)(uint32_t key, uint32_t value)) {
    
    if (key_high < key_low) {
        return;
    }

    Page *leaf = search(bpt, key_low);

    while (leaf) {
        for (uint32_t i = 0; i < leaf->num_keys; i++) {
            if (leaf->keys[i] > key_high) return;
        
            if (leaf->keys[i] >= key_low) {
                callback(leaf->keys[i], leaf->pointers[i + 1]);
            }
        }
        leaf = buffer_manager_get_page(leaf->next_page_id);
    }
}

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
        for (uint32_t i = 0; i < node->num_keys; i++) {
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
                queue[back++] = buffer_manager_get_page(node->pointers[i]);
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