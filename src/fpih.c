#include "fpih.h"

#include <stdio.h>
#include <stdint.h>

// Free-Page Indexed Heap
// This should just be made general as an indexed heap and moved into
// util, just cannot be bothered to do that right now

#define MAX_READ_FREE_DATA_PAGES 32768
#define HEAP_INITIAL_SIZE 0x8
#define TYPE_SIZE sizeof(FreeDataPage)

typedef struct HashMapEntry {
    uint32_t page_id;
    uint16_t heap_idx;
} HashMapEntry;

struct FreePageHeap {
    // Heap
    void *data;
    uint32_t heap_size;
    uint32_t heap_resvsize;

    // Hashmap
    uint32_t hash_map_size;
    uint16_t *hash_slot_depth;
    uint16_t *hash_slot_resv_size;
    HashMapEntry **hash_map;
};

uint8_t free_page_greater(FreeDataPage *a, FreeDataPage *b) {
    return a->free_space > b->free_space;
}

#define HASH_SLOT_DEFAULT_SIZE 2

FreePageHeap *new_fph() {

    FreePageHeap *fph = malloc(sizeof(FreePageHeap));
    fph->data = malloc(TYPE_SIZE * HEAP_INITIAL_SIZE);
    fph->heap_size = 0;
    fph->heap_resvsize = HEAP_INITIAL_SIZE;

    fph->hash_map_size = MAX_READ_FREE_DATA_PAGES * 2;
    fph->hash_slot_depth = calloc(fph->hash_map_size, sizeof(uint16_t));
    fph->hash_slot_resv_size = malloc(sizeof(uint16_t) * fph->hash_map_size);
    fph->hash_map = malloc(sizeof(HashMapEntry*) * fph->hash_map_size);
    for (int i = 0; i < fph->hash_map_size; i++) {
        fph->hash_slot_resv_size[i] = 2;
        fph->hash_map[i] = malloc(sizeof(HashMapEntry) * fph->hash_slot_resv_size[i]);
    }

    return fph;
}

void fph_free(FreePageHeap *fph) {
    free(fph->data);

    for (int i = 0; i < fph->hash_map_size; i++) {
        free(fph->hash_map[i]);
    }
    free(fph->hash_map);
    free(fph->hash_slot_depth);
    free(fph->hash_slot_resv_size);

    free(fph);
}

uint32_t hash_int(uint32_t x, uint32_t mod) {
    uint64_t xx = x;
    xx ^= (uint64_t)(xx >> 16);
    xx *= (uint64_t)0x7feb352;
    xx ^= (uint64_t)(xx >> 15);
    xx *= (uint64_t)0x846ca68b;
    xx ^= (uint64_t)(xx >> 16);
    return xx % mod;
}

void fph_insert_map(FreePageHeap *fph, uint32_t page_id, uint16_t heap_idx) {
    uint32_t hash_idx = hash_int(page_id, fph->hash_map_size);
    HashMapEntry entry = {
        page_id,
        heap_idx
    };
    if (fph->hash_slot_depth[hash_idx] < fph->hash_slot_resv_size[hash_idx]) {
        fph->hash_map[hash_idx][fph->hash_slot_depth[hash_idx]] = entry;
        fph->hash_slot_depth[hash_idx]++;
    }
    else {
        fph->hash_slot_resv_size[hash_idx] *= 2;
        fph->hash_map[hash_idx] = 
            realloc(fph->hash_map[hash_idx], fph->hash_slot_resv_size[hash_idx] * sizeof(HashMapEntry));
        fph->hash_map[hash_idx][fph->hash_slot_depth[hash_idx]] = entry;
        fph->hash_slot_depth[hash_idx]++;
    }
}

uint32_t fph_ileftchild(uint32_t i) {
    return 2 * i + 1;
}

uint32_t fph_irightchild(uint32_t i) {
    return 2 * i + 2;
}

uint32_t fph_iparent(uint32_t i) {
    if (i == 0) {
        return 0;
    }
    return (i - 1) / 2;
}

FreeDataPage *fph_datai(FreePageHeap *fph, uint32_t i) {
    return (FreeDataPage*)(fph->data + i * TYPE_SIZE);
}

void swap_heap(FreeDataPage *a, FreeDataPage *b) {
    FreeDataPage tmp = *a;
    *a = *b;
    *b = tmp;
}

void swap_map(FreePageHeap *fph, FreeDataPage *a, FreeDataPage *b) {
    uint32_t hash_idx1 = hash_int(a->page_id, fph->hash_map_size);
    uint16_t *heap_idx_a = NULL;
    for (int i = 0; i < fph->hash_slot_depth[hash_idx1]; i++) {
        if (fph->hash_map[hash_idx1][i].page_id == a->page_id) {
            heap_idx_a = &fph->hash_map[hash_idx1][i].heap_idx;
            break;
        }
    }

    if (!heap_idx_a) {
        printf("Could not find heapidx in hashmap\n");
        return;
    }

    uint32_t hash_idx2 = hash_int(b->page_id, fph->hash_map_size);
    uint16_t *heap_idx_b = NULL;

    for (int i = 0; i < fph->hash_slot_depth[hash_idx2]; i++) {
        if (fph->hash_map[hash_idx2][i].page_id == b->page_id) {
            heap_idx_b = &fph->hash_map[hash_idx2][i].heap_idx;
            break;
        }
    }

    if (!heap_idx_b) {
        printf("Could not find heapidx in hashmap\n");
        return;
    }

    uint16_t c = *heap_idx_a;
    *heap_idx_a = *heap_idx_b;
    *heap_idx_b = c;
}

