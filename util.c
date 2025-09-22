#include <stdlib.h>
#include <stdio.h>
#include "util.h"

#define STACK_DEFAULT_SIZE 8

// Returns index of first element strictly greater than v
uint32_t upper_bound(uint32_t *arr, uint32_t len, uint32_t v) {
    uint32_t l = 0, r = len;
    while (l < r) {
        uint32_t mid = l + (r - l) / 2;
        if (arr[mid] <= v) {
            l = mid + 1;
        }
        else {
            r = mid;
        }
    }

    return l;
}

// Returns index of first element greater than or equal to v
uint32_t lower_bound(uint32_t *arr, uint32_t len, uint32_t v) {
    uint32_t l = 0, r = len;
    while (l < r) {
        uint32_t mid = l + (r - l) / 2;
        if (arr[mid] < v) {
            l = mid + 1;
        }
        else {
            r = mid;
        }
    }

    return l;
}

uint32_t *clone_u32(uint32_t *src) {
    uint32_t *dst = malloc(sizeof(uint32_t));
    *dst = *src;
    return dst;
}


struct Stack {
    uint32_t *stack;
    uint32_t size;
    uint32_t resvsize;
};

Stack *stack_init() {
    Stack *stack = malloc(sizeof(Stack));
    stack->resvsize = STACK_DEFAULT_SIZE;
    stack->stack = malloc(sizeof(uint32_t) * stack->resvsize);
    stack->size = 0;
    return stack;
}

void stack_free(Stack *stack) {
    free(stack->stack);
    free(stack);
}

uint32_t stack_top(Stack *stack) {
    if (!stack_is_empty(stack)) {
        return stack->stack[stack->size - 1];
    }
    return -1;
}

void stack_pop(Stack *stack) {
    if (!stack_is_empty(stack)) {
        stack->size--;
    }
}

void stack_push(Stack *stack, uint32_t value) {
    if (stack->size == stack->resvsize) {
        stack->resvsize *= 2;
        stack->stack = realloc(stack->stack, sizeof(uint32_t) * stack->resvsize);
    }
    stack->stack[stack->size] = value;
    stack->size++;
}

void stack_clear(Stack *stack) {
    stack->size = 0;
}

uint8_t stack_is_empty(Stack *stack) {
    return stack->size == 0;
}




#define RBTN_RED 0
#define RBTN_BLACK 1

typedef struct RBTreeNode RBTreeNode;
struct RBTreeNode {
    uint32_t key;
    void *value;
    uint8_t color;
    struct RBTreeNode *left;
    RBTreeNode *right;
    RBTreeNode *parent;
};

RBTreeNode *rbtn_new_root(uint32_t key, void *value) {
    RBTreeNode *root = malloc(sizeof(RBTreeNode));
    root->color = RBTN_RED;
    root->key = key;
    root->value = value;
    root->parent = NULL;
    root->left = NULL;
    root->right = NULL;
    return root;
}

RBTreeNode *rbtn_new(uint32_t key, void *value, RBTreeNode *parent) {
    RBTreeNode *node = malloc(sizeof(RBTreeNode));
    node->color = RBTN_RED;
    node->key = key;
    node->value = value;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    return node;
}

void rbtn_free(RBTreeNode *node) {
    if (node->left) rbtn_free(node->left);
    if (node->right) rbtn_free(node->right);
    free(node->value);
    free(node);
}

struct RBTree {
    RBTreeNode *root;
};

RBTree *rbt_init() {
    RBTree *rbt = malloc(sizeof(RBTree));
    rbt->root = NULL;
    return rbt;
}

RBTreeNode *rbt_search(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt->root;

    while (1) {
        if (key == node->key) {
            return node;
        }

        if (key >= node->key) {
            if (!node->right) return node;
            else node = node->right;
        }
        else {
            if (!node->left) return node;
            else node = node->left;
        }
    }
}

// From: https://en.wikipedia.org/wiki/Red%E2%80%93black_tree
void rbt_rotate(RBTree *rbt, RBTreeNode *node, uint8_t left_rotation) {
    RBTreeNode *parent = node->parent;
    RBTreeNode *new_root = left_rotation ? node->right : node->left;
    RBTreeNode *new_child = left_rotation ? new_root->left : new_root->right;

    if (left_rotation) node->right = new_child;
    else node->left = new_child;

    if (new_child) {
        new_child->parent = node;
    }

    if (left_rotation) new_root->left = node;
    else new_root->right = node;

    new_root->parent = parent;
    node->parent = new_root;
    if (parent) {
        if (node == parent->right) parent->right = new_root;
        else parent->left = new_root;
    }
    else {
        rbt->root = new_root;
    }
}

