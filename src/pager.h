#ifndef PAGER_H
#define PAGER_H

#include <stdint.h>
#include <stddef.h>
#include "common.h"

// The pager is the lowest layer. It knows the on-disk layout of every page
// type and converts between the raw bytes of a page and the in-memory
// structs below. It does no caching, that is the job of the buffer manager.
//
// All integers are stored big-endian. Offsets are given in bytes from the
// start of the page.
//
// Page 0: database header (only one per file)
// 0x00: 8 bytes magic string DB_MAGIC
// 0x08: 4 bytes for page_size (has to equal PAGE_SIZE)
// 0x0C: 4 bytes for page_count, number of pages in the file incl. the header
// 0x10: 4 bytes for freelist_head, first unused page (0 if none)
// 0x14: 4 bytes for freelist_count, number of unused pages
// 0x18: 4 * DB_META_COUNT bytes for meta[], free for use by higher layers
//
// Every other page starts with 1 byte for the page type.
//
// bpt page (PAGE_TYPE_BPT)
// 0x0: 1 byte for type
// 0x1: 1 byte for is_leaf: 0x0 or 0x1
// 0x2: 2 bytes for num_keys
// (page_id is not stored on disk as it is implied by its location)
// 0x4: 4 * num_keys bytes for keys[]
// 0x(4 + 4 * num_keys) (
//  4 * (num_keys + 1) for children[] page ids, if internal
//  6 * num_keys for (page_id, slot_id) record ids, if leaf
// )
// 4 bytes for next_page_id, leafs only
//
// Data page (PAGE_TYPE_DATA), a slotted page holding records
// 0x00: 1 byte for type
// 0x01: 1 byte for flags (DATA_PAGE_FLAG_IN_FREE_LIST)
// 0x02: 2 bytes for occupied_slots, number of live records
// 0x04: 2 bytes for free_space_start, end of the slot directory
// 0x06: 2 bytes for free_space_end, start of the record area
// 0x08: 2 bytes for fragmented_bytes, space in holes left by deleted records
// 0x0A: 4 bytes for prev_free_page \ doubly linked list of data pages
// 0x0E: 4 bytes for next_free_page / that have free space (record_manager)
// 0x12: DATA_PAGE_DATA_SIZE bytes of data (
//      slot directory growing upwards from offset 0, 5 bytes per slot:
//          2 bytes for offset
//          2 bytes for length
//          1 byte for flags (FREE, OVERFLOW)
//      free_space_end - free_space_start bytes of free space
//      records growing downwards from the end of the page
// )
// All offsets in the data region start from after the header, in other
// words offset=x is located at (data + x).
//
// A record that is too large to fit in a data page is stored in a chain of
// overflow pages. Its slot is then marked with SLOT_FLAG_OVERFLOW and holds
// an 8 byte stub instead of the record:
//      4 bytes for page_id of the first overflow page
//      4 bytes for the total length of the record
//
// Overflow page (PAGE_TYPE_OVERFLOW)
// 0x0: 1 byte for type
// 0x1: 4 bytes for next_page_id (0 for the last page of a chain)
// 0x5: 2 bytes for length, bytes used in this page
// 0x7: OVERFLOW_PAGE_DATA_SIZE bytes of data
//
// Free page (PAGE_TYPE_FREE), a page that is not in use and can be reused
// 0x0: 1 byte for type
// 0x1: 4 bytes for next_free_page (0 for the last page of the freelist)
//
// Planned format of records once the VM is implemented (not used yet,
// records are opaque byte strings at this layer):
//      Each value is: type, value
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

// Type 0 is never written, a page full of zeroes is therefore detected as corrupt
#define PAGE_TYPE_INVALID  (uint8_t)0x0
#define PAGE_TYPE_BPT      (uint8_t)0x1
#define PAGE_TYPE_DATA     (uint8_t)0x2
#define PAGE_TYPE_OVERFLOW (uint8_t)0x3
#define PAGE_TYPE_FREE     (uint8_t)0x4

// Database header
#define DB_MAGIC "DBFILEv1"
#define DB_MAGIC_SIZE 8
#define DB_META_COUNT 16
#define DB_HEADER_SIZE (0x18 + 4 * DB_META_COUNT)

// B+tree pages
#define BPT_INTERNAL 0
#define BPT_LEAF 1
#define BPT_PAGE_HEADER_SIZE 0x4
// Physical capacities, i.e. how many keys can fit in one page
#define BPT_INTERNAL_CAPACITY ((PAGE_SIZE - BPT_PAGE_HEADER_SIZE - 4) / 8)
#define BPT_LEAF_CAPACITY ((PAGE_SIZE - BPT_PAGE_HEADER_SIZE - 4) / 10)

// The order of the tree. It defaults to the physical capacity but can be
// lowered at compile time (-DBPT_MAX_CHILDREN=4 -DBPT_MAX_ENTRIES_LEAF=3) to
// get deep trees with few keys, which the tests use to exercise splits and
// merges on every level.
#ifndef BPT_MAX_CHILDREN
#define BPT_MAX_CHILDREN (BPT_INTERNAL_CAPACITY + 1)
#endif
#ifndef BPT_MAX_ENTRIES_LEAF
#define BPT_MAX_ENTRIES_LEAF BPT_LEAF_CAPACITY
#endif

#define MAX_CHILDREN (BPT_MAX_CHILDREN)
#define MIN_CHILDREN ((MAX_CHILDREN + 1) / 2)
#define MAX_KEYS (MAX_CHILDREN - 1)

