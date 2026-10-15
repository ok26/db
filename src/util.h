#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stddef.h>

// Binary search helpers on sorted uint32_t arrays
// upper_bound: index of the first element strictly greater than v
// lower_bound: index of the first element greater than or equal to v
uint32_t upper_bound(const uint32_t *arr, uint32_t len, uint32_t v);
uint32_t lower_bound(const uint32_t *arr, uint32_t len, uint32_t v);


// Stack of uint32_t
typedef struct Stack Stack;
Stack *stack_init(void);
void stack_free(Stack *stack);
uint32_t stack_top(Stack *stack);
void stack_pop(Stack *stack);
// Returns 1 on success, 0 if memory could not be allocated
uint8_t stack_push(Stack *stack, uint32_t value);
void stack_clear(Stack *stack);
uint8_t stack_is_empty(Stack *stack);
uint32_t stack_size(Stack *stack);
Stack *stack_clone(Stack *stack);


// Red-black tree mapping uint32_t keys to values of arbitrary size.
// Values are copied into the tree on insert.
typedef struct RBTree RBTree;
RBTree *rbt_init(void);
// Inserts or overwrites. Returns 1 on success, 0 if memory could not be allocated
uint8_t rbt_insert(RBTree *rbt, uint32_t key, const void *value, size_t size);
// Returns 1 if the key existed and was removed, 0 otherwise
uint8_t rbt_delete(RBTree *rbt, uint32_t key);
// Returns a pointer to the stored value, or NULL if the key does not exist
void *rbt_get(RBTree *rbt, uint32_t key);
void rbt_free(RBTree *rbt);
uint32_t rbt_size(RBTree *rbt);
void rbt_clear(RBTree *rbt);
// Checks all red-black and binary search tree invariants, returns 1 if valid
uint8_t rbt_verify_tree(RBTree *rbt);
void rbt_print(RBTree *rbt);

typedef RBTree Map;


// Binary heap of fixed-size elements. cmp(a, b) returns non-zero if a should
// be placed below b, i.e. b has a higher priority than a.
typedef struct Heap Heap;
typedef uint8_t (*cmpf)(const void *, const void *);
Heap *new_heap(cmpf cmp, size_t type_size);
Heap *new_maxheap(void); // uint32_t elements, largest on top
Heap *new_minheap(void); // uint32_t elements, smallest on top
void *heap_top(Heap *heap);
// Returns 1 on success, 0 if memory could not be allocated
uint8_t heap_insert(Heap *heap, const void *value);
void heap_pop(Heap *heap);
uint32_t heap_size(Heap *heap);
uint8_t heap_is_empty(Heap *heap);
void heap_free(Heap *heap);

#endif
