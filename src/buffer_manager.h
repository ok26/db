#include <stdint.h>

#define MAX_CHILDREN 512
#define MIN_CHILDREN (MAX_CHILDREN + 1) / 2
#define MAX_KEYS MAX_CHILDREN - 1

#define MAX_ENTRIES_LEAF 340
#define MIN_ENTRIES_LEAF (MAX_ENTRIES_LEAF + 1) / 2

#define INTERNAL 0
#define LEAF 1

typedef struct PageHeader {
    uint32_t page_id;
    uint16_t num_keys;
    uint8_t is_leaf;
} PageHeader;

typedef struct InternalPage {
    uint32_t keys[MAX_KEYS + 1];
    uint32_t children[MAX_CHILDREN + 1];
} InternalPage;

typedef struct LeafPage {
    uint32_t keys[MAX_ENTRIES_LEAF + 1];
    uint64_t rids[MAX_ENTRIES_LEAF + 1]; // (page_id = rid[..32], slot_id = rid[32..])
    uint32_t next_page_id;
} LeafPage;

typedef struct Page {
    PageHeader header;
    union {InternalPage *internal; LeafPage *leaf};
} Page;

// Page structure:
// Non-data page
// 1 byte for type: 0x0 for non-data page
// 1 byte for is_leaf: 0x0 or 0x1
// 2 bytes for num_keys
// (page_id is not stored on disk as it is implied by its location)
// 4 * num_keys bytes for keys[]
// (
//  4 * (num_keys + 1) for pointers[], if internal
//  6 * num_keys for pointers[] + slot, if leaf
// )
// 4 bytes for next_page_id, leafs only
// 
// 
// Data page
// 1 byte for type: 0x1 for data page
// Followed by slots of data
// Each slot is: type, value
// 1 byte is type except for strings, then 2 bytes
// small_int: 0x0
// medium_int: 0x1
// large_int: 0x2
// float: 0x3
// double: 0x4
// 
// date: 0x5
// datetime: 0x6
// 
// bool: 0x7
//
// char: 0x8
// smallblob: 0x9
// mediumblob: 0xA
// largeblob: 0xB
// smalltext: 0xC
// mediumtext: 0xD
// largetext: 0xE
//
// varchar: identified if bit 0x80 is set, then
// the remaining 7 bits and the next bytes tell
// the size of the string with 15 bits
// 

typedef struct BufferManager BufferManager;
BufferManager *buffer_manager_init(char *db_file_path);
Page *buffer_manager_new_page(BufferManager *bm, uint8_t is_leaf);
Page *buffer_manager_get_page(BufferManager *bm, uint32_t page_id);
void buffer_manager_free(BufferManager *bm);
uint64_t buffer_manager_request_slot(size_t size);