#define MAX_ENTRIES_LEAF (BPT_MAX_ENTRIES_LEAF)
#define MIN_ENTRIES_LEAF ((MAX_ENTRIES_LEAF + 1) / 2)

_Static_assert(MAX_KEYS <= BPT_INTERNAL_CAPACITY, "internal node does not fit in a page");
_Static_assert(MAX_ENTRIES_LEAF <= BPT_LEAF_CAPACITY, "leaf node does not fit in a page");
_Static_assert(MAX_CHILDREN >= 3, "internal nodes need at least 3 children");
_Static_assert(MAX_ENTRIES_LEAF >= 2, "leaves need at least 2 entries");

// Data pages
#define DATA_PAGE_HEADER_SIZE 0x12
#define DATA_PAGE_DATA_SIZE (PAGE_SIZE - DATA_PAGE_HEADER_SIZE)
#define DATA_PAGE_FLAG_IN_FREE_LIST (uint8_t)0x1

#define SLOT_ENTRY_SIZE 0x5
#define SLOT_FLAG_NONE (uint8_t)0x0
#define SLOT_FLAG_FREE (uint8_t)0x1
#define SLOT_FLAG_OVERFLOW (uint8_t)0x2
#define OVERFLOW_STUB_SIZE 8

// Overflow pages
#define OVERFLOW_PAGE_HEADER_SIZE 0x7
#define OVERFLOW_PAGE_DATA_SIZE (PAGE_SIZE - OVERFLOW_PAGE_HEADER_SIZE)

typedef struct DBHeader {
    uint32_t page_count;
    uint32_t freelist_head;
    uint32_t freelist_count;
    uint32_t meta[DB_META_COUNT];
} DBHeader;

// Record id, the location of a record: RID = (page_id, slot_id)
typedef struct RID {
    uint32_t page_id;
    uint16_t slot_id;
} RID;

// Both node types have room for one key more than the max, so that a node
// can overflow temporarily before it is split.
typedef struct InternalPage {
    uint32_t keys[MAX_KEYS + 1];
    uint32_t children[MAX_CHILDREN + 1];
} InternalPage;

typedef struct LeafPage {
    uint32_t keys[MAX_ENTRIES_LEAF + 1];
    RID rids[MAX_ENTRIES_LEAF + 1];
    uint32_t next_page_id;
} LeafPage;

typedef struct BPTPage {
    uint8_t is_leaf;
    uint16_t num_keys;
    union {
        InternalPage internal;
        LeafPage leaf;
    };
} BPTPage;

// The slot directory and the records are kept in their on-disk format in
// data[] and are accessed through helpers in record_manager.c
typedef struct DataPage {
    uint8_t flags;
    uint16_t occupied_slots;
    uint16_t free_space_start;
    uint16_t free_space_end;
    uint16_t fragmented_bytes;
    uint32_t prev_free_page;
    uint32_t next_free_page;
    uint8_t data[DATA_PAGE_DATA_SIZE];
} DataPage;

typedef struct SlotEntry {
    uint16_t offset;
    uint16_t length;
    uint8_t flags;
} SlotEntry;

typedef struct OverflowPage {
    uint32_t next_page_id;
    uint16_t length;
    uint8_t data[OVERFLOW_PAGE_DATA_SIZE];
} OverflowPage;

typedef struct FreePage {
    uint32_t next_free_page;
} FreePage;

// In-memory representation of any page except the header. page_type
// decides which member of the union is valid.
typedef struct Page {
    uint32_t page_id;
    uint8_t page_type;
    union {
        BPTPage bpt;
        DataPage data;
        OverflowPage overflow;
        FreePage free;
    };
} Page;

uint16_t bytes_to_u16_be(const uint8_t b[2]);
uint32_t bytes_to_u32_be(const uint8_t b[4]);
void u16_to_bytes_be(uint8_t b[2], uint16_t v);
void u32_to_bytes_be(uint8_t b[4], uint32_t v);

// Clears a page and initializes it as an empty page of the given type
void page_init(Page *page, uint32_t page_id, uint8_t page_type);

// Access to the slot directory of a data page
uint16_t data_page_slot_count(const DataPage *page);
SlotEntry data_page_get_slot(const DataPage *page, uint16_t slot_id);
void data_page_set_slot(DataPage *page, uint16_t slot_id, SlotEntry slot);

// Conversion between the in-memory structs and the on-disk format. Both
// validate the page and return DB_ERR_INVALID (encode) or DB_ERR_CORRUPT
// (decode) if it is not consistent.
DBStatus page_encode(const Page *page, uint8_t buffer[PAGE_SIZE]);
DBStatus page_decode(const uint8_t buffer[PAGE_SIZE], uint32_t page_id, Page *page);
void header_encode(const DBHeader *header, uint8_t buffer[PAGE_SIZE]);
DBStatus header_decode(const uint8_t buffer[PAGE_SIZE], DBHeader *header);

typedef struct Pager Pager;

// Opens (and creates if needed) the database file
DBStatus pager_open(const char *db_file_path, Pager **pager);
// Closes the file, does not flush anything
void pager_close(Pager *pager);
// Size of the file in bytes
DBStatus pager_file_size(Pager *pager, uint64_t *size);
DBStatus pager_read_header(Pager *pager, DBHeader *header);
DBStatus pager_write_header(Pager *pager, const DBHeader *header);
DBStatus pager_read_page(Pager *pager, uint32_t page_id, Page *page);
DBStatus pager_write_page(Pager *pager, const Page *page);
// Makes sure everything written so far has reached the disk
DBStatus pager_sync(Pager *pager);

#endif
