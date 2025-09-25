#include <stdlib.h>
#include <stdio.h>
#include "src/bptree.h"
#include "src/util.h"
#include "src/buffer_manager.h"

void print_callback(uint32_t key, uint64_t value) {
    printf("Key=%u, Value=%u\n", key, value);
}

int main() {

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