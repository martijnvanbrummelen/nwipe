/*
 * RC4 PRNG Implementation (Optimized with AVX2 for nwipe)
 * Original RC4 Algorithm Author: Ron Rivest (1987)
 * Adaptation Author: Fabian Druschke
 * Date: 2024-09-07
 *
 * This version of the RC4 PRNG is optimized using AVX2 instructions for enhanced performance.
 * It generates pseudorandom data for the nwipe project and is not intended for cryptographic purposes.
 *
 * Disclaimer: This software is provided "as is", without warranty of any kind, express or implied.
 */

#include "rc4_prng.h"
#include <stdint.h>
#include <string.h>
#include <immintrin.h>  // For AVX2 support

// Initialize the RC4 key
void rc4_init( rc4_state_t* state, uint64_t init_key[], unsigned long key_length )
{
    int i, j = 0;
    unsigned char k[RC4_KEY_LENGTH];

    // Convert init_key into a byte array (k) and fill the rest if key_length is smaller
    for( i = 0; i < RC4_KEY_LENGTH; i++ )
    {
        if( i < key_length * sizeof( uint64_t ) )
        {
            k[i] = ( (unsigned char*) init_key )[i];
        }
        else
        {
            // Fallback in case of insufficient key length
            k[i] = k[i - 1] * 6364136223846793005ULL + 1;
        }
    }

    // Initialize the S-Box with identity permutation
    for( i = 0; i < RC4_KEY_LENGTH; i++ )
    {
        state->S[i] = i;
    }

    // Permute the S-Box based on the key
    for( i = 0; i < RC4_KEY_LENGTH; i++ )
    {
        j = ( j + state->S[i] + k[i] ) % RC4_KEY_LENGTH;
        unsigned char temp = state->S[i];
        state->S[i] = state->S[j];
        state->S[j] = temp;
    }

    state->i = 0;
    state->j = 0;
}

// Generate 4096 random bytes and write them into the buffer bufpos
void rc4_genrand_4096_to_buf( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer

    // Loop unrolling and prefetching for performance optimization
    unsigned long n;
    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 4 )
    {
        _mm_prefetch( (const char*) &state->S[state->i + 16], _MM_HINT_T0 );

        // Generate the next 4 bytes
        for( int i = 0; i < 4; i++ )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            temp_buffer[n + i] = state->S[( state->S[state->i] + state->S[state->j] ) % RC4_KEY_LENGTH];
        }
    }

    memcpy( bufpos, temp_buffer, OUTPUT_DATA_LENGTH );
}

// Generate 4096 random bytes and write them into the buffer bufpos with SSE 4.2
void rc4_sse4_genrand( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer for generated random data

    __m128i sse_temp_buffer;  // 128-bit SIMD register for storing 16 bytes of data at a time
    unsigned long n;

    // Loop through the output buffer in blocks of 16 bytes for SSE4 processing
    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 16 )
    {
        // Prefetch the next block of memory to improve cache performance
        _mm_prefetch( (const char*) &state->S[state->i + 16], _MM_HINT_T0 );

        // Process 16 bytes at a time using SSE4 support
        for( int i = 0; i < 16; i += 4 )
        {
            // Update the 'i' index for the RC4 algorithm, wrapping around at the key length
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;

            // Update the 'j' index for the RC4 algorithm, wrapping around at the key length
            state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;

            // Swap state bytes based on the updated indices
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;

            // Generate the next random byte using the RC4 algorithm
            temp_buffer[n + i] = state->S[( state->S[state->i] + state->S[state->j] ) % RC4_KEY_LENGTH];
        }

        // Use SSE4 instructions to copy the generated 16 bytes in parallel
        sse_temp_buffer = _mm_loadu_si128( (__m128i*) &temp_buffer[n] );
        _mm_storeu_si128( (__m128i*) ( bufpos + n ), sse_temp_buffer );
    }
}

// AVX2-optimized version for parallel byte generation
void rc4_avx2_genrand( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer

    __m256i avx2_temp_buffer;
    unsigned long n;

    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 32 )
    {
        _mm_prefetch( (const char*) &state->S[state->i + 32], _MM_HINT_T0 );

        // Process 32 bytes at a time (using AVX2)
        for( int i = 0; i < 32; i += 4 )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            temp_buffer[n + i] = state->S[( state->S[state->i] + state->S[state->j] ) % RC4_KEY_LENGTH];
        }

        // AVX2: Load and store the generated bytes in parallel
        avx2_temp_buffer = _mm256_loadu_si256( (__m256i*) &temp_buffer[n] );
        _mm256_storeu_si256( (__m256i*) ( bufpos + n ), avx2_temp_buffer );
    }
}
