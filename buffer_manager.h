#include "bptree.h"

#define MAX_CHILDREN 200
#define MIN_CHILDREN (MAX_CHILDREN + 1) / 2
#define MAX_KEYS MAX_CHILDREN - 1

typedef struct Page {
    uint32_t page_id;
    uint8_t is_leaf;
    uint16_t num_keys;
    uint32_t keys[MAX_KEYS + 1];
    uint32_t pointers[MAX_CHILDREN + 1];
    uint32_t next_page_id;
} Page;

Page *buffer_manager_new_page(uint8_t is_leaf);
Page *buffer_manager_get_page(uint32_t page_id);
void buffer_manager_free();