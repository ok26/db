#include "../src/util.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

uint32_t pseudo_random(uint32_t v) {
    return (1103515245ULL * (unsigned long long)v + 12345ULL) & 0x7fffffff;
}

int main() {
    Map *m = rbt_init();

    for (int i = 0; i < 10000; i++) {
        uint32_t key = i;
        for (int j = 0; j < 10; j++) key = pseudo_random(key);
        uint32_t *v = malloc(sizeof(uint32_t));
        *v = pseudo_random(key);
        rbt_insert(m, key, (void*)v);
        uint32_t *res = rbt_get(m, key);
        assert(res);
        assert(*res == *v);
    }

    rbt_free(m);
    return 0;
}