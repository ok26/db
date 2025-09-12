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
    *(uint32_t*)data = 1;
    insert(bpt, 52, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 2;
    insert(bpt, 12, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 3;
    insert(bpt, 13, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 4;
    insert(bpt, 56, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 5;
    insert(bpt, 57, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 6;
    insert(bpt, 1, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 7;
    insert(bpt, 2, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 8;
    insert(bpt, 58, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 9;
    insert(bpt, 59, data);
    print_tree(bpt); printf("\n");
    data = clone_u32(data);
    *(uint32_t*)data = 10;
    insert(bpt, 3, data);
    print_tree(bpt); printf("\n");

    range_query(bpt, 1, 100, print_callback); printf("\n");
    range_query(bpt, 6, 57, print_callback);

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