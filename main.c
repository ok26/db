#include <stdlib.h>
#include <stdio.h>
#include "src/bptree.h"
#include "src/util.h"
#include "src/buffer_manager.h"

void print_callback(uint32_t key, uint32_t value) {
    printf("Key=%u, Value=%u\n", key, value);
}

int main() {
    Map *m = rbt_init();

    uint32_t *v = malloc(sizeof(uint32_t));
    for (int i = 0; i < 100; i++) {
        uint32_t key = (1103515245ULL * (unsigned long long)i + 12345ULL) & 0x7fffffff;
        *v = key;
        rbt_insert(m, key, (void*)v);
        void *vg = rbt_get(m, key);
        if (!vg) {
            printf("Could not retrieve key at iteration: {%d}\n", i);
        }
        else {
            printf("%d\n", *(uint32_t*)vg);
        }
        fflush(stdout);
        v = clone_u32(v);
    }
    
    rbt_free(m);

    return 0;
}

/*

max_children = m

max_keys_internal = m - 1

min_children = ceil(m / 2)

min_keys_internal = min_children - 1

max_entries_leaf = m - 1

min_entries_leaf = ceil((m - 1) / 2)

*/