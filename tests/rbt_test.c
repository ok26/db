#include "../src/util.h"
#include "test.h"

static void insert_and_delete_test(void) {
    Map *m = rbt_init();
    int v = 10;
    rbt_insert(m, 200, &v, sizeof(int)); v++; CHECK(rbt_verify_tree(m));
    rbt_insert(m, 270, &v, sizeof(int)); v++; CHECK(rbt_verify_tree(m));
    rbt_insert(m, 221, &v, sizeof(int)); v++; CHECK(rbt_verify_tree(m));
    rbt_insert(m, 230, &v, sizeof(int)); v++; CHECK(rbt_verify_tree(m));
    rbt_insert(m, 1, &v, sizeof(int)); v++; CHECK(rbt_verify_tree(m));
    rbt_insert(m, 20000000, &v, sizeof(int)); CHECK(rbt_verify_tree(m));
    int *res;
    res = rbt_get(m, 200); CHECK(res); CHECK(*res == 10);
    res = rbt_get(m, 221); CHECK(res); CHECK(*res == 12);
    res = rbt_get(m, 230); CHECK(res); CHECK(*res == 13);
    res = rbt_get(m, 1); CHECK(res); CHECK(*res == 14);
    res = rbt_get(m, 20000000); CHECK(res); CHECK(*res == 15);
    res = rbt_get(m, 270); CHECK(res); CHECK(*res == 11);
    CHECK(rbt_size(m) == 6);

    CHECK(rbt_delete(m, 200)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 200); CHECK(!res);
    CHECK(rbt_delete(m, 20000000)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 20000000); CHECK(!res);
    CHECK(rbt_delete(m, 221)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 221); CHECK(!res);
    CHECK(rbt_delete(m, 230)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 230); CHECK(!res);
    CHECK(rbt_delete(m, 1)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 1); CHECK(!res);
    CHECK(rbt_delete(m, 270)); CHECK(rbt_verify_tree(m));
    res = rbt_get(m, 270); CHECK(!res);
    CHECK(rbt_size(m) == 0);

    // Deleting a key that does not exist
    CHECK(!rbt_delete(m, 270));

    rbt_free(m);
}

typedef struct TestStruct {
    uint32_t field1;
    uint8_t field2;
    uint16_t field3;
} TestStruct;

static void large_insert_test(void) {
    Map *m = rbt_init();

    for (int i = 0; i < 10000; i++) {
        uint32_t key = (uint32_t)i;
        for (int j = 0; j < 10; j++) key = pseudo_random(key);
        uint32_t v = pseudo_random(key);
        rbt_insert(m, key, &v, sizeof(uint32_t));
        uint32_t *res = rbt_get(m, key);
        CHECK(res);
        CHECK(*res == v);
    }
    CHECK(rbt_verify_tree(m));

    rbt_clear(m);

    TestStruct *p = malloc(sizeof(TestStruct));
    *p = (TestStruct){
        28734651,
        200,
        12637
    };

    rbt_insert(m, 10, &p, sizeof(TestStruct*));
    void **res = rbt_get(m, 10);
    CHECK(res);
    CHECK(((TestStruct*)*res)->field1 == p->field1);
    CHECK(((TestStruct*)*res)->field2 == p->field2);
    CHECK(((TestStruct*)*res)->field3 == p->field3);

    free(p);
    rbt_free(m);
}

// Random inserts, overwrites and deletes compared against a plain array,
// with the red-black invariants checked after every operation
static void random_operations_test(void) {
    test_seed();
    enum { KEY_SPACE = 2000, OPERATIONS = 40000 };
    static int32_t expected[KEY_SPACE];
    for (int i = 0; i < KEY_SPACE; i++) expected[i] = -1;
    uint32_t expected_size = 0;

    Map *m = rbt_init();
    for (int op = 0; op < OPERATIONS; op++) {
        uint32_t key = test_rand_below(KEY_SPACE);
        if (test_rand_below(3) != 0) {
            int32_t value = (int32_t)test_rand_below(1000000);
            if (expected[key] < 0) expected_size++;
            expected[key] = value;
            CHECK(rbt_insert(m, key, &value, sizeof(value)));
        }
        else {
            uint8_t existed = expected[key] >= 0;
            if (existed) expected_size--;
            expected[key] = -1;
            CHECK(rbt_delete(m, key) == existed);
        }

        CHECK(rbt_size(m) == expected_size);
        if (op % 97 == 0) CHECK(rbt_verify_tree(m));
    }
    CHECK(rbt_verify_tree(m));

    for (uint32_t key = 0; key < KEY_SPACE; key++) {
        int32_t *value = rbt_get(m, key);
        if (expected[key] < 0) CHECK(!value);
        else CHECK(value && *value == expected[key]);
    }

    // Delete everything in random order
    uint32_t keys[KEY_SPACE];
    for (uint32_t i = 0; i < KEY_SPACE; i++) keys[i] = i;
    for (uint32_t i = KEY_SPACE - 1; i > 0; i--) {
        uint32_t j = test_rand_below(i + 1);
        uint32_t tmp = keys[i]; keys[i] = keys[j]; keys[j] = tmp;
    }
    for (uint32_t i = 0; i < KEY_SPACE; i++) {
        CHECK(rbt_delete(m, keys[i]) == (expected[keys[i]] >= 0));
        CHECK(rbt_verify_tree(m));
    }
    CHECK(rbt_size(m) == 0);

    rbt_free(m);
}

int main(void) {
    RUN_TEST(insert_and_delete_test);
    RUN_TEST(large_insert_test);
    RUN_TEST(random_operations_test);
    return 0;
}
