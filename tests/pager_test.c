#include "../src/pager.h"
#include "test.h"

// Encodes a page, decodes it again and checks that the result encodes to
// exactly the same bytes
static void roundtrip(const Page *page, Page *decoded) {
    uint8_t buffer[PAGE_SIZE], again[PAGE_SIZE];
    CHECK_OK(page_encode(page, buffer));
    CHECK(buffer[0] == page->page_type);
    CHECK_OK(page_decode(buffer, page->page_id, decoded));
    CHECK(decoded->page_id == page->page_id);
    CHECK(decoded->page_type == page->page_type);
    CHECK_OK(page_encode(decoded, again));
    CHECK(memcmp(buffer, again, PAGE_SIZE) == 0);
}

static void test_endianness(void) {
    uint8_t b[4];
    u32_to_bytes_be(b, 0x01020304);
    CHECK(b[0] == 1 && b[1] == 2 && b[2] == 3 && b[3] == 4);
    CHECK(bytes_to_u32_be(b) == 0x01020304);
    u16_to_bytes_be(b, 0xBEEF);
    CHECK(b[0] == 0xBE && b[1] == 0xEF);
    CHECK(bytes_to_u16_be(b) == 0xBEEF);
}

static void test_bpt_leaf_roundtrip(void) {
    static Page page, decoded;
    page_init(&page, 7, PAGE_TYPE_BPT);
    page.bpt.is_leaf = BPT_LEAF;
    page.bpt.num_keys = MAX_ENTRIES_LEAF;
    for (uint32_t i = 0; i < MAX_ENTRIES_LEAF; i++) {
        page.bpt.leaf.keys[i] = i * 1000 + 0xAB000000u;
        page.bpt.leaf.rids[i].page_id = 0x10000 + i;
        page.bpt.leaf.rids[i].slot_id = (uint16_t)(i * 3);
    }
    page.bpt.leaf.next_page_id = 0xDEADBEEF;

    roundtrip(&page, &decoded);
    CHECK(decoded.bpt.is_leaf == BPT_LEAF);
    CHECK(decoded.bpt.num_keys == MAX_ENTRIES_LEAF);
    for (uint32_t i = 0; i < MAX_ENTRIES_LEAF; i++) {
        CHECK(decoded.bpt.leaf.keys[i] == page.bpt.leaf.keys[i]);
        CHECK(decoded.bpt.leaf.rids[i].page_id == page.bpt.leaf.rids[i].page_id);
        CHECK(decoded.bpt.leaf.rids[i].slot_id == page.bpt.leaf.rids[i].slot_id);
    }
    CHECK(decoded.bpt.leaf.next_page_id == 0xDEADBEEF);

    // A leaf with one entry too many cannot be written
    uint8_t buffer[PAGE_SIZE];
    page.bpt.num_keys = MAX_ENTRIES_LEAF + 1;
    CHECK_STATUS(page_encode(&page, buffer), DB_ERR_INVALID);
}

static void test_bpt_internal_roundtrip(void) {
    static Page page, decoded;
    page_init(&page, 3, PAGE_TYPE_BPT);
    page.bpt.is_leaf = BPT_INTERNAL;
    page.bpt.num_keys = MAX_KEYS;
    for (uint32_t i = 0; i < MAX_KEYS; i++) page.bpt.internal.keys[i] = i * 7 + 1;
    for (uint32_t i = 0; i <= MAX_KEYS; i++) page.bpt.internal.children[i] = 100 + i;

    roundtrip(&page, &decoded);
    CHECK(decoded.bpt.is_leaf == BPT_INTERNAL);
    CHECK(decoded.bpt.num_keys == MAX_KEYS);
    for (uint32_t i = 0; i < MAX_KEYS; i++) {
        CHECK(decoded.bpt.internal.keys[i] == i * 7 + 1);
    }
    for (uint32_t i = 0; i <= MAX_KEYS; i++) {
        CHECK(decoded.bpt.internal.children[i] == 100 + i);
    }
}

