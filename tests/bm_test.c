#include <assert.h>
#include <stdio.h>
#include "../src/buffer_manager.h"

typedef struct TestStruct {
    uint32_t field1;
    uint8_t field2;
    uint16_t field3;
} TestStruct;

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
    Page *bpt_res = buffer_manager_get_page(bm, page->header.page_id);
    assert(bpt_res);

    buffer_manager_flush_cache(bm);
    res = buffer_manager_get_data(bm, rid2);
    assert(res);
    assert(*(uint32_t*)res == v2);
    res = buffer_manager_get_data(bm, rid1);
    assert(res);
    assert(*(uint32_t*)res == v1);

    res = buffer_manager_get_page(bm, 2);
    assert(res);

    TestStruct v3 = {
        28734651,
        200,
        12637
    };
    RID rid3 = buffer_manager_request_slot(bm, sizeof(TestStruct), &v3);
    res = buffer_manager_get_data(bm, rid3);
    assert(res);
    assert((*(TestStruct*)res).field1 == v3.field1);
    assert((*(TestStruct*)res).field2 == v3.field2);
    assert((*(TestStruct*)res).field3 == v3.field3);



    buffer_manager_free(bm);
    return 0;
}