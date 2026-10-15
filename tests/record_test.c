#include "../src/record_manager.h"
#include "test.h"

static BufferManagerStats stats_of(BufferManager *bm) {
    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    return stats;
}

static RID insert_pattern(BufferManager *bm, uint32_t size, uint32_t seed) {
    uint8_t *data = malloc(size ? size : 1);
    CHECK(data);
    fill_pattern(data, size, seed);
    RID rid;
    CHECK_OK(record_insert(bm, data, size, &rid));
    free(data);
    return rid;
}

static void check_record(BufferManager *bm, RID rid, uint32_t size, uint32_t seed) {
    void *data;
    uint32_t read_size;
    CHECK_OK(record_read(bm, rid, &data, &read_size));
    CHECK(read_size == size);
    CHECK(check_pattern(data, size, seed));
    free(data);
}

// Walks the free-space list and checks that it is a consistent doubly
// linked list of data pages that all have enough free space.
// Returns the number of pages in the list.
static uint32_t check_free_space_list(BufferManager *bm) {
    uint32_t count = 0;
    uint32_t prev = NULL_PAGE_ID;
    uint32_t page_id = buffer_manager_get_meta(bm, DB_META_FREE_SPACE_LIST);
    while (page_id != NULL_PAGE_ID) {
        Page *page;
        CHECK_OK(buffer_manager_get_page(bm, page_id, &page));
        DataPage *dp = &page->data;
        CHECK(page->page_type == PAGE_TYPE_DATA);
        CHECK(dp->flags & DATA_PAGE_FLAG_IN_FREE_LIST);
        CHECK(dp->prev_free_page == prev);
        CHECK(dp->occupied_slots > 0);
        uint32_t free_bytes = (uint32_t)(dp->free_space_end - dp->free_space_start) +
            dp->fragmented_bytes;
        CHECK(free_bytes >= FREE_SPACE_THRESHOLD);
        prev = page_id;
        page_id = dp->next_free_page;
        buffer_manager_unpin(bm, page);
        CHECK(++count < 1000000);
    }
    return count;
}

static void test_sizes(void) {
    const char *path = test_db_path("record_sizes");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    uint32_t sizes[] = {
        0, 1, 4, 100, 1000, MAX_INLINE_RECORD_SIZE - 1, MAX_INLINE_RECORD_SIZE,
        MAX_INLINE_RECORD_SIZE + 1, OVERFLOW_PAGE_DATA_SIZE, OVERFLOW_PAGE_DATA_SIZE + 1,
        3 * OVERFLOW_PAGE_DATA_SIZE, 100000, 1000000
    };
    enum { COUNT = sizeof(sizes) / sizeof(sizes[0]) };
    RID rids[COUNT];

    for (uint32_t i = 0; i < COUNT; i++) rids[i] = insert_pattern(bm, sizes[i], i);
    for (uint32_t i = 0; i < COUNT; i++) check_record(bm, rids[i], sizes[i], i);
    CHECK(stats_of(bm).pinned_pages == 0);

    // Overflow records use ceil(size / OVERFLOW_PAGE_DATA_SIZE) extra pages
    uint32_t pages_before = stats_of(bm).page_count;
    RID big = insert_pattern(bm, 5 * OVERFLOW_PAGE_DATA_SIZE, 77);
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.page_count - stats.free_pages <= pages_before + 5 + 1);
    check_record(bm, big, 5 * OVERFLOW_PAGE_DATA_SIZE, 77);
    CHECK_OK(record_delete(bm, big));

    for (uint32_t i = 0; i < COUNT; i++) {
        CHECK_OK(record_delete(bm, rids[i]));
        void *data;
        uint32_t size;
        CHECK_STATUS(record_read(bm, rids[i], &data, &size), DB_NOT_FOUND);
        CHECK(data == NULL);
        CHECK_STATUS(record_delete(bm, rids[i]), DB_NOT_FOUND);
    }

    // Everything is deleted, so every page except the header is free again
    stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 1);
    CHECK(buffer_manager_get_meta(bm, DB_META_FREE_SPACE_LIST) == NULL_PAGE_ID);
    CHECK(stats.pinned_pages == 0);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_invalid_rids(void) {
    const char *path = test_db_path("record_invalid");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    RID rid = insert_pattern(bm, 10, 1);
    RID overflow = insert_pattern(bm, 10000, 2);
    void *data;
    uint32_t size;

    RID bad = rid;
    bad.slot_id = 5;
    CHECK_STATUS(record_read(bm, bad, &data, &size), DB_NOT_FOUND);
    bad.page_id = 0;
    CHECK_STATUS(record_read(bm, bad, &data, &size), DB_NOT_FOUND);
    bad.page_id = 1000;
    CHECK_STATUS(record_delete(bm, bad), DB_NOT_FOUND);

    // The first page of the overflow chain is not a data page
    bad.page_id = overflow.page_id - 1;
    bad.slot_id = 0;
    CHECK_STATUS(record_read(bm, bad, &data, &size), DB_NOT_FOUND);

    CHECK_STATUS(record_insert(bm, NULL, 10, &bad), DB_ERR_INVALID);
    CHECK(stats_of(bm).pinned_pages == 0);
    CHECK_OK(buffer_manager_close(bm));
}