static void test_data_page_roundtrip(void) {
    static Page page, decoded;
    page_init(&page, 12, PAGE_TYPE_DATA);
    CHECK(page.data.free_space_end == DATA_PAGE_DATA_SIZE);

    // Two records by hand: "hello" in slot 0 and slot 1 free, then "abc" in slot 2
    DataPage *dp = &page.data;
    dp->free_space_end -= 5;
    memcpy(dp->data + dp->free_space_end, "hello", 5);
    SlotEntry s0 = { dp->free_space_end, 5, SLOT_FLAG_NONE };
    dp->free_space_end -= 3;
    memcpy(dp->data + dp->free_space_end, "abc", 3);
    SlotEntry s2 = { dp->free_space_end, 3, SLOT_FLAG_NONE };
    SlotEntry s1 = { 0, 0, SLOT_FLAG_FREE };
    dp->free_space_end -= 4; // A 4 byte hole left by a deleted record
    dp->fragmented_bytes = 4;
    data_page_set_slot(dp, 0, s0);
    data_page_set_slot(dp, 1, s1);
    data_page_set_slot(dp, 2, s2);
    dp->free_space_start = 3 * SLOT_ENTRY_SIZE;
    dp->occupied_slots = 2;
    dp->flags = DATA_PAGE_FLAG_IN_FREE_LIST;
    dp->prev_free_page = 5;
    dp->next_free_page = 9;

    roundtrip(&page, &decoded);
    DataPage *d = &decoded.data;
    CHECK(d->flags == DATA_PAGE_FLAG_IN_FREE_LIST);
    CHECK(d->occupied_slots == 2);
    CHECK(d->free_space_start == 15);
    CHECK(d->free_space_end == DATA_PAGE_DATA_SIZE - 12);
    CHECK(d->fragmented_bytes == 4);
    CHECK(d->prev_free_page == 5 && d->next_free_page == 9);
    CHECK(data_page_slot_count(d) == 3);
    SlotEntry slot = data_page_get_slot(d, 0);
    CHECK(slot.length == 5 && memcmp(d->data + slot.offset, "hello", 5) == 0);
    CHECK(data_page_get_slot(d, 1).flags == SLOT_FLAG_FREE);
    slot = data_page_get_slot(d, 2);
    CHECK(slot.length == 3 && memcmp(d->data + slot.offset, "abc", 3) == 0);
}

static void test_overflow_and_free_roundtrip(void) {
    static Page page, decoded;
    page_init(&page, 99, PAGE_TYPE_OVERFLOW);
    page.overflow.next_page_id = 100;
    page.overflow.length = OVERFLOW_PAGE_DATA_SIZE;
    fill_pattern(page.overflow.data, OVERFLOW_PAGE_DATA_SIZE, 99);
    roundtrip(&page, &decoded);
    CHECK(decoded.overflow.next_page_id == 100);
    CHECK(decoded.overflow.length == OVERFLOW_PAGE_DATA_SIZE);
    CHECK(check_pattern(decoded.overflow.data, OVERFLOW_PAGE_DATA_SIZE, 99));

    page_init(&page, 4, PAGE_TYPE_FREE);
    page.free.next_free_page = 17;
    roundtrip(&page, &decoded);
    CHECK(decoded.free.next_free_page == 17);
}

static void test_corrupt_pages(void) {
    static Page page;
    uint8_t buffer[PAGE_SIZE];

    // A page of zeroes (e.g. never written) is not a valid page
    memset(buffer, 0, PAGE_SIZE);
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // Unknown type
    buffer[0] = 0x77;
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // Leaf with too many keys
    memset(buffer, 0, PAGE_SIZE);
    buffer[0] = PAGE_TYPE_BPT;
    buffer[1] = BPT_LEAF;
    u16_to_bytes_be(&buffer[2], MAX_ENTRIES_LEAF + 1);
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // is_leaf has to be 0 or 1
    u16_to_bytes_be(&buffer[2], 1);
    buffer[1] = 2;
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // Data page whose slot points outside of the record area
    page_init(&page, 1, PAGE_TYPE_DATA);
    page.data.free_space_end -= 10;
    page.data.free_space_start = SLOT_ENTRY_SIZE;
    page.data.occupied_slots = 1;
    SlotEntry slot = { page.data.free_space_end, 10, SLOT_FLAG_NONE };
    data_page_set_slot(&page.data, 0, slot);
    CHECK_OK(page_encode(&page, buffer));
    CHECK_OK(page_decode(buffer, 1, &page));

    slot.offset = 0;
    data_page_set_slot(&page.data, 0, slot);
    CHECK_OK(page_encode(&page, buffer));
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // Data page where records and holes do not add up
    page_init(&page, 1, PAGE_TYPE_DATA);
    page.data.fragmented_bytes = 3;
    CHECK_OK(page_encode(&page, buffer));
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);

    // Overflow page with a too large length
    memset(buffer, 0, PAGE_SIZE);
    buffer[0] = PAGE_TYPE_OVERFLOW;
    u16_to_bytes_be(&buffer[5], OVERFLOW_PAGE_DATA_SIZE + 1);
    CHECK_STATUS(page_decode(buffer, 1, &page), DB_ERR_CORRUPT);
}

