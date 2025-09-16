#include <stdlib.h>
#include <stdio.h>
#include "buffer_manager.h"
#include "bptree.h"

Page *testing[1000024];
uint32_t test_size = 0;

Page *buffer_manager_new_page(uint8_t is_leaf) {
    Page *node = malloc(sizeof(Page));
    node->is_leaf = is_leaf;
    node->num_keys = 0;
    node->page_id = test_size;
    testing[test_size++] = node;
    return node;
}

Page *buffer_manager_get_page(uint32_t page_id) {
    if (page_id >= test_size) {
        printf("Wrong\n");        
    }
    return testing[page_id];
}

void buffer_manager_free() {
    for (int i = 0; i < test_size; i++) {
        free(testing[i]);
    }
}