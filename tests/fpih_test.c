#include "../src/fpih.h"
#include <assert.h>
#include <stdio.h>

int main() {
    FreePageHeap *fph = new_fph();
    assert(fph);
    // initially empty
    assert(fph_empty(fph));

    FreeDataPage a = {.free_space = 1000, .page_id = 1};
    FreeDataPage b = {.free_space = 500, .page_id = 2};
    FreeDataPage c = {.free_space = 1500, .page_id = 3};

    fph_insert(fph, &a);
    printf("after insert a: size=%u\n", fph_size(fph));
    fph_insert(fph, &b);
    printf("after insert b: size=%u\n", fph_size(fph));
    fph_insert(fph, &c);
    printf("after insert c: size=%u\n", fph_size(fph));

    assert(fph_size(fph) == 3);

    FreeDataPage *top = fph_top(fph);
    assert(top != NULL);

    assert(top->page_id == 3);

    FreeDataPage *got = fph_get_by_pageid(fph, 1);
    assert(got && got->page_id == 1 && got->free_space == 1000);

    fph_pop(fph);
    assert(fph_size(fph) == 2);

    fph_remove_by_pageid(fph, 1);
    assert(fph_size(fph) == 1);

    fph_pop(fph);
    assert(fph_empty(fph));

    fph_free(fph);

    return 0;
}
