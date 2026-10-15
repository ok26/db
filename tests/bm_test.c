#include "../src/buffer_manager.h"
#include "test.h"

static uint32_t pinned_pages(BufferManager *bm) {
    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    return stats.pinned_pages;
}

// Writes a recognizable pattern into an overflow page
static void stamp(Page *page, uint32_t seed) {
    page->overflow.length = 64;
    fill_pattern(page->overflow.data, 64, seed);
}

static int has_stamp(Page *page, uint32_t seed) {
    return page->page_type == PAGE_TYPE_OVERFLOW && page->overflow.length == 64 &&
        check_pattern(page->overflow.data, 64, seed);
}

static void test_new_database(void) {
    const char *path = test_db_path("bm_new");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 16, &bm));

    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    CHECK(stats.page_count == 1);
    CHECK(stats.free_pages == 0);
    CHECK(stats.frame_count == 16);

    // Page 0 is the header and cannot be accessed as a normal page
    Page *page;
    CHECK_STATUS(buffer_manager_get_page(bm, 0, &page), DB_ERR_INVALID);
    CHECK_STATUS(buffer_manager_get_page(bm, 1, &page), DB_ERR_INVALID);
    CHECK_STATUS(buffer_manager_new_page(bm, PAGE_TYPE_FREE, &page), DB_ERR_INVALID);

    CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_DATA, &page));
    CHECK(page->page_id == 1);
    CHECK(page->page_type == PAGE_TYPE_DATA);
    CHECK(page->data.free_space_end == DATA_PAGE_DATA_SIZE);
    CHECK(pinned_pages(bm) == 1);
    buffer_manager_unpin(bm, page);
    CHECK(pinned_pages(bm) == 0);

    CHECK_OK(buffer_manager_close(bm));

    // Too few frames
    CHECK_STATUS(buffer_manager_open(path, BUFFER_MANAGER_MIN_FRAMES - 1, &bm),
        DB_ERR_INVALID);
}

// Many more pages than frames, every page has to survive being evicted
static void test_eviction(void) {
    const char *path = test_db_path("bm_eviction");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 8, &bm));

    enum { PAGES = 200 };
    for (uint32_t i = 1; i <= PAGES; i++) {
        Page *page;
        CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &page));
        CHECK(page->page_id == i);
        stamp(page, i);
        buffer_manager_unpin(bm, page);
    }

    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    CHECK(stats.cached_pages == 8);
    CHECK(stats.evictions == PAGES - 8);

    // Read everything back in a different order and modify every third page
    for (uint32_t n = 0; n < PAGES; n++) {
        uint32_t id = (n * 37) % PAGES + 1;
        Page *page;
        CHECK_OK(buffer_manager_get_page(bm, id, &page));
        CHECK(has_stamp(page, id));
        if (id % 3 == 0) {
            stamp(page, id + 1000);
            buffer_manager_mark_dirty(bm, page);
        }
        buffer_manager_unpin(bm, page);
    }

    for (uint32_t id = 1; id <= PAGES; id++) {
        Page *page;
        CHECK_OK(buffer_manager_get_page(bm, id, &page));
        CHECK(has_stamp(page, id % 3 == 0 ? id + 1000 : id));
        buffer_manager_unpin(bm, page);
    }

    CHECK(pinned_pages(bm) == 0);
    CHECK_OK(buffer_manager_close(bm));
}

