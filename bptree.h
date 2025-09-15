#include <stdint.h>

typedef struct BPTreeNode BPTreeNode;
typedef struct BPTree BPTree;

BPTree *init_bpt();
void free_bpt(BPTree *bpt);
uint32_t height(BPTree *bpt);
void insert(BPTree *bpt, uint32_t key, void *data);
void *get(BPTree *bpt, uint32_t key);
void range_query(BPTree *bpt, uint32_t key_low, uint32_t key_high,
    void (*callback)(uint32_t key, void *value));
void print_tree(BPTree *bpt);