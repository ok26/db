#include <assert.h>
#include "../src/buffer_manager.h"

int main() {
    BufferManager *bm = buffer_manager_init("db/test.db");
    Page *root = buffer_manager_new_page(bm, LEAF);
    buffer_manager_free(bm);
    return 0;
}