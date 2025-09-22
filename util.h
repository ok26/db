#include <stdint.h>

uint32_t upper_bound(uint32_t *arr, uint32_t len, uint32_t v);
uint32_t lower_bound(uint32_t *arr, uint32_t len, uint32_t v);
uint32_t *clone_u32(uint32_t *src);

typedef struct Stack Stack;
Stack *stack_init();
void stack_free(Stack *stack);
uint32_t stack_top(Stack *stack);
void stack_pop(Stack *stack);
void stack_push(Stack *stack, uint32_t value);
void stack_clear(Stack *stack);
uint8_t stack_is_empty(Stack *stack);

typedef struct RBTree RBTree;
RBTree *rbt_init();
void rbt_insert(RBTree *rbt, uint32_t key, void *value);
void rbt_delete(RBTree *rbt, uint32_t key);
void *rbt_get(RBTree *rbt, uint32_t key);
void rbt_free(RBTree *rbt);

typedef RBTree Map;

typedef struct Heap Heap;
Heap new_maxheap();
Heap new_minheap();
uint32_t top(Heap *heap);
void insert(Heap *heap, uint32_t value);
void pop(Heap *heap);
size_t size(Heap *heap);
uint8_t is_empty(Heap *heap);
void free_heap(Heap *heap);