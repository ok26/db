#include "../src/bptree.h"
#include "../src/util.h"
#include "test.h"

// This file is compiled twice: against the normal library and against one
// built with a tiny tree order (see SMALL_DEFS in the Makefile). The tiny
// order gives very deep trees, so fewer keys are used there.
#if MAX_ENTRIES_LEAF < 64 || MAX_CHILDREN < 64
#define SMALL_ORDER 1
#define N_BASIC 5000
#define N_MASSIVE 30000
#else
#define SMALL_ORDER 0
#define N_BASIC 50000
#define N_MASSIVE 1000000
#endif

// The exact heights are only checked with the default order
#define DEFAULT_ORDER (MAX_ENTRIES_LEAF == BPT_LEAF_CAPACITY && \
    MAX_CHILDREN == BPT_INTERNAL_CAPACITY + 1)

static BufferManagerStats stats_of(BufferManager *bm) {
    BufferManagerStats stats;
    buffer_manager_stats(bm, &stats);
    return stats;
}

static void check_no_pins(BufferManager *bm) {
    CHECK(stats_of(bm).pinned_pages == 0);
}

static uint32_t height_of(BPTree *bpt) {
    uint32_t height;
    CHECK_OK(bpt_height(bpt, &height));
    return height;
}

static void check_get_u32(BPTree *bpt, uint32_t key, uint32_t expected) {
    void *data;
    uint32_t size;
    CHECK_OK(bpt_get(bpt, key, &data, &size));
    CHECK(size == sizeof(uint32_t));
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    CHECK(value == expected);
    free(data);
}

static void check_get_pattern(BPTree *bpt, uint32_t key, uint32_t size, uint32_t seed) {
    void *data;
    uint32_t read_size;
    CHECK_OK(bpt_get(bpt, key, &data, &read_size));
    CHECK(read_size == size);
    CHECK(check_pattern(data, size, seed));
    free(data);
}

static void insert_pattern(BPTree *bpt, uint32_t key, uint32_t size, uint32_t seed) {
    static uint8_t buffer[40000];
    CHECK(size <= sizeof(buffer));
    fill_pattern(buffer, size, seed);
    CHECK_OK(bpt_insert(bpt, key, buffer, size));
}

static uint64_t verify(BPTree *bpt) {
    BPTStats stats;
    CHECK_OK(bpt_verify(bpt, &stats));
    CHECK(stats.height == height_of(bpt));
    return stats.key_count;
}

static void shuffle(uint32_t *keys, uint32_t n) {
    for (uint32_t i = n - 1; i > 0; i--) {
        uint32_t j = test_rand_below(i + 1);
        uint32_t tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
    }
}


// The original test_empty_db + test_filled_db: insert, read back, then open
// the database again and read everything from the file
static void test_insert_and_reopen(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_reopen" : "bpt_reopen");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 0, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));
    CHECK(height_of(bpt) == 1);

    for (uint32_t i = 0; i <= N_BASIC; i++) {
        uint32_t key = pseudo_random(i);
        CHECK_OK(bpt_insert(bpt, key, &i, sizeof(uint32_t)));
    }
    if (DEFAULT_ORDER) CHECK(height_of(bpt) == 2);

    for (uint32_t i = N_BASIC + 1; i-- > 0;) {
        check_get_u32(bpt, pseudo_random(i), i);
    }
    CHECK(verify(bpt) == N_BASIC + 1);
    check_no_pins(bm);

    // Keys that were never inserted
    void *data;
    uint32_t size;
    CHECK_STATUS(bpt_get(bpt, 0x80000000u, &data, &size), DB_NOT_FOUND);
    CHECK(data == NULL);
    CHECK_STATUS(bpt_delete(bpt, 0x80000000u), DB_NOT_FOUND);

    // Store the root so that the tree can be found again
    uint32_t height = height_of(bpt);
    buffer_manager_set_meta(bm, DB_META_FIRST_USER, bpt_root_page_id(bpt));
    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));

    CHECK_OK(buffer_manager_open(path, 64, &bm));
    CHECK_OK(bpt_open(bm, buffer_manager_get_meta(bm, DB_META_FIRST_USER), &bpt));
    CHECK(height_of(bpt) == height);

    for (uint32_t i = N_BASIC + 1; i-- > 0;) {
        check_get_u32(bpt, pseudo_random(i), i);
    }

    uint32_t fib1 = 0, fib2 = 1;
    while (fib1 <= N_BASIC) {
        check_get_u32(bpt, pseudo_random(fib1), fib1);
        fib2 = fib1 + fib2;
        fib1 = fib2 - fib1;
    }
    CHECK(verify(bpt) == N_BASIC + 1);
    check_no_pins(bm);

    // Opening something that is not a tree
    BPTree *not_a_tree;
    CHECK_STATUS(bpt_open(bm, 0, &not_a_tree), DB_ERR_INVALID);

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