// Small records fill pages tightly, deleted space is reused
static void test_space_reuse(void) {
    const char *path = test_db_path("record_reuse");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    enum { COUNT = 20000, SIZE = 20 };
    static RID rids[COUNT];
    for (uint32_t i = 0; i < COUNT; i++) rids[i] = insert_pattern(bm, SIZE, i);

    // Each record needs SIZE bytes + a slot entry. Pages are filled until
    // less than FREE_SPACE_THRESHOLD bytes are left.
    uint32_t max_per_page = DATA_PAGE_DATA_SIZE / (SIZE + SLOT_ENTRY_SIZE);
    uint32_t min_per_page = (DATA_PAGE_DATA_SIZE - FREE_SPACE_THRESHOLD) /
        (SIZE + SLOT_ENTRY_SIZE);
    uint32_t used_pages = stats_of(bm).page_count - 1;
    CHECK(used_pages >= COUNT / max_per_page);
    CHECK(used_pages <= COUNT / min_per_page + 1);

    // Delete every other record, then insert as many again. This should
    // fill the holes and hardly grow the file (a page can end up with an
    // unused slot entry, which makes it leave the free-space list a record
    // earlier than the first time).
    for (uint32_t i = 0; i < COUNT; i += 2) CHECK_OK(record_delete(bm, rids[i]));
    check_free_space_list(bm);
    for (uint32_t i = 0; i < COUNT; i += 2) rids[i] = insert_pattern(bm, SIZE, i + COUNT);
    CHECK(stats_of(bm).page_count - 1 <= used_pages + 1);

    for (uint32_t i = 0; i < COUNT; i++) {
        check_record(bm, rids[i], SIZE, i % 2 == 0 ? i + COUNT : i);
    }
    check_free_space_list(bm);
    CHECK_OK(buffer_manager_close(bm));
}

