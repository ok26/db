#include <stdlib.h>
#include "util.h"

// Returns index of first element strictly greater than v
uint32_t upper_bound(uint32_t *arr, uint32_t len, uint32_t v) {
    uint32_t l = 0, r = len;
    while (l < r) {
        uint32_t mid = l + (r - l) / 2;
        if (arr[mid] <= v) {
            l = mid + 1;
        }
        else {
            r = mid;
        }
    }

    return l;
}

// Returns index of first element greater than or equal to v
uint32_t lower_bound(uint32_t *arr, uint32_t len, uint32_t v) {
    uint32_t l = 0, r = len;
    while (l < r) {
        uint32_t mid = l + (r - l) / 2;
        if (arr[mid] < v) {
            l = mid + 1;
        }
        else {
            r = mid;
        }
    }

    return l;
}

uint32_t *clone_u32(uint32_t *src) {
    uint32_t *dst = malloc(sizeof(uint32_t));
    *dst = *src;
    return dst;
}