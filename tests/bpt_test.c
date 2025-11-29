#include "../src/bptree.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static int remaining_count;
static void count_cb(uint32_t key, void *data) {
    (void)data;
    printf("Remaining key: %u\n", key);
    remaining_count++;
}


uint32_t pseudo_random(uint32_t v) {
    return (1103515245ULL * (unsigned long long)v + 12345ULL) & 0x7fffffff;
}

void test_empty_db() {
    BufferManager *bm = buffer_manager_init("db/test.db");
    BPTree *bpt = bpt_new(bm);

    assert(bpt_height(bpt) == 1);

    for (int i = 0; i <= 50000; i++) {
        uint32_t key = pseudo_random(i);
        bpt_insert(bpt, key, &i, sizeof(int));
    }

    assert(bpt_height(bpt) == 2);
    printf("Finished inserting!\n");

    for (int i = 50000; i >= 0; i--) {
        uint32_t key = pseudo_random(i);
        uint32_t *v = bpt_get(bpt, key);
        assert(v);
        assert(*v == i);
    }

    buffer_manager_flush_cache(bm);
    assert(bpt_height(bpt) == 2);

    uint32_t fib1 = 0, fib2 = 1;
    while (fib1 <= 50000) {
        uint32_t key = pseudo_random(fib1);
        uint32_t *v = bpt_get(bpt, key);
        assert(v);
        assert(*v == fib1);
        fib2 = fib1 + fib2;
        fib1 = fib2 - fib1;
    }

    buffer_manager_free(bm);
    bpt_free(bpt);
}

void test_filled_db() {
    BufferManager *bm = buffer_manager_init("db/test.db");
    BPTree *bpt = bpt_read(bm, 4); // In this case page_id 4 will be the root

    assert(bpt_height(bpt) == 2);

    for (int i = 50000; i >= 0; i--) {
        uint32_t key = pseudo_random(i);
        uint32_t *v = bpt_get(bpt, key);
        assert(v);
        assert(*v == i);
    }

    uint32_t fib1 = 0, fib2 = 1;
    while (fib1 <= 50000) {
        uint32_t key = pseudo_random(fib1);
        uint32_t *v = bpt_get(bpt, key);
        assert(v);
        assert(*v == fib1);
        fib2 = fib1 + fib2;
        fib1 = fib2 - fib1;
    }
}

void test_with_deletion() {
    BufferManager *bm = buffer_manager_init("db/test.db");
    BPTree *bpt = bpt_new(bm);

    assert(bpt_height(bpt) == 1);

    // Generate random permutation of keys 1->50000
    int keys[50001];
    for (int i = 0; i <= 50000; i++) keys[i] = pseudo_random(i);

    for (int i = 0; i <= 50000; i++) {
        int v = i * i;
        bpt_insert(bpt, keys[i], &v, sizeof(int));
    }

    
    printf("Finished inserting!\n");
    bpt_verify_tree(bpt);
    srand(time(NULL));

    // Shuffle keys
    for (int i = 50000; i >= 1; i--) {
        int j = rand() % i;
        int tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
    }

    printf("Finished shuffling\n");

    for (int i = 0; i <= 50000; i++) {
        bpt_delete(bpt, keys[i]);
        fflush(stdout);
    }

    remaining_count = 0;
    bpt_range_query(bpt, 0, 0x7fffffff, count_cb);
    printf("Remaining keys after deletions: %d\n", remaining_count);

    assert(bpt_empty(bpt));

    buffer_manager_free(bm);
    bpt_free(bpt);
}

int main() {

    // test_filled_db can be used if test_empty_db has been used eariler
    // buffer_manager_flush_cache is used in test_empty_db either way

    // test_empty_db();
    // test_filled_db();
    test_with_deletion();
    return 0;
}