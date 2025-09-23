#include "../src/bptree.h"
#include "../src/util.h"
#include <assert.h>
#include <stdlib.h>

uint32_t pseudo_random(uint32_t v) {
    return (1103515245ULL * (unsigned long long)v + 12345ULL) & 0x7fffffff;
}

int main() {
    BPTree *bpt = bpt_init();

    uint64_t expected[50001];

    for (int i = 0; i <= 50000; i++) {
        uint32_t key = pseudo_random(pseudo_random(pseudo_random(i)));
        uint32_t value = pseudo_random(key);
        bpt_insert(bpt, key, value);
        expected[i] = (uint64_t)key + ((uint64_t)value << 32);
    }

    assert(bpt_height(bpt) == 2);

    Map *seen = rbt_init();
    for (int i = 50000; i >= 0; i--) {
        uint32_t v = bpt_get(bpt, expected[i] & 0xffffffff);
        uint32_t *overwritten_value = rbt_get(seen, expected[i] & 0xffffffff);
        if (overwritten_value) {
            assert(v == *overwritten_value);
        }
        else {
            assert(v == (uint32_t)(expected[i] >> 32));
            uint32_t *written_value = malloc(sizeof(uint32_t));
            *written_value = v;
            rbt_insert(seen, expected[i] & 0xffffffff, (void*)written_value);
        }
    }

    rbt_free(seen);
    bpt_free(bpt);
    return 0;
}