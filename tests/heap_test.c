#include "../src/util.h"
#include <assert.h>

uint32_t data[10] = {10, 67, 100, 99, 100000, 15, 6, 2, 2, 1};
uint32_t expected_max[10] = {100000, 100, 99, 67, 15, 10, 6, 2, 2, 1};
uint32_t expected_min[10] = {1, 2, 2, 6, 10, 15, 67, 99, 100, 100000};

int main() {
    Heap *min_heap = new_minheap();
    Heap *max_heap = new_maxheap();

    for (int i = 0; i < 10; i++) {
        heap_insert(min_heap, (void*)&data[i]);
        heap_insert(max_heap, (void*)&data[i]);
    }

    for (int i = 0; i < 10; i++) {
        uint32_t v = *(uint32_t*)heap_top(max_heap);
        heap_pop(max_heap);
        assert(v == expected_max[i]);
        v = *(uint32_t*)heap_top(min_heap);
        heap_pop(min_heap);
        assert(v == expected_min[i]);
    }

    heap_free(min_heap);
    heap_free(max_heap);

    return 0;
}