// A record that only fits after the holes in a page are compacted
static void test_compaction(void) {
    const char *path = test_db_path("record_compaction");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    // Fill exactly one page with 10 records
    uint32_t size = (DATA_PAGE_DATA_SIZE - 10 * SLOT_ENTRY_SIZE) / 10;
    RID rids[10];
    for (uint32_t i = 0; i < 10; i++) {
        rids[i] = insert_pattern(bm, size, i);
        CHECK(rids[i].page_id == rids[0].page_id);
        CHECK(rids[i].slot_id == i);
    }

    // Delete records 2, 4 and 6, leaving three separate holes
    CHECK_OK(record_delete(bm, rids[2]));
    CHECK_OK(record_delete(bm, rids[4]));
    CHECK_OK(record_delete(bm, rids[6]));

    // A record of twice the size only fits into the page after compaction,
    // it reuses the first free slot
    RID big = insert_pattern(bm, 2 * size, 100);
    CHECK(big.page_id == rids[0].page_id);
    CHECK(big.slot_id == 2);

    // RIDs of the other records stay valid even though they were moved
    for (uint32_t i = 0; i < 10; i++) {
        if (i == 2 || i == 4 || i == 6) continue;
        check_record(bm, rids[i], size, i);
    }
    check_record(bm, big, 2 * size, 100);
    CHECK(stats_of(bm).page_count == 2);
    CHECK_OK(buffer_manager_close(bm));

    // And the page is still valid after being written and read again
    CHECK_OK(buffer_manager_open(path, 16, &bm));
    check_record(bm, big, 2 * size, 100);
    check_record(bm, rids[9], size, 9);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_update(void) {
    const char *path = test_db_path("record_update");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    RID rid = insert_pattern(bm, 500, 1);
    uint8_t buffer[20000];

    // Shrinking keeps the RID
    RID new_rid;
    fill_pattern(buffer, 300, 2);
    CHECK_OK(record_update(bm, rid, buffer, 300, &new_rid));
    CHECK(new_rid.page_id == rid.page_id && new_rid.slot_id == rid.slot_id);
    check_record(bm, rid, 300, 2);

    // Growing moves the record
    RID other = insert_pattern(bm, 10, 3);
    fill_pattern(buffer, 3000, 4);
    CHECK_OK(record_update(bm, rid, buffer, 3000, &new_rid));
    check_record(bm, new_rid, 3000, 4);
    check_record(bm, other, 10, 3);

    // To and from overflow pages
    rid = new_rid;
    fill_pattern(buffer, 20000, 5);
    CHECK_OK(record_update(bm, rid, buffer, 20000, &new_rid));
    check_record(bm, new_rid, 20000, 5);
    rid = new_rid;
    fill_pattern(buffer, 7, 6);
    CHECK_OK(record_update(bm, rid, buffer, 7, &new_rid));
    check_record(bm, new_rid, 7, 6);

    CHECK_OK(record_delete(bm, new_rid));
    CHECK_OK(record_delete(bm, other));
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 1);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_persistence(void) {
    const char *path = test_db_path("record_persistence");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    enum { COUNT = 3000 };
    static RID rids[COUNT];
    for (uint32_t i = 0; i < COUNT; i++) rids[i] = insert_pattern(bm, i % 300, i);
    // Leave holes in many pages
    for (uint32_t i = 0; i < COUNT; i += 3) CHECK_OK(record_delete(bm, rids[i]));
    uint32_t list_length = check_free_space_list(bm);
    CHECK(list_length > 1);
    CHECK_OK(buffer_manager_close(bm));

    CHECK_OK(buffer_manager_open(path, 16, &bm));
    for (uint32_t i = 0; i < COUNT; i++) {
        if (i % 3 == 0) continue;
        check_record(bm, rids[i], i % 300, i);
    }

    // The free-space list survived, so small inserts go into existing pages
    CHECK(check_free_space_list(bm) == list_length);
    uint32_t page_count = stats_of(bm).page_count;
    for (uint32_t i = 0; i < 200; i++) insert_pattern(bm, 10, i);
    CHECK(stats_of(bm).page_count == page_count);
    CHECK_OK(buffer_manager_close(bm));
}

// Random inserts, updates and deletes of all sizes with a tiny buffer pool
static void test_random(void) {
    test_seed();
    const char *path = test_db_path("record_random");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, BUFFER_MANAGER_MIN_FRAMES, &bm));

    enum { SLOTS = 3000, OPERATIONS = 30000 };
    static RID rids[SLOTS];
    static uint32_t sizes[SLOTS];
    static uint32_t seeds[SLOTS];
    static uint8_t live[SLOTS];
    static uint8_t buffer[30000];

    for (uint32_t op = 0; op < OPERATIONS; op++) {
        uint32_t i = test_rand_below(SLOTS);
        uint32_t size;
        uint32_t r = test_rand_below(100);
        if (r < 2) size = 4000 + test_rand_below(26000);  // Overflow
        else if (r < 10) size = test_rand_below(MAX_INLINE_RECORD_SIZE + 1);
        else size = test_rand_below(200);
        uint32_t seed = op;

        if (!live[i]) {
            rids[i] = insert_pattern(bm, size, seed);
            live[i] = 1;
        }
        else if (test_rand_below(2) == 0) {
            CHECK_OK(record_delete(bm, rids[i]));
            live[i] = 0;
            continue;
        }
        else {
            fill_pattern(buffer, size, seed);
            CHECK_OK(record_update(bm, rids[i], buffer, size, &rids[i]));
        }
        sizes[i] = size;
        seeds[i] = seed;

        if (op % 5000 == 0) {
            for (uint32_t j = 0; j < SLOTS; j++) {
                if (live[j]) check_record(bm, rids[j], sizes[j], seeds[j]);
            }
            check_free_space_list(bm);
            CHECK(stats_of(bm).pinned_pages == 0);
        }
    }

    // Survives a reopen
    CHECK_OK(buffer_manager_close(bm));
    CHECK_OK(buffer_manager_open(path, BUFFER_MANAGER_MIN_FRAMES, &bm));
    for (uint32_t j = 0; j < SLOTS; j++) {
        if (live[j]) check_record(bm, rids[j], sizes[j], seeds[j]);
    }
    check_free_space_list(bm);

    // Deleting everything frees every page
    for (uint32_t j = 0; j < SLOTS; j++) {
        if (live[j]) CHECK_OK(record_delete(bm, rids[j]));
    }
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 1);
    CHECK(check_free_space_list(bm) == 0);
    CHECK_OK(buffer_manager_close(bm));
}

int main(void) {
    RUN_TEST(test_sizes);
    RUN_TEST(test_invalid_rids);
    RUN_TEST(test_space_reuse);
    RUN_TEST(test_compaction);
    RUN_TEST(test_update);
    RUN_TEST(test_persistence);
    RUN_TEST(test_random);
    return 0;
}
