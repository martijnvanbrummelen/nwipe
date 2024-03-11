#include "aes_ctr_prng.h"
#include <openssl/rand.h>
#include <string.h>
// #include <stdio.h> // Necessary for printf
#include <openssl/aes.h>
#include <openssl/modes.h>

void aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length )
{
    unsigned char key[16];  // Expanded to 128 Bit
    memset( key, 0, 16 );

    // printf("Original key length (in unsigned long units): %lu\n", key_length);
    // printf("Original key (64 Bit): %016lx\n", init_key[0]);

    // Repeat the 64-bit key to create a 128-bit key.
    for( size_t i = 0; i < 16; i++ )
    {
        key[i] = ( (unsigned char*) init_key )[i % 8];
    }

    AES_set_encrypt_key( key, 128, &state->aes_key );  // 128 Bit key
    memset( state->ivec, 0, AES_BLOCK_SIZE );
    state->num = 0;
    memset( state->ecount, 0, AES_BLOCK_SIZE );
}

static void next_state( aes_ctr_state_t* state )
{
    for( int i = 0; i < AES_BLOCK_SIZE; ++i )
    {
        if( ++state->ivec[i] )
            break;
    }
}

unsigned long aes_ctr_prng_genrand_uint32( aes_ctr_state_t* state )
{
    unsigned long result = 0;

    CRYPTO_ctr128_encrypt( (unsigned char*) &result,
                           (unsigned char*) &result,
                           sizeof( result ),
                           &state->aes_key,
                           state->ivec,
                           state->ecount,
                           &state->num,
                           (block128_f) AES_encrypt );
    next_state( state );  // Ensure this function does not cause errors

    return result & 0xFFFFFFFF;
}