static int count_cb(uint32_t key, const void *data, uint32_t size, void *ctx) {
    (void)key;
    (void)data;
    (void)size;
    (*(uint32_t*)ctx)++;
    return 0;
}

// The original test_with_deletion: insert keys, delete them all in random order
static void test_delete_everything(void) {
    test_seed();
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_delete" : "bpt_delete");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 64, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));
    uint32_t root = bpt_root_page_id(bpt);

    static uint32_t keys[N_BASIC + 1];
    for (uint32_t i = 0; i <= N_BASIC; i++) keys[i] = pseudo_random(i);

    for (uint32_t i = 0; i <= N_BASIC; i++) {
        uint32_t v = i * i;
        CHECK_OK(bpt_insert(bpt, keys[i], &v, sizeof(uint32_t)));
    }
    CHECK(verify(bpt) == N_BASIC + 1);
    CHECK(height_of(bpt) >= 2);

    shuffle(keys, N_BASIC + 1);
    for (uint32_t i = 0; i <= N_BASIC; i++) {
        CHECK_OK(bpt_delete(bpt, keys[i]));
        if (i % (N_BASIC / 10) == 0) {
            CHECK(verify(bpt) == N_BASIC - i);
            check_no_pins(bm);
        }
    }

    uint32_t remaining = 0;
    CHECK_OK(bpt_range_query(bpt, 0, UINT32_MAX, count_cb, &remaining));
    CHECK(remaining == 0);

    uint8_t empty;
    CHECK_OK(bpt_empty(bpt, &empty));
    CHECK(empty);
    CHECK(height_of(bpt) == 1);
    CHECK(bpt_root_page_id(bpt) == root);
    CHECK(verify(bpt) == 0);

    // Every page except the header and the root is free again
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 2);

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

// The original test_massive, which used to crash once the cache was full.
// The tree is several times larger than the buffer pool, so pages are
// evicted and read back all the time.
static void test_massive(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_massive" : "bpt_massive");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, SMALL_ORDER ? 32 : 1024, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));
    Map *m = rbt_init();

    for (uint32_t i = 0; i < N_MASSIVE; i++) {
        uint32_t key = pseudo_random(i);
        uint32_t v = i;
        CHECK_OK(bpt_insert(bpt, key, &v, sizeof(uint32_t)));
        CHECK(rbt_insert(m, key, &v, sizeof(uint32_t)));
    }
    if (DEFAULT_ORDER) CHECK(height_of(bpt) == 3);
    check_no_pins(bm);
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.page_count > 4 * stats.frame_count);
    CHECK(stats.evictions > stats.page_count);

    // Retrieve elements in fibonacci order
    uint32_t fib1 = 0, fib2 = 1;
    while (fib1 < N_MASSIVE) {
        uint32_t key = pseudo_random(fib1);
        uint32_t expected_v = *(uint32_t*)rbt_get(m, key);
        check_get_u32(bpt, key, expected_v);
        fib2 = fib1 + fib2;
        fib1 = fib2 - fib1;
    }

    CHECK(verify(bpt) == N_MASSIVE);
    for (uint32_t i = 0; i < N_MASSIVE; i += 7) {
        check_get_u32(bpt, pseudo_random(i), i);
    }

    rbt_free(m);
    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

// Keys in ascending and descending order are the worst cases for splits
static void test_sequential_and_extreme_keys(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_sequential" : "bpt_sequential");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 32, &bm));
    BPTree *asc, *desc;
    CHECK_OK(bpt_create(bm, &asc));
    CHECK_OK(bpt_create(bm, &desc));

    for (uint32_t i = 0; i < N_BASIC; i++) {
        CHECK_OK(bpt_insert(asc, i, &i, sizeof(i)));
        uint32_t key = UINT32_MAX - i;
        CHECK_OK(bpt_insert(desc, key, &i, sizeof(i)));
    }
    CHECK(verify(asc) == N_BASIC);
    CHECK(verify(desc) == N_BASIC);
    check_get_u32(asc, 0, 0);
    check_get_u32(desc, UINT32_MAX, 0);
    check_get_u32(asc, N_BASIC - 1, N_BASIC - 1);

    // Remove from the front of one and the back of the other
    for (uint32_t i = 0; i < N_BASIC - 1; i++) {
        CHECK_OK(bpt_delete(asc, i));
        CHECK_OK(bpt_delete(desc, UINT32_MAX - (N_BASIC - 1) + i));
    }
    CHECK(verify(asc) == 1);
    CHECK(verify(desc) == 1);
    check_get_u32(asc, N_BASIC - 1, N_BASIC - 1);
    check_get_u32(desc, UINT32_MAX, 0);
    CHECK(height_of(asc) == 1);

    bpt_close(asc);
    bpt_close(desc);
    CHECK_OK(buffer_manager_close(bm));
}

