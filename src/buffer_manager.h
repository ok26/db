#include <stdint.h>
#include <stddef.h>
#include "pager.h"

typedef struct RID {
    uint32_t page_id;
    uint16_t slot_id;
} RID;

typedef struct BufferManager BufferManager;
BufferManager *buffer_manager_init(char *db_file_path);
Page *buffer_manager_new_page(BufferManager *bm, uint8_t is_leaf);
Page *buffer_manager_get_page(BufferManager *bm, uint32_t page_id);
void buffer_manager_free(BufferManager *bm);
RID buffer_manager_request_slot(size_t size);