static void test_header(void) {
    DBHeader header = { 0 }, decoded;
    header.page_count = 1234;
    header.freelist_head = 77;
    header.freelist_count = 3;
    for (uint32_t i = 0; i < DB_META_COUNT; i++) header.meta[i] = i * i + 1;

    uint8_t buffer[PAGE_SIZE];
    header_encode(&header, buffer);
    CHECK(memcmp(buffer, DB_MAGIC, DB_MAGIC_SIZE) == 0);
    CHECK_OK(header_decode(buffer, &decoded));
    CHECK(decoded.page_count == 1234);
    CHECK(decoded.freelist_head == 77);
    CHECK(decoded.freelist_count == 3);
    for (uint32_t i = 0; i < DB_META_COUNT; i++) CHECK(decoded.meta[i] == i * i + 1);

    // Wrong magic
    buffer[0] ^= 0xFF;
    CHECK_STATUS(header_decode(buffer, &decoded), DB_ERR_CORRUPT);
    buffer[0] ^= 0xFF;

    // Freelist head outside of the file
    header.freelist_head = 5000;
    header_encode(&header, buffer);
    CHECK_STATUS(header_decode(buffer, &decoded), DB_ERR_CORRUPT);
}

static void test_file_io(void) {
    const char *path = test_db_path("pager_test");
    Pager *pager;
    CHECK_OK(pager_open(path, &pager));

    uint64_t size;
    CHECK_OK(pager_file_size(pager, &size));
    CHECK(size == 0);

    DBHeader header = { 0 };
    header.page_count = 4;
    CHECK_OK(pager_write_header(pager, &header));

    static Page page, read_back;
    for (uint32_t id = 1; id <= 3; id++) {
        page_init(&page, id, PAGE_TYPE_OVERFLOW);
        page.overflow.length = 100;
        fill_pattern(page.overflow.data, 100, id);
        CHECK_OK(pager_write_page(pager, &page));
    }
    CHECK_OK(pager_sync(pager));
    CHECK_OK(pager_file_size(pager, &size));
    CHECK(size == 4 * PAGE_SIZE);
    pager_close(pager);

    CHECK_OK(pager_open(path, &pager));
    DBHeader read_header;
    CHECK_OK(pager_read_header(pager, &read_header));
    CHECK(read_header.page_count == 4);
    for (uint32_t id = 3; id >= 1; id--) {
        CHECK_OK(pager_read_page(pager, id, &read_back));
        CHECK(read_back.page_id == id);
        CHECK(read_back.page_type == PAGE_TYPE_OVERFLOW);
        CHECK(read_back.overflow.length == 100);
        CHECK(check_pattern(read_back.overflow.data, 100, id));
    }

    // Reading past the end of the file, and page 0 through the page interface
    CHECK_STATUS(pager_read_page(pager, 10, &read_back), DB_ERR_CORRUPT);
    CHECK_STATUS(pager_read_page(pager, 0, &read_back), DB_ERR_INVALID);
    pager_close(pager);
}

int main(void) {
    RUN_TEST(test_endianness);
    RUN_TEST(test_bpt_leaf_roundtrip);
    RUN_TEST(test_bpt_internal_roundtrip);
    RUN_TEST(test_data_page_roundtrip);
    RUN_TEST(test_overflow_and_free_roundtrip);
    RUN_TEST(test_corrupt_pages);
    RUN_TEST(test_header);
    RUN_TEST(test_file_io);
    return 0;
}
