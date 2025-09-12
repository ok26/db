#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "bptree.h"
#include "util.h"

#define MAX_CHILDREN 3
#define MIN_CHILDREN (MAX_CHILDREN + 1) / 2
#define MAX_KEYS MAX_CHILDREN - 1

// Remove comment for full debug
// #define FULL_DEBUG

struct BPTreeNode {
    uint8_t is_leaf;
    uint32_t num_keys;
    uint32_t keys[MAX_KEYS + 1];
    void *pointers[MAX_CHILDREN + 1];
    BPTreeNode *next;
    BPTreeNode *parent;
};

BPTreeNode *new_node(uint8_t is_leaf) {
    BPTreeNode *node = malloc(sizeof(BPTreeNode));
    node->is_leaf = is_leaf;
    node->num_keys = 0;
    node->next = NULL;
    node->parent = NULL;
    return node;
}

void free_node(BPTreeNode *node) {
    for (int i = 0; i <= node->num_keys && node->num_keys != 0; i++) {
        if (node->is_leaf && i != node->num_keys) {
            free(node->pointers[i + 1]);
        }
        else if (!node->is_leaf) {
            free_node((BPTreeNode*)node->pointers[i]);
        }
    }
    free(node);
}

struct BPTree {
    BPTreeNode *root;
};

BPTree *init_bpt() {
    BPTree *bpt = malloc(sizeof(BPTree));
    BPTreeNode *root = new_node(1);
    bpt->root = root;
    return bpt;
}

void free_bpt(BPTree *bpt) {
    free_node(bpt->root);
    free(bpt);
}

BPTreeNode *search(BPTree *bpt, uint32_t key) {
    BPTreeNode *node = bpt->root;
    while (!node->is_leaf) {
        uint32_t idx = upper_bound(node->keys, node->num_keys, key);
        node = (BPTreeNode*)node->pointers[idx];
    }
    return node;
}

void internal_insert(uint32_t key, void *pointer, BPTreeNode *node) {

    // Insert into node
    for (int i = 0; i <= node->num_keys; i++) {
        
        if (i == node->num_keys) {
            node->keys[node->num_keys] = key;
            node->pointers[node->num_keys + 1] = pointer;
            break;
        }

        if (node->keys[i] > key) {
            memmove(&node->keys[i + 1], &node->keys[i],
                (node->num_keys - i) * sizeof(uint32_t));
            memmove(&node->pointers[i + 2], &node->pointers[i + 1], 
                (node->num_keys - i) * sizeof(void*));
            node->keys[i] = key;
            node->pointers[i + 1] = pointer;
            break;
        }
    }

    node->num_keys++;

    if (node->num_keys <= MAX_KEYS) {
        return;
    }

    // Split Node
    uint32_t spidx = node->num_keys / 2;
    BPTreeNode *r_node = new_node(node->is_leaf);
    

    if (node->is_leaf) {
        memcpy(&r_node->keys[0], &node->keys[spidx], 
            (node->num_keys - spidx) * sizeof(uint32_t));
        memcpy(&r_node->pointers[1], &node->pointers[spidx + 1],
            (node->num_keys - spidx) * sizeof(void*));
    }
    else {
        memcpy(&r_node->keys[0], &node->keys[spidx + 1],
            (node->num_keys - spidx - 1) * sizeof(uint32_t));
        memcpy(&r_node->pointers[0], &node->pointers[spidx + 1],
            (node->num_keys - spidx) * sizeof(void*));
    }
    
    

    r_node->num_keys = node->num_keys - spidx;
    node->num_keys = spidx;
    
    if (node->is_leaf) {
        r_node->next = node->next;
        node->next = r_node;
        
    }
    else {
        // Ignore top key, new leftmost-key splits two values
        r_node->num_keys--;

        // Update children
        for (int i = 0; i <= r_node->num_keys; i++) {
            BPTreeNode *child = (BPTreeNode*)r_node->pointers[i];
            child->parent = r_node;
        }
    }

    // Split root or insert into parent
    if (!node->parent) {
        BPTreeNode *root = new_node(0);
        root->num_keys = 1;
        root->keys[0] = node->keys[node->num_keys];
        root->pointers[0] = node;
        root->pointers[1] = r_node;

        node->parent = root;
        r_node->parent = root;
    }
    else {
        r_node->parent = node->parent;
        uint32_t prom_key = node->keys[node->num_keys];
        internal_insert(prom_key, r_node, node->parent);
    }    
}

void insert(BPTree *bpt, uint32_t key, void *data) {
    BPTreeNode *leaf = search(bpt, key);
    internal_insert(key, data, leaf);
    while (bpt->root->parent) {
        bpt->root = bpt->root->parent;
    }
}

void *get(BPTree *bpt, uint32_t key) {
    BPTreeNode *leaf = search(bpt, key);
    uint32_t idx = lower_bound(leaf->keys, leaf->num_keys, key);
    if (idx != leaf->num_keys && leaf->keys[idx] == key) {
        return leaf->pointers[idx + 1];
    }
    else {
        return NULL;
    }
}

void range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high, 
    void (*callback)(uint32_t key, void *value)) {
    
    if (key_high < key_low) {
        return;
    }

    BPTreeNode *leaf = search(bpt, key_low);

    while (leaf) {
        for (int i = 0; i < leaf->num_keys; i++) {
            if (leaf->keys[i] > key_high) return;
        
            if (leaf->keys[i] >= key_low) {
                callback(leaf->keys[i], leaf->pointers[i + 1]);
            }
        }
        leaf = leaf->next;
    }
}

void print_tree(BPTree *bpt) {
    if (!bpt->root) return;

    BPTreeNode *queue[1024];
    int front = 0, back = 0;

    int nodes_in_level = 1;
    int nodes_next_level = 0;

    queue[back++] = bpt->root;

    int level = 0;
    printf("L0 ");
    while (front < back) {
        BPTreeNode *node = queue[front++];
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
                queue[back++] = (BPTreeNode*)node->pointers[i];
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