#include <stdlib.h>
#include <stdio.h>
#include "bptree.h"
#include "util.h"

void print_callback(uint32_t key, void *value) {
    printf("Key=%u, Value=%u\n", key, *(uint32_t*)value);
}

int main() {
    BPTree *bpt = init_bpt();
    void *data = malloc(sizeof(uint32_t));

    for (int i = 0; i < 10000000; i++) {
        insert(bpt, 
            (1103515245ULL * (unsigned long long)i + 12345ULL) & 0x7fffffff, 
            clone_u32(data));
    }

    range_query(bpt, 0, 100000, print_callback);
    printf("\n%u\n", height(bpt));

    free_bpt(bpt);

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