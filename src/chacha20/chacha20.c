/*
 * chacha20.c: ChaCha20 stream cipher CSPRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 *
 * This implementation is intentionally kept simple and portable.
 * It should not require special hardware and run about everywhere.
 *
 * Hardware-specific accelerations are discouraged and, if present,
 * were not added by the original author. Anyone with 15 minutes of
 * time and the RFC should be able to understand and audit the code.
 */

#include "chacha20.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define ROTL32( v, n ) ( ( ( v ) << ( n ) ) | ( ( v ) >> ( 32 - ( n ) ) ) )

#define QUARTERROUND( a, b, c, d )                                                                                     \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = ROTL32( d, 16 );                                                                                               \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = ROTL32( b, 12 );                                                                                               \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = ROTL32( d, 8 );                                                                                                \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = ROTL32( b, 7 );

#define CC0 0x61707865u
#define CC1 0x3320646eu
#define CC2 0x79622d32u
#define CC3 0x6b206574u

/* Load a 32-bit word from 4 bytes in little-endian order. */
static inline uint32_t load32_le( const uint8_t* p )
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* Store a 32-bit word as 4 bytes in little-endian order. */
static inline void store32_le( uint8_t* p, uint32_t v )
{
    p[0] = (uint8_t) ( v );
    p[1] = (uint8_t) ( v >> 8 );
    p[2] = (uint8_t) ( v >> 16 );
    p[3] = (uint8_t) ( v >> 24 );
}

/* Produce one 64-byte keystream block. */
static inline void block( const chacha20_state_t* state, uint8_t out[64] )
{
    uint32_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
    uint32_t j0, j1, j2, j3, j4, j5, j6, j7, j8, j9, j10, j11, j12, j13, j14, j15;
    int i;

    j0 = state->s[0];
    j1 = state->s[1];
    j2 = state->s[2];
    j3 = state->s[3];
    j4 = state->s[4];
    j5 = state->s[5];
    j6 = state->s[6];
    j7 = state->s[7];
    j8 = state->s[8];
    j9 = state->s[9];
    j10 = state->s[10];
    j11 = state->s[11];
    j12 = state->s[12];
    j13 = state->s[13];
    j14 = state->s[14];
    j15 = state->s[15];

    x0 = j0;
    x1 = j1;
    x2 = j2;
    x3 = j3;
    x4 = j4;
    x5 = j5;
    x6 = j6;
    x7 = j7;
    x8 = j8;
    x9 = j9;
    x10 = j10;
    x11 = j11;
    x12 = j12;
    x13 = j13;
    x14 = j14;
    x15 = j15;

#if defined( __clang__ )
#pragma unroll 10
#elif defined( __GNUC__ )
#pragma GCC unroll 10
#endif
    for( i = 10; i > 0; --i ) /* 10 x 2 (column + diagonal) = 20 */
    {
        /* Column rounds */
        QUARTERROUND( x0, x4, x8, x12 )
        QUARTERROUND( x1, x5, x9, x13 )
        QUARTERROUND( x2, x6, x10, x14 )
        QUARTERROUND( x3, x7, x11, x15 )

        /* Diagonal rounds */
        QUARTERROUND( x0, x5, x10, x15 )
        QUARTERROUND( x1, x6, x11, x12 )
        QUARTERROUND( x2, x7, x8, x13 )
        QUARTERROUND( x3, x4, x9, x14 )
    }

    store32_le( out + 0, x0 + j0 );
    store32_le( out + 4, x1 + j1 );
    store32_le( out + 8, x2 + j2 );
    store32_le( out + 12, x3 + j3 );
    store32_le( out + 16, x4 + j4 );
    store32_le( out + 20, x5 + j5 );
    store32_le( out + 24, x6 + j6 );
    store32_le( out + 28, x7 + j7 );
    store32_le( out + 32, x8 + j8 );
    store32_le( out + 36, x9 + j9 );
    store32_le( out + 40, x10 + j10 );
    store32_le( out + 44, x11 + j11 );
    store32_le( out + 48, x12 + j12 );
    store32_le( out + 52, x13 + j13 );
    store32_le( out + 56, x14 + j14 );
    store32_le( out + 60, x15 + j15 );
}

/* Advance the 64-bit block counter (s[12] = low, s[13] = high). */
static inline void counter_increment( chacha20_state_t* state )
{
    state->s[12] += 1;
    if( state->s[12] == 0 )
        state->s[13] += 1; /* carry */
}

/*
 * chacha20_prng_init - Initialize state from a seed buffer of at least 40 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int chacha20_prng_init( chacha20_state_t* state, const uint8_t seed[], size_t seed_length )
{
    if( !state || !seed )
        return -1;

    /* 32 bytes key + 8 bytes nonce */
    if( seed_length < 40 )
        return -1;

    memset( state, 0, sizeof( *state ) );

    /* s[0..3]: constants */
    state->s[0] = CC0;
    state->s[1] = CC1;
    state->s[2] = CC2;
    state->s[3] = CC3;

    /* s[4..11]: 256-bit key from seed bytes 0-31 */
    for( unsigned int i = 0; i < 8; ++i )
        state->s[4 + i] = load32_le( seed + i * 4 );

    /* s[12..13]: 64-bit counter (initialized to 0) */
    state->s[12] = 0;
    state->s[13] = 0;

    /* s[14..15]: 64-bit nonce from seed bytes 32-39 */
    state->s[14] = load32_le( seed + 32 );
    state->s[15] = load32_le( seed + 36 );

    /* Mark keystream buffer as empty */
    state->keystream_pos = 64;

    return 0;
}

/*
 * chacha20_prng_genrand_to_buf - Generate len bytes of keystream into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int chacha20_prng_genrand_to_buf( chacha20_state_t* state, uint8_t* out, size_t len )
{
    if( !state || !out )
        return -1;

    /* Drain existing keystream buffer (the leftovers) */
    if( state->keystream_pos < 64 && len > 0 )
    {
        size_t avail = 64 - state->keystream_pos;
        size_t take = ( len < avail ) ? len : avail;

        memcpy( out, state->keystream_buf + state->keystream_pos, take );

        state->keystream_pos += take;
        out += take;
        len -= take;
    }

    /* Block-sized fast path (no copying required) */
    while( len >= 64 )
    {
        block( state, out );
        counter_increment( state );

        out += 64;
        len -= 64;
    }

    /* Fetch a re-usable new block for the tail (remainder) */
    if( len > 0 )
    {
        block( state, state->keystream_buf );
        counter_increment( state );

        memcpy( out, state->keystream_buf, len );
        state->keystream_pos = len;
    }

    return 0;
}
