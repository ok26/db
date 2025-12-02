#include <stdlib.h>

typedef struct FreeDataPage {
    uint16_t free_space;
    uint32_t page_id;
} FreeDataPage;

// Indexed heap
typedef struct FreePageHeap FreePageHeap;
FreePageHeap *new_fph();
void fph_free(FreePageHeap *fph);
FreeDataPage *fph_top(FreePageHeap *fph);
void fph_insert(FreePageHeap *fph, FreeDataPage *value);
void fph_pop(FreePageHeap *fph);
uint32_t fph_size(FreePageHeap *fph);
uint8_t fph_empty(FreePageHeap *fph);
FreeDataPage *fph_get_by_pageid(FreePageHeap *fph, uint32_t page_id);
void fph_remove_by_pageid(FreePageHeap *fph, uint32_t page_id);