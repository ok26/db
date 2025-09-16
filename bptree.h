#include <stdint.h>

typedef struct BPTree BPTree;

BPTree *bpt_init();
void bpt_free(BPTree *bpt);
uint32_t bpt_height(BPTree *bpt);
void bpt_insert(BPTree *bpt, uint32_t key, uint32_t data);
uint32_t bpt_get(BPTree *bpt, uint32_t key);
void bpt_range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high,
    void (*callback)(uint32_t key, uint32_t value));
void bpt_print(BPTree *bpt);