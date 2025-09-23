#include "../src/util.h"
#include <assert.h>

uint32_t data[10] = {10, 67, 100, 99, 100000, 15, 6, 2, 2, 1};
uint32_t expected_max[11] = {100000, 100, 99, 67, 15, 10, 6, 2, 2, 1, 0};
uint32_t expected_min[11] = {1, 2, 2, 6, 10, 15, 67, 99, 100, 100000, 0};

int main() {
    Heap *min_heap = new_minheap();
    Heap *max_heap = new_maxheap();

    for (int i = 0; i < 10; i++) {
        heap_insert(min_heap, data[i]);
        heap_insert(max_heap, data[i]);
    }

    for (int i = 0; i < 11; i++) {
        uint32_t v = heap_top(max_heap);
        heap_pop(max_heap);
        assert(v == expected_max[i]);
        v = heap_top(min_heap);
        heap_pop(min_heap);
        assert(v == expected_min[i]);
    }

    heap_free(min_heap);
    heap_free(max_heap);

    return 0;
}