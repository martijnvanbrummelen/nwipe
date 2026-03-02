/*
 * xorshift128plus.h: Xorshift128+ PRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 */
#ifndef XORSHIFT128PLUS_H
#define XORSHIFT128PLUS_H

#include <stdint.h>
#include <stddef.h>

/*
 * xorshift128plus_state_t - Xorshift128+ PRNG state.
 *
 * s[0], s[1]  128-bit state (can never be zero)
 *
 * One per thread.  Do not share across threads.
 */
typedef struct xorshift128plus_state_s
{
    uint64_t s[2]; /* 128-bit state */
} xorshift128plus_state_t;

/*
 * xorshift128plus_prng_init - Initialize state from a seed buffer of at least 16 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int xorshift128plus_prng_init( xorshift128plus_state_t* state, const uint8_t seed[], size_t seed_length );

/*
 * xorshift128plus_prng_genrand_to_buf - Generate len bytes of output into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int xorshift128plus_prng_genrand_to_buf( xorshift128plus_state_t* state, uint8_t* out, size_t len );

#endif /* XORSHIFT128PLUS_H */
