#include "../src/util.h"
#include <assert.h>

int main() {
    Stack *stack = stack_init();
    for (int i = 0; i < 100; i++) {
        stack_push(stack, i * i);
    }

    for (int i = 99; i >= 0; i--) {
        uint32_t v = stack_top(stack);
        stack_pop(stack);
        assert(v == i * i);
    }

    stack_free(stack);

    return 0;
}