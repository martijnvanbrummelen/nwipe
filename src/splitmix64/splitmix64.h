/*
 * splitmix64.c: SplitMix64 PRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 */
#ifndef SPLITMIX64_H
#define SPLITMIX64_H

#include <stdint.h>
#include <stddef.h>

/*
 * splitmix64_state_t - SplitMix64 PRNG state.
 * One per thread. Do not share across threads.
 */
typedef struct splitmix64_state_s
{
    uint64_t s; /* 64-bit state */
} splitmix64_state_t;

/*
 * splitmix64_prng_init - Initialize state from a seed buffer of at least 8 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int splitmix64_prng_init( splitmix64_state_t* state, const uint8_t seed[], size_t seed_length );

/*
 * splitmix64_prng_genrand_to_buf - Generate len bytes of output into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int splitmix64_prng_genrand_to_buf( splitmix64_state_t* state, uint8_t* out, size_t len );

#endif /* SPLITMIX64_H */