void map_delete(FreePageHeap *fph, uint32_t page_id) {
    uint32_t hash_idx = hash_int(page_id, fph->hash_map_size);

    int32_t del_idx = -1;
    for (int i = 0; i < fph->hash_slot_depth[hash_idx]; i++) {
        if (fph->hash_map[hash_idx][i].page_id == page_id) {
            del_idx = i;
            break;
        }
    }

    if (del_idx == -1) {
        return;
    }

    for (int i = del_idx + 1; i < fph->hash_slot_depth[hash_idx]; i++) {
        fph->hash_map[hash_idx][i - 1] = fph->hash_map[hash_idx][i];
    }
    fph->hash_slot_depth[hash_idx]--;
}

void map_update(FreePageHeap *fph, uint32_t page_id, uint32_t heap_idx) {
    uint32_t hash_idx = hash_int(page_id, fph->hash_map_size);
    for (int i = 0; i < fph->hash_slot_depth[hash_idx]; i++) {
        if (fph->hash_map[hash_idx][i].page_id == page_id) {
            fph->hash_map[hash_idx][i].heap_idx = heap_idx;
            break;
        }
    }
}

void fph_sift_up(FreePageHeap *fph, uint32_t start) {
    uint32_t root = start;
    while (root != 0) {
        uint32_t parent = fph_iparent(root);
        if (fph_datai(fph, root)->free_space > fph_datai(fph, parent)->free_space) {
            swap_map(fph, fph_datai(fph, root), fph_datai(fph, parent));
            swap_heap(fph_datai(fph, root), fph_datai(fph, parent));
            root = parent;
        }
        else {
            break;
        }
    }
}

void fph_sift_down(FreePageHeap *fph, uint32_t start, uint32_t end) {
    uint32_t root = start;
    while (fph_ileftchild(root) < end) {
        uint32_t child = fph_ileftchild(root);
        if (child + 1 < end && 
                fph_datai(fph, child + 1)->free_space > fph_datai(fph, child)->free_space) {
            child ++;
        }

        if (fph_datai(fph, root)->free_space < fph_datai(fph, child)->free_space) {
            swap_map(fph, fph_datai(fph, root), fph_datai(fph, child));
            swap_heap(fph_datai(fph, root), fph_datai(fph, child));
            root = child;
        }
        else {
            break;
        }
    }
}

FreeDataPage *fph_top(FreePageHeap *fph) {
    if (fph->heap_size == 0) {
        return NULL;
    }

    return fph_datai(fph, 0);
}

void fph_insert(FreePageHeap *fph, FreeDataPage *value) {
    if (fph->heap_size == fph->heap_resvsize) {
        fph->heap_resvsize *= 2;
        fph->data = realloc(fph->data, fph->heap_resvsize * TYPE_SIZE);
    }

    void *dst = fph_datai(fph, fph->heap_size);
    *(FreeDataPage*)dst = *value;
    fph_insert_map(fph, value->page_id, fph->heap_size);
    fph->heap_size++;
    fph_sift_up(fph, fph->heap_size - 1);
}

void fph_pop(FreePageHeap *fph) {
    if (fph_empty(fph)) {
        return;
    }
    if (fph->heap_size == 1) {
        map_delete(fph, fph_datai(fph, 0)->page_id);
        fph->heap_size--;
        return;
    }

    FreeDataPage *top = fph_datai(fph, 0);
    FreeDataPage *bot = fph_datai(fph, fph->heap_size - 1);
    map_delete(fph, top->page_id);
    swap_heap(bot, top);
    map_update(fph, top->page_id, 0);
    fph->heap_size--;
    fph_sift_down(fph, 0, fph->heap_size);
}

uint32_t fph_size(FreePageHeap *fph) {
    return fph->heap_size;
}

uint8_t fph_empty(FreePageHeap *fph) {
    return fph->heap_size == 0;
}

FreeDataPage *fph_get_by_pageid(FreePageHeap *fph, uint32_t page_id) {
    uint32_t hash_idx = hash_int(page_id, fph->hash_map_size);
    for (int i = 0; i < fph->hash_slot_depth[hash_idx]; i++) {
        if (fph->hash_map[hash_idx][i].page_id == page_id) {
                    return fph_datai(fph, fph->hash_map[hash_idx][i].heap_idx);
        }
    }
    return NULL;
}

void fph_remove_by_pageid(FreePageHeap *fph, uint32_t page_id) {
    uint32_t hash_idx = hash_int(page_id, fph->hash_map_size);
    uint32_t idx = UINT32_MAX;
    for (int i = 0; i < fph->hash_slot_depth[hash_idx]; i++) {
        if (fph->hash_map[hash_idx][i].page_id == page_id) {
            idx = fph->hash_map[hash_idx][i].heap_idx;
            break;
        }
    }
    if (idx == UINT32_MAX) {
        return;
    }

    uint32_t last_idx = fph->heap_size - 1;
    if (idx == last_idx) {
        fph->heap_size--;
        map_delete(fph, page_id);
        return;
    }

    swap_map(fph, fph_datai(fph, idx), fph_datai(fph, last_idx));
    swap_heap(fph_datai(fph, idx), fph_datai(fph, last_idx));
    map_update(fph, fph_datai(fph, idx)->page_id, idx);
    fph->heap_size--;
    map_delete(fph, page_id);
    fph_sift_down(fph, idx, fph->heap_size);
}