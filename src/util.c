#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

#define STACK_DEFAULT_SIZE 8

// Returns index of first element strictly greater than v
uint32_t upper_bound(const uint32_t *arr, uint32_t len, uint32_t v) {
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
uint32_t lower_bound(const uint32_t *arr, uint32_t len, uint32_t v) {
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


struct Stack {
    uint32_t *stack;
    uint32_t size;
    uint32_t resvsize;
};

Stack *stack_init(void) {
    Stack *stack = malloc(sizeof(Stack));
    if (!stack) return NULL;
    stack->resvsize = STACK_DEFAULT_SIZE;
    stack->stack = malloc(sizeof(uint32_t) * stack->resvsize);
    if (!stack->stack) {
        free(stack);
        return NULL;
    }
    stack->size = 0;
    return stack;
}

void stack_free(Stack *stack) {
    if (!stack) return;
    free(stack->stack);
    free(stack);
}

uint32_t stack_top(Stack *stack) {
    if (!stack_is_empty(stack)) {
        return stack->stack[stack->size - 1];
    }
    return 0;
}

void stack_pop(Stack *stack) {
    if (!stack_is_empty(stack)) {
        stack->size--;
    }
}

uint8_t stack_push(Stack *stack, uint32_t value) {
    if (stack->size == stack->resvsize) {
        uint32_t *grown = realloc(stack->stack,
            sizeof(uint32_t) * stack->resvsize * 2);
        if (!grown) return 0;
        stack->stack = grown;
        stack->resvsize *= 2;
    }
    stack->stack[stack->size] = value;
    stack->size++;
    return 1;
}

void stack_clear(Stack *stack) {
    stack->size = 0;
}

uint8_t stack_is_empty(Stack *stack) {
    return stack->size == 0;
}

uint32_t stack_size(Stack *stack) {
    return stack->size;
}

Stack *stack_clone(Stack *stack) {
    Stack *new_stack = malloc(sizeof(Stack));
    if (!new_stack) return NULL;
    new_stack->size = stack->size;
    new_stack->resvsize = stack->resvsize;
    new_stack->stack = malloc(sizeof(uint32_t) * stack->resvsize);
    if (!new_stack->stack) {
        free(new_stack);
        return NULL;
    }
    memcpy(new_stack->stack, stack->stack, sizeof(uint32_t) * stack->size);
    return new_stack;
}




#define RBTN_RED 0
#define RBTN_BLACK 1

typedef struct RBTreeNode RBTreeNode;
struct RBTreeNode {
    uint32_t key;
    void *value;
    uint8_t color;
    RBTreeNode *left;
    RBTreeNode *right;
    RBTreeNode *parent;
};

static RBTreeNode *rbtn_new(uint32_t key, const void *value, size_t size,
        RBTreeNode *parent) {
    RBTreeNode *node = malloc(sizeof(RBTreeNode));
    if (!node) return NULL;
    // malloc(0) may return NULL, always allocate at least one byte
    node->value = malloc(size ? size : 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, value, size);
    node->color = RBTN_RED;
    node->key = key;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static void rbtn_free(RBTreeNode *node) {
    if (!node) return;
    rbtn_free(node->left);
    rbtn_free(node->right);
    free(node->value);
    free(node);
}

static void rbtn_free_single(RBTreeNode *node) {
    free(node->value);
    free(node);
}

static uint8_t rbtn_is_red(RBTreeNode *node) {
    return node && node->color == RBTN_RED;
}

struct RBTree {
    RBTreeNode *root;
    uint32_t size;
};

RBTree *rbt_init(void) {
    RBTree *rbt = malloc(sizeof(RBTree));
    if (!rbt) return NULL;
    rbt->root = NULL;
    rbt->size = 0;
    return rbt;
}

// Returns the node with the given key, or the node that would become its
// parent if the key does not exist. Returns NULL for an empty tree.
static RBTreeNode *rbt_search(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt->root;
    while (node) {
        if (key == node->key) {
            return node;
        }

        RBTreeNode *next = key > node->key ? node->right : node->left;
        if (!next) return node;
        node = next;
    }
    return NULL;
}

// Replaces the child `old_child` of `parent` with `new_child`
static void rbt_replace_child(RBTree *rbt, RBTreeNode *parent,
        RBTreeNode *old_child, RBTreeNode *new_child) {
    if (!parent) rbt->root = new_child;
    else if (parent->left == old_child) parent->left = new_child;
    else parent->right = new_child;
}

// From: https://en.wikipedia.org/wiki/Red%E2%80%93black_tree
// A left rotation moves node->right up into the place of node
static void rbt_rotate(RBTree *rbt, RBTreeNode *node, uint8_t left_rotation) {
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
    rbt_replace_child(rbt, parent, node, new_root);
}

uint8_t rbt_insert(RBTree *rbt, uint32_t key, const void *value, size_t size) {
    RBTreeNode *parent = rbt_search(rbt, key);
    if (parent && parent->key == key) {
        // If key already exists, update value
        void *new_value = malloc(size ? size : 1);
        if (!new_value) return 0;
        memcpy(new_value, value, size);
        free(parent->value);
        parent->value = new_value;
        return 1;
    }

    RBTreeNode *node = rbtn_new(key, value, size, parent);
    if (!node) return 0;
    rbt->size++;

    if (!parent) {
        node->color = RBTN_BLACK;
        rbt->root = node;
        return 1;
    }

    if (key < parent->key) parent->left = node;
    else parent->right = node;

    // Rebalance, node is red and might have a red parent
    while (parent && parent->color == RBTN_RED) {
        RBTreeNode *grandparent = parent->parent;
        if (!grandparent) {
            // Parent is the root, simply recolor it
            parent->color = RBTN_BLACK;
            break;
        }

        uint8_t parent_left_child = grandparent->left == parent;
        RBTreeNode *uncle = parent_left_child ?
            grandparent->right : grandparent->left;
        if (!rbtn_is_red(uncle)) {
            uint8_t node_left_child = parent->left == node;
            if (node_left_child != parent_left_child) {
                // Inner grandchild, rotate it to the outside first
                rbt_rotate(rbt, parent, parent_left_child);
                node = parent;
                parent = parent_left_child ? grandparent->left : grandparent->right;
            }

            rbt_rotate(rbt, grandparent, !parent_left_child);
            parent->color = RBTN_BLACK;
            grandparent->color = RBTN_RED;
            break;
        }

        parent->color = RBTN_BLACK;
        uncle->color = RBTN_BLACK;
        grandparent->color = RBTN_RED;
        node = grandparent;
        parent = node->parent;
    }

    rbt->root->color = RBTN_BLACK;
    return 1;
}

// Restores the black height after a black leaf was removed from the
// side `left` of `parent`. Cases are named as in the wikipedia article.
static void rbt_delete_fixup(RBTree *rbt, RBTreeNode *parent, uint8_t left) {
    RBTreeNode *node = NULL;
    while (parent) {
        if (node) left = (parent->left == node);

        // The sibling cannot be NULL since the node side is one black short
        RBTreeNode *sibling = left ? parent->right : parent->left;
        RBTreeNode *close_nephew = left ? sibling->left : sibling->right;
        RBTreeNode *distant_nephew = left ? sibling->right : sibling->left;

        if (sibling->color == RBTN_RED) {
            // Case D3: make the sibling black by rotating it above parent
            rbt_rotate(rbt, parent, left);
            parent->color = RBTN_RED;
            sibling->color = RBTN_BLACK;
            sibling = close_nephew;
            close_nephew = left ? sibling->left : sibling->right;
            distant_nephew = left ? sibling->right : sibling->left;
            // Parent is now red so one of the cases D4-D6 applies below
        }

        if (!rbtn_is_red(distant_nephew) && rbtn_is_red(close_nephew)) {
            // Case D5: rotate the close nephew into the distant position
            rbt_rotate(rbt, sibling, !left);
            sibling->color = RBTN_RED;
            close_nephew->color = RBTN_BLACK;
            distant_nephew = sibling;
            sibling = close_nephew;
        }

        if (rbtn_is_red(distant_nephew)) {
            // Case D6
            rbt_rotate(rbt, parent, left);
            sibling->color = parent->color;
            parent->color = RBTN_BLACK;
            distant_nephew->color = RBTN_BLACK;
            return;
        }

        if (parent->color == RBTN_RED) {
            // Case D4
            sibling->color = RBTN_RED;
            parent->color = RBTN_BLACK;
            return;
        }

        // Case D2: everything is black, move the problem one level up
        sibling->color = RBTN_RED;
        node = parent;
        parent = node->parent;
    }
}

static void rbt_delete_node(RBTree *rbt, RBTreeNode *node) {
    if (node->left && node->right) {
        // Replace with in-order successor which has at most one child
        RBTreeNode *successor = node->right;
        while (successor->left) successor = successor->left;

        node->key = successor->key;
        void *tmp = node->value;
        node->value = successor->value;
        successor->value = tmp;
        node = successor;
    }

    RBTreeNode *parent = node->parent;
    RBTreeNode *child = node->left ? node->left : node->right;

    if (child) {
        // A node with exactly one child has to be black with a red child
        child->parent = parent;
        child->color = RBTN_BLACK;
        rbt_replace_child(rbt, parent, node, child);
        rbtn_free_single(node);
        return;
    }

    uint8_t was_black = node->color == RBTN_BLACK;
    uint8_t left_child = parent && parent->left == node;
    rbt_replace_child(rbt, parent, node, NULL);
    rbtn_free_single(node);

    if (parent && was_black) {
        rbt_delete_fixup(rbt, parent, left_child);
    }
}

uint8_t rbt_delete(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt_search(rbt, key);
    if (!node || node->key != key) {
        return 0;
    }

    rbt->size--;
    rbt_delete_node(rbt, node);
    return 1;
}

void *rbt_get(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt_search(rbt, key);
    return node && key == node->key ? node->value : NULL;
}

void rbt_free(RBTree *rbt) {
    if (!rbt) return;
    rbtn_free(rbt->root);
    free(rbt);
}

uint32_t rbt_size(RBTree *rbt) {
    return rbt->size;
}

void rbt_clear(RBTree *rbt) {
    rbtn_free(rbt->root);
    rbt->root = NULL;
    rbt->size = 0;
}

// Returns the black height of the subtree, or -1 if any invariant is broken.
// All keys in the subtree have to be in [lower, upper].
static int rbt_verify_dfs(RBTreeNode *node, RBTreeNode *parent,
        uint64_t lower, uint64_t upper, uint32_t *count) {
    if (!node) return 1;
    if (node->parent != parent) return -1;
    if (node->key < lower || node->key > upper) return -1;
    if (node->color == RBTN_RED && rbtn_is_red(parent)) return -1;
    (*count)++;

    int left = rbt_verify_dfs(node->left, node, lower,
        (uint64_t)node->key - 1, count);
    int right = rbt_verify_dfs(node->right, node,
        (uint64_t)node->key + 1, upper, count);
    if (left < 0 || right < 0 || left != right) return -1;
    return left + (node->color == RBTN_BLACK);
}

uint8_t rbt_verify_tree(RBTree *rbt) {
    if (rbtn_is_red(rbt->root)) return 0;
    uint32_t count = 0;
    if (rbt_verify_dfs(rbt->root, NULL, 0, UINT32_MAX, &count) < 0) return 0;
    return count == rbt->size;
}

void rbt_print(RBTree *rbt) {
    if (!rbt->root) return;

    RBTreeNode **queue = malloc(sizeof(RBTreeNode*) * rbt->size);
    if (!queue) return;
    uint32_t front = 0, back = 0;

    uint32_t nodes_in_level = 1;
    uint32_t nodes_next_level = 0;

    queue[back++] = rbt->root;

    while (front < back) {
        RBTreeNode *node = queue[front++];
        nodes_in_level--;

        printf("[k%u, %c] ", node->key, node->color == RBTN_RED ? 'r' : 'b');

        if (node->left) {
            queue[back++] = node->left;
            nodes_next_level++;
        }
        if (node->right) {
            queue[back++] = node->right;
            nodes_next_level++;
        }

        if (nodes_in_level == 0) {
            printf("\n");
            nodes_in_level = nodes_next_level;
            nodes_next_level = 0;
        }
    }
    free(queue);
}


#define HEAP_DEFAULT_SIZE 0x8

struct Heap {
    uint8_t *data;
    uint32_t size;
    uint32_t resvsize;
    cmpf cmp;
    size_t type_size;
    void *scratch; // Temporary storage of one element, used when swapping
};

static uint8_t u32_greater(const void *a, const void *b) {
    return *(const uint32_t*)a < *(const uint32_t*)b;
}

static uint8_t u32_lesser(const void *a, const void *b) {
    return *(const uint32_t*)a > *(const uint32_t*)b;
}

Heap *new_heap(cmpf cmp, size_t type_size) {
    Heap *heap = malloc(sizeof(Heap));
    if (!heap) return NULL;
    heap->data = malloc(type_size * HEAP_DEFAULT_SIZE);
    heap->scratch = malloc(type_size);
    if (!heap->data || !heap->scratch) {
        free(heap->data);
        free(heap->scratch);
        free(heap);
        return NULL;
    }
    heap->size = 0;
    heap->resvsize = HEAP_DEFAULT_SIZE;
    heap->cmp = cmp;
    heap->type_size = type_size;
    return heap;
}

Heap *new_maxheap(void) {
    return new_heap(u32_greater, sizeof(uint32_t));
}

Heap *new_minheap(void) {
    return new_heap(u32_lesser, sizeof(uint32_t));
}

static void *datai(Heap *heap, uint32_t i) {
    return heap->data + (size_t)i * heap->type_size;
}

static void swap(Heap *heap, uint32_t a, uint32_t b) {
    memcpy(heap->scratch, datai(heap, a), heap->type_size);
    memcpy(datai(heap, a), datai(heap, b), heap->type_size);
    memcpy(datai(heap, b), heap->scratch, heap->type_size);
}

static uint32_t ileftchild(uint32_t i) {
    return 2 * i + 1;
}

static uint32_t iparent(uint32_t i) {
    return (i - 1) / 2;
}

static void sift_down(Heap *heap, uint32_t start, uint32_t end) {
    uint32_t root = start;
    while (ileftchild(root) < end) {
        uint32_t child = ileftchild(root);
        if (child + 1 < end &&
                heap->cmp(datai(heap, child), datai(heap, child + 1))) {
            child++;
        }

        if (heap->cmp(datai(heap, root), datai(heap, child))) {
            swap(heap, root, child);
            root = child;
        }
        else {
            break;
        }
    }
}

static void sift_up(Heap *heap, uint32_t start) {
    uint32_t root = start;
    while (root != 0) {
        uint32_t parent = iparent(root);
        if (heap->cmp(datai(heap, parent), datai(heap, root))) {
            swap(heap, root, parent);
            root = parent;
        }
        else {
            break;
        }
    }
}

void *heap_top(Heap *heap) {
    if (heap->size == 0) {
        return NULL;
    }

    return datai(heap, 0);
}

uint8_t heap_insert(Heap *heap, const void *value) {
    if (heap->size == heap->resvsize) {
        uint8_t *grown = realloc(heap->data,
            (size_t)heap->resvsize * 2 * heap->type_size);
        if (!grown) return 0;
        heap->data = grown;
        heap->resvsize *= 2;
    }

    memcpy(datai(heap, heap->size), value, heap->type_size);
    heap->size++;
    sift_up(heap, heap->size - 1);
    return 1;
}

void heap_pop(Heap *heap) {
    if (heap_is_empty(heap)) {
        return;
    }

    heap->size--;
    if (heap_is_empty(heap)) {
        return;
    }

    swap(heap, heap->size, 0);
    sift_down(heap, 0, heap->size);
}

uint32_t heap_size(Heap *heap) {
    return heap->size;
}

uint8_t heap_is_empty(Heap *heap) {
    return heap->size == 0;
}

void heap_free(Heap *heap) {
    if (!heap) return;
    free(heap->data);
    free(heap->scratch);
    free(heap);
}
