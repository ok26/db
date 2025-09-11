#include <stdlib.h>
#include <stdio.h>
#include "bptree.h"
#include "util.h"

int main() {
    BPTree *bpt = init_bpt();
    void *data = malloc(sizeof(uint32_t));
    *(uint32_t*)data = 12836741;
    insert(bpt, 52, data);
    print_tree(bpt); printf("\n");
    insert(bpt, 12, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 13, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 56, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 57, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 1, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 2, clone_u32(data));
    print_tree(bpt); printf("\n");
    insert(bpt, 5000000, clone_u32(data));
    print_tree(bpt); printf("\n");

    free_bpt(bpt);

    return 0;
}