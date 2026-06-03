/*
 * chacha20_test.c: ChaCha20 stream cipher CSPRNG for nwipe.
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
#include "chacha20_test.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Load a 32-bit word from 4 bytes in little-endian order. */
static inline uint32_t load32_le( const uint8_t* p )
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* Setup state for the respective test vector. */
static void setup_state( chacha20_state_t* state, const uint8_t key[32], const uint8_t nonce[8], uint64_t counter )
{
    memset( state, 0, sizeof( *state ) );

    state->s[0] = 0x61707865u;
    state->s[1] = 0x3320646eu;
    state->s[2] = 0x79622d32u;
    state->s[3] = 0x6b206574u;

    for( int i = 0; i < 8; i++ )
        state->s[4 + i] = load32_le( key + i * 4 );

    state->s[12] = (uint32_t) ( counter & 0xFFFFFFFF );
    state->s[13] = (uint32_t) ( counter >> 32 );
    state->s[14] = load32_le( nonce + 0 );
    state->s[15] = load32_le( nonce + 4 );

    state->keystream_pos = 64;
}

/* Run a test vector (-1 = init failure, 0 = success, 1 = test failure). */
static int run_test( const chacha20_test_vector_t* tv )
{
    uint8_t key[32];
    uint8_t nonce[8];

    for( size_t i = 0; i < 32; i++ ) /* Key */
    {
        unsigned int byte;
        if( sscanf( tv->key_hex + 2 * i, "%2x", &byte ) != 1 )
        {
            return -1;
        }
        key[i] = (uint8_t) byte;
    }

    for( size_t i = 0; i < 8; i++ ) /* Nonce */
    {
        unsigned int byte;
        if( sscanf( tv->nonce_hex + 2 * i, "%2x", &byte ) != 1 )
        {
            return -1;
        }
        nonce[i] = (uint8_t) byte;
    }

    size_t len = strlen( tv->plaintext_hex ) / 2;

    uint8_t* pt = malloc( len );
    uint8_t* exp = malloc( len );
    uint8_t* got = malloc( len );

    if( !pt || !exp || !got )
    {
        free( pt );
        free( exp );
        free( got );
        return -1;
    }

    for( size_t i = 0; i < len; i++ ) /* Plaintext */
    {
        unsigned int byte;
        if( sscanf( tv->plaintext_hex + 2 * i, "%2x", &byte ) != 1 )
        {
            free( pt );
            free( exp );
            free( got );
            return -1;
        }
        pt[i] = (uint8_t) byte;
    }

    for( size_t i = 0; i < len; i++ ) /* Ciphertext */
    {
        unsigned int byte;
        if( sscanf( tv->ciphertext_hex + 2 * i, "%2x", &byte ) != 1 )
        {
            free( pt );
            free( exp );
            free( got );
            return -1;
        }
        exp[i] = (uint8_t) byte;
    }

    chacha20_state_t state;
    setup_state( &state, key, nonce, tv->counter );
    chacha20_prng_genrand_to_buf( &state, got, len );

    /* Keystream ^= Ciphertext -> Plaintext */
    for( size_t i = 0; i < len; i++ )
        got[i] ^= pt[i];

    int passed = ( memcmp( got, exp, len ) == 0 );

    free( pt );
    free( exp );
    free( got );

    return passed ? 0 : 1;
}

/*
 * chacha20_self_test - Run all ChaCha20 test vectors.
 *
 * Returns:  0     all tests passed
 *           1..N  one-based index of the first failing vector
 *          -1     fatal error (bad hex data, allocation failure, etc.)
 */
int chacha20_self_test( void )
{
    for( size_t i = 0; i < chacha20_test_vectors_count; i++ )
    {
        int rc = run_test( &chacha20_test_vectors[i] );

        if( rc == -1 )
            return -1;

        if( rc == 1 )
            return (int) ( i + 1 );
    }

    return 0;
}
