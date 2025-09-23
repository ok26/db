#define MAX_CHILDREN 200
#define MIN_CHILDREN (MAX_CHILDREN + 1) / 2
#define MAX_KEYS MAX_CHILDREN - 1

// First version of a Page
// Will in the future exist different type of pages,
// this for example is an internal page but there
// will also be data-pages and freed-pages
typedef struct Page {
    uint32_t page_id;
    uint8_t is_leaf;
    uint16_t num_keys;
    uint32_t keys[MAX_KEYS + 1];
    uint32_t pointers[MAX_CHILDREN + 1];
    uint32_t next_page_id;
} Page;

typedef struct BufferManager BufferManager;
BufferManager *buffer_manager_init(char *db_file_path);
Page *buffer_manager_new_page(BufferManager *bm, uint8_t is_leaf);
Page *buffer_manager_get_page(BufferManager *bm, uint32_t page_id);
void buffer_manager_free(BufferManager *bm);