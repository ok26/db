#include <stdint.h>
#include "consts.h"

#define PAGE_SIZE (off_t)4096

#define BPT_PAGE (uint8_t)0x0
#define DATA_PAGE (uint8_t)0x1
#define OVERFLOW_PAGE (uint8_t)0x2

#define DATA_PAGE_HEADER_SIZE 0x7
#define SLOT_FLAG_NONE (uint8_t)0x0
#define SLOT_FLAG_FREE (uint8_t)0x1
#define SLOT_FLAG_OVERFLOW (uint8_t)0x2
#define SLOT_FLAG_INVALID (uint8_t)0x80

// Page structure:
// bpt page
// 0x0: 1 byte for type: 0x0 for non-data page
// 0x1: 1 byte for is_leaf: 0x0 or 0x1
// 0x2: 2 bytes for num_keys
// (page_id is not stored on disk as it is implied by its location)
// 0x4 4 * num_keys bytes for keys[]
// 0x(4 + 4 * num_keys) (
//  4 * (num_keys + 1) for pointers[], if internal
//  6 * num_keys for pointers[] + slot, if leaf
// )
// (?) 4 bytes for next_page_id, leafs only
// 
// 
// Data page
// 0x0: 1 byte for type: 0x1 for data page
// 0x1: 2 bytes for num_slots
// 0x3: 2 bytes for free_space_start
// 0x5: 2 bytes for free_space_end
// 0x7: 5 * num_slots bytes for slot_directory
// (
//      Each slot-entry is of the form:
//      2 bytes for offset
//      2 bytes for length
//      1 byte for flags (FREE, OVERFLOWS and so on)
// )
// free_space_end - free_space_start bytes of free space
// 4096 - free_space_end for records
// (
//      Each slot is: type, value
//      1 byte for type except for varchar, then 2 bytes
//      small_int: 0x0
//      medium_int: 0x1
//      large_int: 0x2
//      float: 0x3
//      double: 0x4
// 
//      date: 0x5
//      datetime: 0x6
// 
//      bool: 0x7
//
//      char: 0x8
//      smallblob: 0x9
//      mediumblob: 0xA
//      largeblob: 0xB
//      smalltext: 0xC
//      mediumtext: 0xD
//      largetext: 0xE
//
//      varchar: identified if bit 0x80 is set, then
//      the remaining 7 bits and the next bytes tell
//      the size of the string with 15 bits
//
//      If the record is markes with OVERFLOW, it begins with 
//      4 bytes for page_id referencing the overflow-page
//
// )
// 

typedef struct PageHeader {
    uint8_t page_type;
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

    // RID = (page_id, slot_id)
    uint32_t page_ids[MAX_ENTRIES_LEAF + 1];
    uint16_t slot_ids[MAX_ENTRIES_LEAF + 1];
    uint32_t next_page_id;
} LeafPage;

// Check how pages are freed, might not free the actual internal/leaf
// Can be solved by passing specific item-freefunction
typedef struct Page {
    PageHeader header;
    union {InternalPage *internal; LeafPage *leaf;};
} Page;


// All offsets start from after the header, in other words
// offset=x is located at (data + x) and free_space_end
// is initially PAGE_SIZE - DATA_PAGE_HEADER_SIZE. It follows that
// records start at (data + free_space_end) and slot_directory at
// (data)
typedef struct DataPage {
    uint8_t page_type;
    uint32_t page_id;
    uint16_t num_slots;
    uint16_t free_space_start;
    uint16_t free_space_end;
    uint8_t *data;
} DataPage;

// Probably needs length aswell
typedef struct OverflowPage {
    uint8_t *data;
    uint32_t next_page;
} OverflowPage;

uint16_t bytes_to_u16_be(uint8_t b[2]);
uint32_t bytes_to_u32_be(uint8_t b[4]);
uint64_t bytes_to_u64_be(uint8_t b[8]);
void u16_to_bytes_be(uint8_t b[2], uint16_t v);
void u32_to_bytes_be(uint8_t b[4], uint32_t v);
void u64_to_bytes_be(uint8_t b[8], uint64_t v);

void write_page_to_db(char *db_file_path, void *page);
void *read_page_from_db(char *db_file_path, uint32_t page_id);