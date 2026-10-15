#ifndef TEST_H
#define TEST_H

// Minimal test helpers shared by all tests

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/common.h"

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

#define CHECK_STATUS(expr, expected) do { \
        DBStatus _rc = (expr); \
        if (_rc != (expected)) { \
            fprintf(stderr, "%s:%d: %s returned '%s', expected '%s'\n", __FILE__, \
                __LINE__, #expr, db_status_str(_rc), db_status_str(expected)); \
            exit(1); \
        } \
    } while (0)

#define CHECK_OK(expr) CHECK_STATUS(expr, DB_OK)

#define RUN_TEST(fn) do { \
        printf("  %s ... ", #fn); \
        fflush(stdout); \
        fn(); \
        printf("ok\n"); \
    } while (0)

// Deterministic pseudo random numbers (xorshift64*) so that failures can be
// reproduced. The seed can be changed with the TEST_SEED environment variable.
static uint64_t test_rng_state = 0x9E3779B97F4A7C15ull;

static inline void test_seed(void) {
    const char *env = getenv("TEST_SEED");
    uint64_t seed = env ? strtoull(env, NULL, 10) : 42;
    test_rng_state = seed * 0x9E3779B97F4A7C15ull + 1;
    printf("(seed %llu) ", (unsigned long long)seed);
}

static inline uint64_t test_rand(void) {
    test_rng_state ^= test_rng_state >> 12;
    test_rng_state ^= test_rng_state << 25;
    test_rng_state ^= test_rng_state >> 27;
    return test_rng_state * 0x2545F4914F6CDD1Dull;
}

// Uniform number in [0, n)
static inline uint32_t test_rand_below(uint32_t n) {
    return (uint32_t)(test_rand() % n);
}

// The pseudo random function used by the original tests. It is a bijection
// on [0, 2^31) so it never produces the same key twice.
static inline uint32_t pseudo_random(uint32_t v) {
    return (1103515245ULL * (unsigned long long)v + 12345ULL) & 0x7fffffff;
}

// Returns the path of a database file for a test and deletes any old file
// with that name. Files are placed in $TEST_DB_DIR (default build/testdb).
static inline const char *test_db_path(const char *name) {
    static char path[512];
    const char *dir = getenv("TEST_DB_DIR");
    if (!dir) dir = "build/testdb";

    // mkdir -p
    char partial[400];
    snprintf(partial, sizeof(partial), "%s", dir);
    for (char *p = partial + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char c = *p;
            *p = '\0';
            if (mkdir(partial, 0755) != 0 && errno != EEXIST) {
                perror(partial);
                exit(1);
            }
            *p = c;
            if (c == '\0') break;
        }
    }

    snprintf(path, sizeof(path), "%s/%s.db", dir, name);
    unlink(path);
    return path;
}

// Fills buf with bytes derived from seed so that the content can be checked later
static inline void fill_pattern(uint8_t *buf, uint32_t size, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < size; i++) {
        x = x * 1664525u + 1013904223u;
        buf[i] = (uint8_t)(x >> 24);
    }
}

static inline int check_pattern(const uint8_t *buf, uint32_t size, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < size; i++) {
        x = x * 1664525u + 1013904223u;
        if (buf[i] != (uint8_t)(x >> 24)) return 0;
    }
    return 1;
}

#endif
