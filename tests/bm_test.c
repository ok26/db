#include <assert.h>
#include <stdio.h>
#include "../src/buffer_manager.h"

int main() {
    BufferManager *bm = buffer_manager_init("db/test.db");

    uint32_t v1 = 12345678;
    RID rid1 = buffer_manager_request_slot(bm, sizeof(uint32_t), &v1);
    void *res = buffer_manager_get_data(bm, rid1);
    assert(res);
    assert(*(uint32_t*)res == v1);

    uint32_t v2 = 15;
    RID rid2 = buffer_manager_request_slot(bm, sizeof(uint32_t), &v2);
    res = buffer_manager_get_data(bm, rid2);
    assert(res);
    assert(*(uint32_t*)res == v2);
    res = buffer_manager_get_data(bm, rid1);
    assert(res);
    assert(*(uint32_t*)res == v1);

    Page *page = buffer_manager_new_bpt_page(bm, LEAF);
    assert(page);

    buffer_manager_flush_cache(bm);
    res = buffer_manager_get_data(bm, rid2);
    assert(res);
    assert(*(uint32_t*)res == v2);
    res = buffer_manager_get_data(bm, rid1);
    assert(res);
    assert(*(uint32_t*)res == v1);

    res = buffer_manager_get_page(bm, 2);
    assert(res);

    buffer_manager_free(bm);
    return 0;
}