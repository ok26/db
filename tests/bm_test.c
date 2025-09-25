#include <assert.h>
#include "buffer_manager.h"

int main() {
    BufferManager *bm = buffer_manager_init("db/test.db");
    Page *root = buffer_manager_new_page(bm, 0);
    return 0;
}