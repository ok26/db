#include "../src/util.h"
#include "test.h"

static void test_push_pop(void) {
    Stack *stack = stack_init();
    CHECK(stack);
    CHECK(stack_is_empty(stack));

    for (int i = 0; i < 100; i++) {
        CHECK(stack_push(stack, (uint32_t)(i * i)));
    }
    CHECK(stack_size(stack) == 100);

    for (int i = 99; i >= 0; i--) {
        uint32_t v = stack_top(stack);
        stack_pop(stack);
        CHECK(v == (uint32_t)(i * i));
    }
    CHECK(stack_is_empty(stack));

    // Popping an empty stack does nothing
    stack_pop(stack);
    CHECK(stack_is_empty(stack));

    stack_free(stack);
}

static void test_clone(void) {
    Stack *stack = stack_init();
    for (uint32_t i = 0; i < 20; i++) CHECK(stack_push(stack, i));

    Stack *clone = stack_clone(stack);
    CHECK(clone);
    stack_clear(stack);
    CHECK(stack_is_empty(stack));

    CHECK(stack_size(clone) == 20);
    for (uint32_t i = 20; i-- > 0;) {
        CHECK(stack_top(clone) == i);
        stack_pop(clone);
    }

    stack_free(stack);
    stack_free(clone);
}

int main(void) {
    RUN_TEST(test_push_pop);
    RUN_TEST(test_clone);
    return 0;
}
