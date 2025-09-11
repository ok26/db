#include <stdint.h>

typedef struct BPTreeNode BPTreeNode;
typedef struct BPTree BPTree;

BPTree *init_bpt();
void free_bpt(BPTree *bpt);
void insert(BPTree *bpt, uint32_t key, void *data);
void *get(BPTree *bpt, uint32_t key);
void print_tree(BPTree *bpt);