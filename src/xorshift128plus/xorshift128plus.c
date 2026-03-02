/*
 * xorshift128plus.c: Xorshift128+ PRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 */

#include "xorshift128plus.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Read a little-endian uint64_t from a byte buffer. */
static inline uint64_t load_le64( const uint8_t* p )
{
    return (uint64_t) p[0] | (uint64_t) p[1] << 8 | (uint64_t) p[2] << 16 | (uint64_t) p[3] << 24
        | (uint64_t) p[4] << 32 | (uint64_t) p[5] << 40 | (uint64_t) p[6] << 48 | (uint64_t) p[7] << 56;
}

/* Write a uint64_t as little-endian bytes. */
static inline void store_le64( uint8_t* p, uint64_t v )
{
    p[0] = (uint8_t) ( v );
    p[1] = (uint8_t) ( v >> 8 );
    p[2] = (uint8_t) ( v >> 16 );
    p[3] = (uint8_t) ( v >> 24 );
    p[4] = (uint8_t) ( v >> 32 );
    p[5] = (uint8_t) ( v >> 40 );
    p[6] = (uint8_t) ( v >> 48 );
    p[7] = (uint8_t) ( v >> 56 );
}

/* Returns one 64-bit pseudorandom value and advances the state. */
static inline uint64_t xorshift128plus_next( xorshift128plus_state_t* state )
{
    uint64_t a = state->s[0];
    uint64_t b = state->s[1];

    state->s[0] = b;
    a ^= a << 23;
    a ^= a >> 18;
    a ^= b;
    a ^= b >> 5;
    state->s[1] = a;

    return a + b;
}

/*
 * xorshift128plus_prng_init - Initialize state from a seed buffer of at least 16 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int xorshift128plus_prng_init( xorshift128plus_state_t* state, const uint8_t seed[], size_t seed_length )
{
    if( state == NULL || seed == NULL || seed_length < 16 )
    {
        return -1;
    }

    state->s[0] = load_le64( seed );
    state->s[1] = load_le64( seed + 8 );

    if( state->s[0] == 0 && state->s[1] == 0 )
    {
        return -1;
    }

    return 0;
}

/*
 * xorshift128plus_prng_genrand_to_buf - Generate len bytes of output into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int xorshift128plus_prng_genrand_to_buf( xorshift128plus_state_t* state, uint8_t* out, size_t len )
{
    if( state == NULL || out == NULL )
    {
        return -1;
    }

    while( len >= 8 )
    {
        store_le64( out, xorshift128plus_next( state ) );
        out += 8;
        len -= 8;
    }

    if( len > 0 )
    {
        uint8_t tmp[8];
        store_le64( tmp, xorshift128plus_next( state ) );
        memcpy( out, tmp, len );
    }

    return 0;
}
