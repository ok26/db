#include "buffer_manager.h"
#include <stdint.h>

typedef struct BPTree BPTree;

BPTree *bpt_new(BufferManager *bm);
BPTree *bpt_read(BufferManager *bm, uint32_t root_page_id);
void bpt_free(BPTree *bpt);
uint32_t bpt_height(BPTree *bpt);
void bpt_insert(BPTree *bpt, uint32_t key, void *data, size_t size);
uint64_t bpt_get(BPTree *bpt, uint32_t key);
void bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high,
    void (*callback)(uint32_t key, uint64_t value));
// void bpt_print(BPTree *bpt);