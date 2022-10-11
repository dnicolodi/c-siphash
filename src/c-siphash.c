/*
 * SipHash Implementation
 *
 * For highlevel documentation of the API see the header file and the docbook
 * comments. This implementation is based on the reference implementation of
 * SipHash, written by Jean-Philippe Aumasson and Daniel J. Bernstein, and
 * released to the Public Domain.
 *
 * So far, only SipHash24 is implemented, since there was no need for other
 * parameters. However, adjusted c_siphash_append_X() and
 * c_siphash_finalize_Y() can be easily provided, if required.
 */

#include <c-stdaux.h>
#include <stddef.h>
#include <stdint.h>
#include "c-siphash.h"

static inline uint64_t c_siphash_read_le64(const uint8_t bytes[8]) {
        return  ((uint64_t) bytes[0]) |
               (((uint64_t) bytes[1]) <<  8) |
               (((uint64_t) bytes[2]) << 16) |
               (((uint64_t) bytes[3]) << 24) |
               (((uint64_t) bytes[4]) << 32) |
               (((uint64_t) bytes[5]) << 40) |
               (((uint64_t) bytes[6]) << 48) |
               (((uint64_t) bytes[7]) << 56);
}

static inline uint64_t c_siphash_rotate_left(uint64_t x, uint8_t b) {
        return (x << b) | (x >> (64 - b));
}

static inline void c_siphash_sipround(CSipHash *state) {
        state->v0 += state->v1;
        state->v1 = c_siphash_rotate_left(state->v1, 13);
        state->v1 ^= state->v0;
        state->v0 = c_siphash_rotate_left(state->v0, 32);
        state->v2 += state->v3;
        state->v3 = c_siphash_rotate_left(state->v3, 16);
        state->v3 ^= state->v2;
        state->v0 += state->v3;
        state->v3 = c_siphash_rotate_left(state->v3, 21);
        state->v3 ^= state->v0;
        state->v2 += state->v1;
        state->v1 = c_siphash_rotate_left(state->v1, 17);
        state->v1 ^= state->v2;
        state->v2 = c_siphash_rotate_left(state->v2, 32);
}

/**
 * c_siphash_init() - initialize siphash context
 * @state:              context object
 * @seed:               128bit seed
 *
 * This initializes the siphash state context. Once initialized, it can be used
 * to hash arbitrary input. To feed data into it, use c_siphash_append(). To get
 * the final hash, use c_siphash_finalize().
 *
 * Note that the siphash context does not allocate state. There is no need to
 * deserialize it before releasing its backing memory.
 *
 * The hashes generated by this context change depending on the seed. Every
 * user is highly inclined to provide their unique seed. If no stable hashes
 * are needed, a random seed will do fine.
 *
 * Right now, only SipHash24 is supported. Other SipHash parameters can be
 * easily added if required.
 */
_c_public_ void c_siphash_init(CSipHash *state, const uint8_t seed[16]) {
        uint64_t k0, k1;

        k0 = c_siphash_read_le64(seed);
        k1 = c_siphash_read_le64(seed + 8);

        *state = (CSipHash) {
                /*
                 * Default seed is taken from the reference implementation
                 * of SipHash24 ("somepseudorandomlygeneratedbytes"). Callers
                 * are still recommended to provide proper seeds themselves.
                 */
                .v0 = 0x736f6d6570736575ULL ^ k0,
                .v1 = 0x646f72616e646f6dULL ^ k1,
                .v2 = 0x6c7967656e657261ULL ^ k0,
                .v3 = 0x7465646279746573ULL ^ k1,
                .padding = 0,
                .n_bytes = 0,
        };
}

/**
 * c_siphash_append() - hash stream of data
 * @state:              context object
 * @bytes:              array of input bytes
 * @n_bytes:            number of input bytes
 *
 * This feeds an array of bytes into the SipHash state machine. This is a
 * streaming-capable API. That is, the resulting hash is the same, regardless
 * of the way you chunk the input.
 * This function simply feeds the given bytes into the SipHash state machine.
 * It does not produce a final hash. You can call this function many times to
 * append more data. To retrieve the final hash, call c_siphash_finalize().
 *
 * Note that this implementation works best when used with chunk-sizes of
 * multiples of 64bit (8-bytes). This is not a requirement, though.
 */