static void test_overwrite_and_sizes(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_overwrite" : "bpt_overwrite");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 32, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));

    enum { KEYS = 2000 };
    for (uint32_t k = 0; k < KEYS; k++) insert_pattern(bpt, k * 3, k % 50, k);

    // Overwrite with smaller, larger and overflowing values
    for (uint32_t k = 0; k < KEYS; k++) {
        uint32_t size = k % 3 == 0 ? 0 : k % 3 == 1 ? 1000 : 9000;
        insert_pattern(bpt, k * 3, size, k + 1);
    }
    CHECK(verify(bpt) == KEYS);
    for (uint32_t k = 0; k < KEYS; k++) {
        uint32_t size = k % 3 == 0 ? 0 : k % 3 == 1 ? 1000 : 9000;
        check_get_pattern(bpt, k * 3, size, k + 1);
    }

    // No pages are leaked by the overwrites
    for (uint32_t k = 0; k < KEYS; k++) CHECK_OK(bpt_delete(bpt, k * 3));
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 2);
    check_no_pins(bm);

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

typedef struct RangeCheck {
    const int32_t *expected; // Seed per key, -1 if the key does not exist
    uint32_t next_key;       // Every key below this has already been seen
    uint32_t count;
    uint32_t stop_after;     // 0 = never stop
} RangeCheck;

static int range_cb(uint32_t key, const void *data, uint32_t size, void *ctx) {
    RangeCheck *check = ctx;
    CHECK(key >= check->next_key);
    // No existing key may be skipped
    for (uint32_t k = check->next_key; k < key; k++) CHECK(check->expected[k] < 0);
    CHECK(check->expected[key] >= 0);
    CHECK(size == key % 40);
    CHECK(check_pattern(data, size, (uint32_t)check->expected[key]));
    check->next_key = key + 1;
    check->count++;
    return check->stop_after && check->count == check->stop_after;
}

