#include <stdint.h>
#include <stddef.h>

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
Stack *stack_clone(Stack *stack);

typedef struct RBTree RBTree;
RBTree *rbt_init();
void rbt_insert(RBTree *rbt, uint32_t key, void *value, size_t size);
void rbt_delete(RBTree *rbt, uint32_t key, size_t size);
void *rbt_get(RBTree *rbt, uint32_t key);
void rbt_free(RBTree *rbt);
uint32_t rbt_size(RBTree *rbt);
void rbt_clear(RBTree *rbt);
uint8_t rbt_verify_tree(RBTree *rbt);
void rbt_print(RBTree *rbt);

typedef RBTree Map;

typedef struct Heap Heap;
typedef uint8_t (*cmpf)(void*, void*);
Heap *new_heap(cmpf cmp, size_t type_size);
Heap *new_maxheap();
Heap *new_minheap();
void *heap_top(Heap *heap);
void heap_insert(Heap *heap, void *value);
void heap_pop(Heap *heap);
uint32_t heap_size(Heap *heap);
uint8_t heap_is_empty(Heap *heap);
void heap_free(Heap *heap);