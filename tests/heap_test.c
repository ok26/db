#include "../src/util.h"
#include "test.h"

static uint32_t data[10] = {10, 67, 100, 99, 100000, 15, 6, 2, 2, 1};
static uint32_t expected_max[10] = {100000, 100, 99, 67, 15, 10, 6, 2, 2, 1};
static uint32_t expected_min[10] = {1, 2, 2, 6, 10, 15, 67, 99, 100, 100000};

static void test_min_max(void) {
    Heap *min_heap = new_minheap();
    Heap *max_heap = new_maxheap();

    for (int i = 0; i < 10; i++) {
        CHECK(heap_insert(min_heap, &data[i]));
        CHECK(heap_insert(max_heap, &data[i]));
    }
    CHECK(heap_size(min_heap) == 10);

    for (int i = 0; i < 10; i++) {
        uint32_t v = *(uint32_t*)heap_top(max_heap);
        heap_pop(max_heap);
        CHECK(v == expected_max[i]);
        v = *(uint32_t*)heap_top(min_heap);
        heap_pop(min_heap);
        CHECK(v == expected_min[i]);
    }
    CHECK(heap_is_empty(min_heap));
    CHECK(heap_top(min_heap) == NULL);

    heap_free(min_heap);
    heap_free(max_heap);
}

typedef struct Item {
    uint16_t priority;
    uint32_t id;
} Item;

static uint8_t item_lesser(const void *a, const void *b) {
    return ((const Item*)a)->priority < ((const Item*)b)->priority;
}

static void test_random_structs(void) {
    test_seed();
    Heap *heap = new_heap(item_lesser, sizeof(Item));
    uint32_t counts[1000] = {0};

    for (uint32_t i = 0; i < 20000; i++) {
        Item item = { (uint16_t)test_rand_below(1000), i };
        counts[item.priority]++;
        CHECK(heap_insert(heap, &item));
    }

    uint32_t last = UINT32_MAX;
    while (!heap_is_empty(heap)) {
        Item item = *(Item*)heap_top(heap);
        heap_pop(heap);
        CHECK(item.priority <= last);
        CHECK(counts[item.priority] > 0);
        counts[item.priority]--;
        last = item.priority;
    }
    for (int i = 0; i < 1000; i++) CHECK(counts[i] == 0);

    heap_free(heap);
}

int main(void) {
    RUN_TEST(test_min_max);
    RUN_TEST(test_random_structs);
    return 0;
}
