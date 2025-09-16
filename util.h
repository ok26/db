#include <stdint.h>

uint32_t upper_bound(uint32_t *arr, uint32_t len, uint32_t v);
uint32_t lower_bound(uint32_t *arr, uint32_t len, uint32_t v);
uint32_t *clone_u32(uint32_t *src);

typedef struct Stack {
    uint32_t *stack;
    uint32_t size;
    uint32_t resvsize;
} Stack;

Stack *stack_init();
void stack_free(Stack *stack);
uint32_t stack_top(Stack *stack);
void stack_pop(Stack *stack);
void stack_push(Stack *stack, uint32_t value);
void stack_clear(Stack *stack);
uint8_t stack_is_empty(Stack *stack);