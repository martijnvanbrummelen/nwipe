/*
 * splitmix64.c: SplitMix64 PRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 */

#include "splitmix64.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Returns one 64-bit pseudorandom value */
static inline uint64_t splitmix64_next( splitmix64_state_t* state )
{
    uint64_t z = ( state->s += 0x9E3779B97F4A7C15ULL );
    z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
    z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
    return z ^ ( z >> 31 );
}

/*
 * splitmix64_prng_init - Initialize state from a seed buffer of at least 8 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int splitmix64_prng_init( splitmix64_state_t* state, const uint8_t seed[], size_t seed_length )
{
    if( !state || !seed || seed_length < 8 )
        return -1;

    memcpy( &state->s, seed, 8 );

    return 0;
}

/*
 * splitmix64_prng_genrand_to_buf - Generate len bytes of output into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int splitmix64_prng_genrand_to_buf( splitmix64_state_t* state, uint8_t* out, size_t len )
{
    if( !state || !out )
        return -1;

    size_t i = 0;

    for( ; i + 8 <= len; i += 8 )
    {
        uint64_t r = splitmix64_next( state );
        memcpy( out + i, &r, 8 );
    }

    if( i < len )
    {
        uint64_t r = splitmix64_next( state );
        memcpy( out + i, &r, len - i );
    }

    return 0;
}
