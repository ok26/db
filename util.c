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
    fflush(stdout);
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