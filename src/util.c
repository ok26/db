#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
    return 0;
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

Stack *stack_clone(Stack *stack) {
    Stack *new_stack = malloc(sizeof(Stack));
    new_stack->size = stack->size;
    new_stack->resvsize = stack->resvsize;
    new_stack->stack = malloc(sizeof(uint32_t) * stack->resvsize);
    // Copy existing stack entries so the clone is a true copy
    if (stack->size > 0) {
        memcpy(new_stack->stack, stack->stack, sizeof(uint32_t) * stack->size);
    }
    return new_stack;
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

RBTreeNode *rbtn_new_root(uint32_t key, void *value, size_t size) {
    RBTreeNode *root = malloc(sizeof(RBTreeNode));
    root->color = RBTN_RED;
    root->key = key;
    root->value = malloc(size);
    memcpy(root->value, value, size);
    root->parent = NULL;
    root->left = NULL;
    root->right = NULL;
    return root;
}

RBTreeNode *rbtn_new(uint32_t key, void *value, size_t size, RBTreeNode *parent) {
    RBTreeNode *node = malloc(sizeof(RBTreeNode));
    node->color = RBTN_RED;
    node->key = key;
    node->value = malloc(size);
    memcpy(node->value, value, size);
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

void rbtn_free_single(RBTreeNode *node) {
    free(node->value);
    free(node);
}

struct RBTree {
    RBTreeNode *root;
    uint32_t size;
};

RBTree *rbt_init() {
    RBTree *rbt = malloc(sizeof(RBTree));
    rbt->root = NULL;
    rbt->size = 0;
    return rbt;
}

RBTreeNode *rbt_search(RBTree *rbt, uint32_t key) {
    if (!rbt->root) return NULL;

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

void rbt_insert(RBTree *rbt, uint32_t key, void *value, size_t size) {
    if (!rbt->root) {
        rbt->root = rbtn_new_root(key, value, size);
        rbt->size = 1;
        return;
    }

    RBTreeNode *parent = rbt_search(rbt, key); // Cannot be null as root exists
    if (parent->key == key) {
        // If key already exists, update value
        parent->value = realloc(parent->value, size);
        memcpy(parent->value, value, size);
        return;
    }

    rbt->size++;

    RBTreeNode *node = rbtn_new(key, value, size, parent);

    uint8_t left_child = key < parent->key;
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

// Helper
void rbt_delete_node(RBTree *rbt, RBTreeNode *node, size_t data_size) {
    uint8_t child_cnt = (node->left != NULL) + (node->right != NULL);
    uint8_t node_is_root = !node->parent;

    if (child_cnt == 2) {
        RBTreeNode *left_most_successor = node->right;
        while (1) {
            if (left_most_successor->left)
                left_most_successor = left_most_successor->left;
            else
                break;
        }
        // Swap values
        node->key = left_most_successor->key;
        memcpy(node->value, left_most_successor->value, data_size);
        rbt_delete_node(rbt, left_most_successor, data_size);
        return;
    }

    if (child_cnt == 1) {
        RBTreeNode *child = node->left ? node->left : node->right;
        child->color = RBTN_BLACK;
        if (node_is_root) {
            child->parent = NULL;
            rbt->root = child;
            rbtn_free_single(node);
            return;
        }
        else {
            child->parent = node->parent;
            if (node->parent->left == node) node->parent->left = child;
            else node->parent->right = child;
            rbtn_free_single(node);
            return;
        }
    }

    // Otherwise, no children
    if (node_is_root) {
        rbtn_free_single(rbt->root);
        rbt->root = NULL;
        return;
    }

    if (node->color == RBTN_RED) {
        if (node->parent->left == node) node->parent->left = NULL;
        else node->parent->right = NULL;
        rbtn_free_single(node);
        return;
    }

    // Advanced case: no children and black
    RBTreeNode *parent = node->parent;

    // Remove node
    uint8_t left_child = (parent->left == node);
    rbtn_free_single(node);
    node = NULL;
    if (left_child) parent->left = NULL;
    else parent->right = NULL;

    RBTreeNode *sibling;
    RBTreeNode *distant_nephew;
    RBTreeNode *close_nephew;

    uint8_t case_6 = 0;
    uint8_t case_5 = 0;
    while (parent) {
        if (node) left_child = (parent->left == node);
        sibling = left_child ? parent->right : parent->left;
        distant_nephew = left_child ? sibling->right : sibling->left;
        close_nephew = left_child ? sibling->left : sibling->right;

        if (sibling->color == RBTN_RED) {
            rbt_rotate(rbt, parent, left_child);
            parent->color = RBTN_RED;
            sibling->color = RBTN_BLACK;
            sibling = close_nephew;

            distant_nephew = left_child ? sibling->right : sibling->left;
            if (distant_nephew && distant_nephew->color == RBTN_RED) {
                case_6 = 1;
                break;
            }
            close_nephew = left_child ? sibling->left : sibling->right;
            if (close_nephew && close_nephew->color == RBTN_RED) {
                case_5 = 1;
                break;
            }

            sibling->color = RBTN_RED;
            parent->color = RBTN_BLACK;
            break;
        }

        if (distant_nephew && distant_nephew->color == RBTN_RED) {
                case_6 = 1;
                break;
            }
        
        if (close_nephew && close_nephew->color == RBTN_RED) {
            case_5 = 1;
            break;
        }

        if (parent->color == RBTN_RED) {
			sibling->color = RBTN_RED;
			parent->color = RBTN_BLACK;
			break;
		}

        if (!parent) break;

        sibling->color = RBTN_RED;
		node = parent;
        parent = node->parent;
    }

    if (case_6) {
        rbt_rotate(rbt, sibling, !left_child);
        sibling->color = RBTN_RED;
        close_nephew->color = RBTN_BLACK;
        distant_nephew = sibling;
        sibling = close_nephew;
    }
    
    if (case_6 || case_5) {
        rbt_rotate(rbt, parent, left_child);
        sibling->color = parent->color; 
        parent->color = RBTN_BLACK;  
        distant_nephew->color = RBTN_BLACK;  
    }
}

void rbt_delete(RBTree *rbt, uint32_t key, size_t size) {
    RBTreeNode *node = rbt_search(rbt, key);
    if (!node || node->key != key) {
        printf("Node not found\n");
        return;
    }

    rbt->size--;
    rbt_delete_node(rbt, node, size);
}

void *rbt_get(RBTree *rbt, uint32_t key) {
    RBTreeNode *node = rbt_search(rbt, key);
    return node && key == node->key ? node->value : NULL;
}

void rbt_free(RBTree *rbt) {
    if (rbt->root) rbtn_free(rbt->root);
    free(rbt);
}

uint32_t rbt_size(RBTree *rbt) {
    return rbt->size;
}

void rbt_clear(RBTree *rbt) {
    if (rbt->root) rbtn_free(rbt->root);
    rbt->root = NULL;
    rbt->size = 0;
}

uint8_t rbt_verify_dfs(RBTree *rbt, RBTreeNode *node) {
    if (!node) return 1;
    if (node->parent && node->parent->color == RBTN_RED 
        && node->color == RBTN_RED) return 255;
    uint8_t black_cnt_left = rbt_verify_dfs(rbt, node->left);
    uint8_t black_cnt_right = rbt_verify_dfs(rbt, node->right);
    if (black_cnt_left == 255 || black_cnt_right == 255 || 
            black_cnt_left != black_cnt_right) {    
        return -1;
    }
    return black_cnt_left;
}

uint8_t rbt_verify_tree(RBTree *rbt) {
    uint8_t black_nodes_cnt = rbt_verify_dfs(rbt, rbt->root);
    if (black_nodes_cnt == 255) return 0;
    return 1;
}

void rbt_print(RBTree *rbt) {
    if (!rbt->root) return;

    RBTreeNode *queue[1024];
    uint32_t front = 0, back = 0;

    uint32_t nodes_in_level = 1;
    uint32_t nodes_next_level = 0;

    queue[back++] = rbt->root;

    while (front < back) {
        RBTreeNode *node = queue[front++];
        nodes_in_level--;

        printf("[k%u, f%d, v%p] ", node->key, node->color, node->value);

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
}


#define HEAP_DEFAULT_SIZE 0x8

struct Heap {
    void *data;
    uint32_t size;
    uint32_t resvsize;
    cmpf cmp;
    size_t type_size;
};

uint8_t u32_greater(void *a, void *b) {
    return *(uint32_t*)a < *(uint32_t*)b;
}

uint8_t u32_lesser(void *a, void *b) {
    return *(uint32_t*)a > *(uint32_t*)b;
}

Heap *new_heap(cmpf cmp, size_t type_size) {
    Heap *heap = malloc(sizeof(Heap));
    heap->data = malloc(type_size * HEAP_DEFAULT_SIZE);
    heap->size = 0;
    heap->resvsize = HEAP_DEFAULT_SIZE;
    heap->cmp = cmp;
    heap->type_size = type_size;
    return heap;
}

Heap *new_maxheap() {
    return new_heap(u32_greater, sizeof(uint32_t));
}

Heap *new_minheap() {
    return new_heap(u32_lesser, sizeof(uint32_t));
}

void swap(Heap *heap, void *a, void *b) {
    void *tmp = malloc(heap->type_size);
    memcpy(tmp, a, heap->type_size);
    memcpy(a, b, heap->type_size);
    memcpy(b, tmp, heap->type_size);
    free(tmp);
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

void *datai(Heap *heap, uint32_t i) {
    return (void*)(heap->data + i * heap->type_size);
}

void sift_down(Heap *heap, uint32_t start, uint32_t end) {

    uint32_t root = start;
    while (ileftchild(root) < end) {
        uint32_t child = ileftchild(root);
        if (child + 1 < end && 
                heap->cmp(datai(heap, child), datai(heap, child + 1))) {
            child++;
        }

        if (heap->cmp(datai(heap, root), datai(heap, child))) {
            swap(heap, datai(heap, root), datai(heap, child));
            root = child;
        }
        else {
            break;
        }
    }
}

void sift_up(Heap *heap, uint32_t start) {
    
    uint32_t root = start;
    while (root != 0) {
        uint32_t parent = iparent(root);
        if (!heap->cmp(datai(heap, root), datai(heap, parent))) {
            swap(heap, datai(heap, root), datai(heap, parent));
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

void heap_insert(Heap *heap, void *value) {
    if (heap->size == heap->resvsize) {
        heap->resvsize *= 2;
        heap->data = realloc(heap->data, heap->resvsize * heap->type_size);
    }

    void *dst = datai(heap, heap->size);
    memcpy(dst, value, heap->type_size);
    heap->size++;
    sift_up(heap, heap->size - 1);
}

void heap_pop(Heap *heap) {
    if (heap_is_empty(heap)) {
        return;
    }

    heap->size--;
    if (heap_is_empty(heap)) {
        return;
    }

    swap(heap, datai(heap, heap->size), datai(heap, 0));
    sift_down(heap, 0, heap->size);
}

uint32_t heap_size(Heap *heap) {
    return heap->size;
}

uint8_t heap_is_empty(Heap *heap) {
    return heap->size == 0;
}

void heap_free(Heap *heap) {
    free(heap->data);
    free(heap);
}