void rbt_insert(RBTree *rbt, uint32_t key, void *value) {
    if (!rbt->root) {
        rbt->root = rbtn_new_root(key, value);
        return;
    }

    RBTreeNode *parent = rbt_search(rbt, key);
    if (parent->key == key) {
        // If key already exists, update value
        free(parent->value);
        parent->value = value;
        return;
    }

    RBTreeNode *node = rbtn_new(key, parent, value);

    uint8_t left_child = key < node->key;
    if (left_child) parent->left = node;
    else parent->right = node;

    while (parent) {
        if (node->parent->color == RBTN_BLACK) {
            return;
        }

        RBTreeNode *grandparent = parent->parent;
        if (!grandparent) {
            parent->color = RBTN_BLACK;
            return;
        }

        uint8_t parent_left_child = grandparent->left == parent;
        RBTreeNode *uncle = parent_left_child ?
            grandparent->right : grandparent->left;
        if (!uncle || uncle->color == RBTN_BLACK) {
            uint8_t node_left_child = parent->left == node;
            if (node_left_child != parent_left_child) {
                rbt_rotate(rbt, parent, parent_left_child);
                node = parent;
                parent = parent_left_child ? grandparent->left : grandparent->right;
            }

            rbt_rotate(rbt, grandparent, !parent_left_child);
			parent->color = RBTN_BLACK;
			grandparent->color = RBTN_RED;
			return;
        }

        parent->color = RBTN_BLACK;
        uncle->color = RBTN_BLACK;
        grandparent->color = RBTN_RED;
        node = grandparent;
        parent = node->parent;
    }
}

void rbt_delete(RBTree *rbt, uint32_t key) {

}

void *rbt_get(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt_search(rbt, key);
    return key == node->key ? node->value : NULL;
}

void rbt_free(RBTree *rbt) {
    if (rbt->root) rbtn_free(rbt->root);
    free(rbt);
}



typedef uint8_t (*cmpf)(uint32_t, uint32_t);
struct Heap {
    uint32_t *data;
    uint32_t size;
    uint32_t resvsize;
    cmpf cmp;
};

uint8_t greater(uint32_t a, uint32_t b) {
    return a > b;
}

uint8_t lesser(uint32_t a, uint32_t b) {
    return a < b;
}

Heap new_heap(cmpf cmp) {
    Heap heap;
    heap.data = malloc(sizeof(uint32_t) * 8);
    heap.size = 0;
    heap.resvsize = 8;
    heap.cmp = cmp;
    return heap;
}

Heap new_maxheap() {
    return new_heap(greater);
}

Heap new_minheap() {
    return new_heap(lesser);
}

void swap(uint32_t *a, uint32_t *b) {
    uint32_t tmp = *a;
    *a = *b;
    *b = tmp;
}

uint32_t ileftchild(uint32_t i) {
    return 2 * i + 1;
}

uint32_t irightchild(uint32_t i) {
    return 2 * i + 2;
}

uint32_t iparent(uint32_t i) {
    if (i == 0) {
        return 0;
    }
    return (i - 1) / 2;
}

void sift_down(uint32_t *data, uint32_t start, uint32_t end, cmpf cmp) {
    uint32_t root = start;

    while (ileftchild(root) < end) {
        uint32_t child = ileftchild(root);
        if (child + 1 < end && cmp(data[child], data[child + 1])) {
            child++;
        }

        if (cmp(data[root], data[child])) {
            swap(&data[root], &data[child]);
            root = child;
        }
        else {
            break;
        }
    }
}

void sift_up(uint32_t *data, uint32_t start, cmpf cmp) {
    uint32_t root = start;

    while (root != 0) {
        uint32_t parent = iparent(root);
        if (!cmp(data[root], data[parent])) {
            swap(&data[root], &data[parent]);
            root = parent;
        }
        else {
            break;
        }
    }
}

uint32_t top(Heap *heap) {
    if (heap->size == 0) {
        return 0;
    }

    return heap->data[0];
}

void insert(Heap *heap, uint32_t value) {
    if (heap->size == heap->resvsize) {
        heap->resvsize *= 2;
        heap->data = realloc(heap->data, heap->resvsize * sizeof(uint32_t));
    }

    heap->data[heap->size] = value;
    heap->size++;
    sift_up(heap->data, heap->size - 1, heap->cmp);
}

void pop(Heap *heap) {
    heap->size--;
    if (heap->size == 0) {
        return;
    }

    swap(&heap->data[heap->size], &heap->data[0]);
    sift_down(heap->data, 0, heap->size, heap->cmp);
}

size_t size(Heap *heap) {
    return heap->size;
}

uint8_t is_empty(Heap *heap) {
    return heap->size == 0;
}

void free_heap(Heap *heap) {
    free(heap->data);
}