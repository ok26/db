#include <stdlib.h>
#include <stdio.h>
#include "bptree.h"
#include "util.h"
#include "buffer_manager.h"

void print_callback(uint32_t key, uint32_t value) {
    printf("Key=%u, Value=%u\n", key, value);
}

int main() {
    BPTree *bpt = bpt_init();
    uint32_t data = 12;

    for (int i = 0; i < 1000000; i++) {
        bpt_insert(bpt, 
            (1103515245ULL * (unsigned long long)i + 12345ULL) & 0x7fffffff, 
            data);
    }

    bpt_range_query(bpt, 0, 100000, print_callback);
    printf("\n%u\n", bpt_height(bpt));

    bpt_free(bpt);
    buffer_manager_free();

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