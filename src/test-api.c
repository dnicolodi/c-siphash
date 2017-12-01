/*
 * Tests for Public API
 * This test, unlikely the others, is linked against the real, distributed,
 * shared library. Its sole purpose is to test for symbol availability.
 */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c-siphash.h"

static void test_api(void) {
        CSipHash state = C_SIPHASH_NULL;
        uint8_t seed[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
        uint64_t hash;

        c_siphash_init(&state, seed);
        c_siphash_append(&state, NULL, 0);
        hash = c_siphash_finalize(&state);
        assert(hash == 12552310112479190712ULL);
}

int main(int argc, char **argv) {
        test_api();
        return 0;
}