_c_public_ void c_siphash_append(CSipHash *state, const uint8_t *bytes, size_t n_bytes) {
        const uint8_t *end = bytes + n_bytes;
        size_t left = state->n_bytes & 7;
        uint64_t m;

        state->n_bytes += n_bytes;

        /*
         * SipHash operates on 64bit chunks. If the previous blob was not a
         * multiple of 64bit in length, we must operate on single bytes.
         */
        if (left > 0) {
                for ( ; bytes < end && left < 8; ++bytes, ++left)
                        state->padding |= ((uint64_t) *bytes) << (left * 8);

                if (bytes == end && left < 8)
                        return;

                state->v3 ^= state->padding;
                c_siphash_sipround(state);
                c_siphash_sipround(state);
                state->v0 ^= state->padding;

                state->padding = 0;
        }

        end -= (state->n_bytes % sizeof(uint64_t));

        /*
         * We are now guaranteed to be at a 64bit state boundary. Hence, we can
         * operate in 64bit chunks on all input. This is much faster than the
         * one-byte-at-a-time loop.
         */
        for ( ; bytes < end; bytes += 8) {
                m = c_siphash_read_le64(bytes);

                state->v3 ^= m;
                c_siphash_sipround(state);
                c_siphash_sipround(state);
                state->v0 ^= m;
        }

        /*
         * Now that we hashed as much 64bit chunks as possible, we need to
         * remember the remaining trailing bytes. Keep them in @padding so the
         * next round (or the finalizer) get access to them.
         */
        left = state->n_bytes & 7;
        switch (left) {
                case 7:
                        state->padding |= ((uint64_t) bytes[6]) << 48;
                        /* fallthrough */
                case 6:
                        state->padding |= ((uint64_t) bytes[5]) << 40;
                        /* fallthrough */
                case 5:
                        state->padding |= ((uint64_t) bytes[4]) << 32;
                        /* fallthrough */
                case 4:
                        state->padding |= ((uint64_t) bytes[3]) << 24;
                        /* fallthrough */
                case 3:
                        state->padding |= ((uint64_t) bytes[2]) << 16;
                        /* fallthrough */
                case 2:
                        state->padding |= ((uint64_t) bytes[1]) <<  8;
                        /* fallthrough */
                case 1:
                        state->padding |= ((uint64_t) bytes[0]);
                        /* fallthrough */
                case 0:
                        break;
        }
}

/**
 * c_siphash_finalize() - finalize hash
 * @state:              context object
 *
 * This produces the final SipHash24 hash value for the given SipHash state.
 * That is, it produces a hash value corresponding to the SipHash24 hash value
 * of the concatenated byte-array passed into @state via c_siphash_append().
 *
 * Note that @state has an invalid state after this function returns. To reuse
 * it for another hash, you must call c_siphash_init() again. If you don't need
 * the object, anymore, you can release it any time. There is no need to
 * destroy the object explicitly.
 *
 * Return: 64bit hash value
 */
_c_public_ uint64_t c_siphash_finalize(CSipHash *state) {
        uint64_t b;

        b = state->padding | (((uint64_t) state->n_bytes) << 56);

        state->v3 ^= b;
        c_siphash_sipround(state);
        c_siphash_sipround(state);
        state->v0 ^= b;

        state->v2 ^= 0xff;

        c_siphash_sipround(state);
        c_siphash_sipround(state);
        c_siphash_sipround(state);
        c_siphash_sipround(state);

        return state->v0 ^ state->v1 ^ state->v2  ^ state->v3;
}

/**
 * c_siphash_hash() - hash data blob
 * @seed:               128bit seed
 * @bytes:              byte array to hash
 * @n_bytes:            number of bytes to hash
 *
 * This produces the SipHash24 hash value for the input @bytes / @n_bytes,
 * using the seed provided as @seed.
 *
 * This is functionally equivalent to:
 *
 *         CSipHash state;
 *         c_siphash_init(&state, seed);
 *         c_siphash_append(&state, bytes, n_bytes);
 *         return c_siphash_finalize(&state);
 *
 * Unlike the streaming API, this is a one-shot call suitable for any data that
 * is available in-memory at the same time.
 *
 * Return: 64bit hash value
 */
_c_public_ uint64_t c_siphash_hash(const uint8_t seed[16], const uint8_t *bytes, size_t n_bytes) {
        CSipHash state;

        c_siphash_init(&state, seed);
        c_siphash_append(&state, bytes, n_bytes);

        return c_siphash_finalize(&state);
}
