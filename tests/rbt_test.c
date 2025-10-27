#include "../src/util.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

void insert_and_delete_test() {
    Map *m = rbt_init();
    int v = 10;
    rbt_insert(m, 200, &v, sizeof(int)); v++; assert(rbt_verify_tree(m));
    rbt_insert(m, 270, &v, sizeof(int)); v++; assert(rbt_verify_tree(m));
    rbt_insert(m, 221, &v, sizeof(int)); v++; assert(rbt_verify_tree(m));
    rbt_insert(m, 230, &v, sizeof(int)); v++; assert(rbt_verify_tree(m));
    rbt_insert(m, 1, &v, sizeof(int)); v++; assert(rbt_verify_tree(m));
    rbt_insert(m, 20000000, &v, sizeof(int)); assert(rbt_verify_tree(m));
    int *res;
    res = rbt_get(m, 200); assert(res); assert(*res == 10);
    res = rbt_get(m, 221); assert(res); assert(*res == 12);
    res = rbt_get(m, 230); assert(res); assert(*res == 13);
    res = rbt_get(m, 1); assert(res); assert(*res == 14);
    res = rbt_get(m, 20000000); assert(res); assert(*res == 15);
    res = rbt_get(m, 270); assert(res); assert(*res == 11);

    rbt_delete(m, 200, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 200); assert(!res);
    rbt_delete(m, 20000000, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 20000000); assert(!res);
    rbt_delete(m, 221, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 221); assert(!res);
    rbt_delete(m, 230, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 230); assert(!res);
    rbt_delete(m, 1, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 1); assert(!res);
    rbt_delete(m, 270, sizeof(int)); assert(rbt_verify_tree(m));
    res = rbt_get(m, 270); assert(!res);

    rbt_free(m);
}

uint32_t pseudo_random(uint32_t v) {
    return (1103515245ULL * (unsigned long long)v + 12345ULL) & 0x7fffffff;
}

typedef struct TestStruct {
    uint32_t field1;
    uint8_t field2;
    uint16_t field3;
} TestStruct;

void large_insert_test() {
    Map *m = rbt_init();

    for (int i = 0; i < 10000; i++) {   
        uint32_t key = i;
        for (int j = 0; j < 10; j++) key = pseudo_random(key);
        uint32_t v = pseudo_random(key);
        rbt_insert(m, key, &v, sizeof(uint32_t));
        uint32_t *res = rbt_get(m, key);
        assert(res);
        assert(*res == v);
    }

    rbt_clear(m);

    TestStruct *p = malloc(sizeof(TestStruct));
    *p = (TestStruct){
        28734651,
        200,
        12637
    };

    rbt_insert(m, 10, &p, sizeof(TestStruct*));
    void **res = rbt_get(m, 10);
    assert(res);
    assert(((TestStruct*)*res)->field1 == p->field1);
    assert(((TestStruct*)*res)->field2 == p->field2);
    assert(((TestStruct*)*res)->field3 == p->field3);
    
    free(p);
    rbt_free(m);
}

int main() {
    insert_and_delete_test();
    large_insert_test();
    return 0;
}