static void test_range_query(void) {
    test_seed();
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_range" : "bpt_range");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 32, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));

    enum { KEY_SPACE = 20000 };
    static int32_t expected[KEY_SPACE];
    for (uint32_t k = 0; k < KEY_SPACE; k++) {
        expected[k] = -1;
        if (test_rand_below(3) == 0) {
            expected[k] = (int32_t)k;
            insert_pattern(bpt, k, k % 40, k);
        }
    }

    for (int i = 0; i < 300; i++) {
        uint32_t lo = test_rand_below(KEY_SPACE);
        uint32_t hi = lo + test_rand_below(KEY_SPACE - lo);
        RangeCheck check = { expected, lo, 0, 0 };
        CHECK_OK(bpt_range_query(bpt, lo, hi, range_cb, &check));
        for (uint32_t k = check.next_key; k <= hi; k++) CHECK(expected[k] < 0);
    }

    // Stop early
    RangeCheck check = { expected, 0, 0, 10 };
    CHECK_OK(bpt_range_query(bpt, 0, UINT32_MAX, range_cb, &check));
    CHECK(check.count == 10);

    // Empty and inverted ranges
    uint32_t count = 0;
    CHECK_OK(bpt_range_query(bpt, KEY_SPACE, UINT32_MAX, count_cb, &count));
    CHECK_OK(bpt_range_query(bpt, 100, 99, count_cb, &count));
    CHECK(count == 0);
    check_no_pins(bm);

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

// Random inserts, overwrites and deletes with values of all sizes, compared
// against a reference. Runs with the smallest possible buffer pool.
static void test_random_operations(void) {
    test_seed();
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_random" : "bpt_random");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, BUFFER_MANAGER_MIN_FRAMES, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));

    enum { KEY_SPACE = 6000, OPERATIONS = 60000 };
    static uint32_t sizes[KEY_SPACE];
    static int32_t seeds[KEY_SPACE];
    uint32_t live = 0;
    for (uint32_t k = 0; k < KEY_SPACE; k++) seeds[k] = -1;

    for (uint32_t op = 0; op < OPERATIONS; op++) {
        // Spread the keys over the whole range of uint32_t
        uint32_t idx = test_rand_below(KEY_SPACE);
        uint32_t key = idx * 715827u;

        // Phases that grow and shrink the tree
        uint32_t insert_chance = (op / 10000) % 2 == 0 ? 70 : 30;
        if (test_rand_below(100) < insert_chance) {
            uint32_t r = test_rand_below(100);
            uint32_t size = r < 2 ? 5000 + test_rand_below(30000) :
                            r < 10 ? test_rand_below(4000) : test_rand_below(64);
            if (seeds[idx] < 0) live++;
            insert_pattern(bpt, key, size, op);
            sizes[idx] = size;
            seeds[idx] = (int32_t)op;
        }
        else {
            DBStatus expected = seeds[idx] < 0 ? DB_NOT_FOUND : DB_OK;
            CHECK_STATUS(bpt_delete(bpt, key), expected);
            if (seeds[idx] >= 0) live--;
            seeds[idx] = -1;
        }
        check_no_pins(bm);

        if (op % 2000 == 0) {
            CHECK(verify(bpt) == live);
            for (uint32_t k = 0; k < KEY_SPACE; k++) {
                if (seeds[k] >= 0) {
                    check_get_pattern(bpt, k * 715827u, sizes[k], (uint32_t)seeds[k]);
                }
                else {
                    void *data;
                    uint32_t size;
                    CHECK_STATUS(bpt_get(bpt, k * 715827u, &data, &size), DB_NOT_FOUND);
                }
            }
        }
    }

    // Everything is still there after reopening
    uint32_t root = bpt_root_page_id(bpt);
    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
    CHECK_OK(buffer_manager_open(path, BUFFER_MANAGER_MIN_FRAMES, &bm));
    CHECK_OK(bpt_open(bm, root, &bpt));
    CHECK(verify(bpt) == live);
    for (uint32_t k = 0; k < KEY_SPACE; k++) {
        if (seeds[k] >= 0) check_get_pattern(bpt, k * 715827u, sizes[k], (uint32_t)seeds[k]);
    }

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

// The root page id never changes while the tree grows and shrinks
static void test_fixed_root(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_root" : "bpt_root");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 32, &bm));
    BPTree *bpt;
    CHECK_OK(bpt_create(bm, &bpt));
    uint32_t root = bpt_root_page_id(bpt);

    // Sequential inserts leave half full nodes, this is enough for 3 levels
    uint32_t n = MAX_ENTRIES_LEAF * MAX_CHILDREN * 2;
    uint32_t max_height = 1;
    for (uint32_t i = 0; i < n; i++) {
        CHECK_OK(bpt_insert(bpt, i, &i, sizeof(i)));
        uint32_t h = height_of(bpt);
        CHECK(h >= max_height);
        max_height = h;
    }
    CHECK(max_height >= 3);
    CHECK(bpt_root_page_id(bpt) == root);
    verify(bpt);

    for (uint32_t i = 0; i < n; i++) {
        CHECK_OK(bpt_delete(bpt, i));
        CHECK(bpt_root_page_id(bpt) == root);
    }
    CHECK(height_of(bpt) == 1);
    verify(bpt);

    bpt_close(bpt);
    CHECK_OK(buffer_manager_close(bm));
}

// Several trees in one database, destroying one does not affect the other
static void test_destroy(void) {
    const char *path = test_db_path(SMALL_ORDER ? "bpt_small_destroy" : "bpt_destroy");
    BufferManager *bm;
    CHECK_OK(buffer_manager_open(path, 32, &bm));
    BPTree *a, *b;
    CHECK_OK(bpt_create(bm, &a));
    CHECK_OK(bpt_create(bm, &b));

    for (uint32_t i = 0; i < N_BASIC / 5; i++) {
        insert_pattern(a, pseudo_random(i), i % 100, i);
        insert_pattern(b, i, i % 20 == 0 ? 6000 : 30, i + 1);
    }

    CHECK_OK(bpt_destroy(a));
    CHECK(verify(b) == N_BASIC / 5);
    for (uint32_t i = 0; i < N_BASIC / 5; i++) {
        check_get_pattern(b, i, i % 20 == 0 ? 6000 : 30, i + 1);
    }

    CHECK_OK(bpt_destroy(b));
    BufferManagerStats stats = stats_of(bm);
    CHECK(stats.free_pages == stats.page_count - 1);
    check_no_pins(bm);
    CHECK_OK(buffer_manager_close(bm));
}

int main(void) {
    printf("  (max %u entries per leaf, max %u children)\n",
        (unsigned)MAX_ENTRIES_LEAF, (unsigned)MAX_CHILDREN);
    RUN_TEST(test_insert_and_reopen);
    RUN_TEST(test_delete_everything);
    RUN_TEST(test_massive);
    RUN_TEST(test_sequential_and_extreme_keys);
    RUN_TEST(test_overwrite_and_sizes);
    RUN_TEST(test_range_query);
    RUN_TEST(test_random_operations);
    RUN_TEST(test_fixed_root);
    RUN_TEST(test_destroy);
    return 0;
}