// Pinned pages are never evicted, the least recently used unpinned page is
static void test_pinning_and_lru(void) {
    const char *path = test_db_path("bm_pinning");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 8, &bm));

    Page *pinned[8];
    for (uint32_t i = 0; i < 8; i++) {
        CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &pinned[i]));
        stamp(pinned[i], i + 1);
    }
    CHECK(pinned_pages(bm) == 8);

    // All frames are pinned
    Page *page;
    CHECK_STATUS(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &page), DB_ERR_FULL);

    // Pinning the same page twice needs two unpins
    CHECK_OK(buffer_manager_get_page(bm, 1, &page));
    CHECK(page == pinned[0]);
    buffer_manager_unpin(bm, page);
    CHECK(pinned_pages(bm) == 8);

    // Unpin pages 3, 1, 2 in this order, page 3 is now least recently used
    buffer_manager_unpin(bm, pinned[2]);
    buffer_manager_unpin(bm, pinned[0]);
    buffer_manager_unpin(bm, pinned[1]);

    // Touch page 3 again, making page 1 the least recently used
    CHECK_OK(buffer_manager_get_page(bm, 3, &page));
    buffer_manager_unpin(bm, page);

    // A new page evicts page 1, which is still readable from the file
    Page *new_page;
    CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &new_page));
    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    CHECK(stats.evictions == 1);
    uint64_t misses = stats.misses;

    // Pages 2 and 3 are still cached
    CHECK_OK(buffer_manager_get_page(bm, 2, &page));
    CHECK(has_stamp(page, 2));
    buffer_manager_unpin(bm, page);
    CHECK_OK(buffer_manager_get_page(bm, 3, &page));
    CHECK(has_stamp(page, 3));
    buffer_manager_unpin(bm, page);
    buffer_manager_stats(bm, &stats);
    CHECK(stats.misses == misses);

    // Page 1 has to be read back from the file
    CHECK_OK(buffer_manager_get_page(bm, 1, &page));
    CHECK(has_stamp(page, 1));
    buffer_manager_unpin(bm, page);
    buffer_manager_stats(bm, &stats);
    CHECK(stats.misses == misses + 1);

    buffer_manager_unpin(bm, new_page);
    for (uint32_t i = 3; i < 8; i++) buffer_manager_unpin(bm, pinned[i]);
    CHECK(pinned_pages(bm) == 0);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_freelist(void) {
    const char *path = test_db_path("bm_freelist");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 8, &bm));

    for (uint32_t i = 1; i <= 10; i++) {
        Page *page;
        CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &page));
        buffer_manager_unpin(bm, page);
    }

    // Free pages 4, 7 and 2
    uint32_t to_free[] = { 4, 7, 2 };
    for (int i = 0; i < 3; i++) {
        Page *page;
        CHECK_OK(buffer_manager_get_page(bm, to_free[i], &page));
        CHECK_OK(buffer_manager_free_page(bm, page));
    }

    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    CHECK(stats.free_pages == 3);
    CHECK(stats.page_count == 11);
    CHECK(pinned_pages(bm) == 0);

    // A page pinned twice cannot be freed
    Page *page, *same;
    CHECK_OK(buffer_manager_get_page(bm, 5, &page));
    CHECK_OK(buffer_manager_get_page(bm, 5, &same));
    CHECK_STATUS(buffer_manager_free_page(bm, page), DB_ERR_INVALID);
    buffer_manager_unpin(bm, same);
    buffer_manager_unpin(bm, page);

    // The freelist survives closing the database
    CHECK_OK(buffer_manager_close(bm));
    CHECK_OK(buffer_manager_open(path, 8, &bm));
    buffer_manager_stats(bm, &stats);
    CHECK(stats.free_pages == 3);
    CHECK(stats.page_count == 11);

    // Freed pages are reused, last freed first, before the file grows
    uint32_t expected[] = { 2, 7, 4, 11 };
    for (int i = 0; i < 4; i++) {
        CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_DATA, &page));
        CHECK(page->page_id == expected[i]);
        CHECK(page->page_type == PAGE_TYPE_DATA);
        CHECK(page->data.occupied_slots == 0);
        buffer_manager_unpin(bm, page);
    }
    buffer_manager_stats(bm, &stats);
    CHECK(stats.free_pages == 0);
    CHECK(stats.page_count == 12);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_persistence(void) {
    const char *path = test_db_path("bm_persistence");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 8, &bm));

    for (uint32_t i = 1; i <= 50; i++) {
        Page *page;
        CHECK_OK(buffer_manager_new_page(bm, PAGE_TYPE_OVERFLOW, &page));
        stamp(page, i * 7);
        buffer_manager_unpin(bm, page);
    }
    buffer_manager_set_meta(bm, DB_META_FIRST_USER, 0xC0FFEE);
    CHECK_OK(buffer_manager_close(bm));

    CHECK_OK(buffer_manager_open(path, 32, &bm));
    CHECK(buffer_manager_get_meta(bm, DB_META_FIRST_USER) == 0xC0FFEE);
    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    CHECK(stats.page_count == 51);
    for (uint32_t i = 1; i <= 50; i++) {
        Page *page;
        CHECK_OK(buffer_manager_get_page(bm, i, &page));
        CHECK(has_stamp(page, i * 7));
        buffer_manager_unpin(bm, page);
    }
    CHECK_OK(buffer_manager_close(bm));
}

static void test_not_a_database(void) {
    const char *path = test_db_path("bm_not_a_db");
    FILE *f = fopen(path, "w");
    CHECK(f);
    for (int i = 0; i < PAGE_SIZE; i++) fputc('x', f);
    fclose(f);

    BufferManager *bm;
    CHECK_STATUS(buffer_manager_open(path, 8, &bm), DB_ERR_CORRUPT);
    CHECK(bm == NULL);
}

int main(void) {
    RUN_TEST(test_new_database);
    RUN_TEST(test_eviction);
    RUN_TEST(test_pinning_and_lru);
    RUN_TEST(test_freelist);
    RUN_TEST(test_persistence);
    RUN_TEST(test_not_a_database);
    return 0;
}
