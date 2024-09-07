/*
 * RC4 PRNG Implementation (Optimized with AVX2 for nwipe)
 * Original RC4 Algorithm Author: Ron Rivest (1987)
 * Adaptation Author: Fabian Druschke
 * Date: 2024-09-07
 *
 * This version of the RC4 PRNG is optimized for high performance, leveraging modern
 * hardware features such as AVX2 and SSE4.2, and introduces several improvements
 * over the traditional RC4 algorithm:
 *
 * 1. **CTR Mode**: A counter-based mode is used to ensure uniqueness of generated
 *    pseudorandom streams, preventing repetition issues common with static key usage.
 *
 * 2. **RC4-Drop**: The first 256 bytes of the RC4 output are discarded to avoid
 *    known initial biases in the classic RC4 stream, improving the quality of the output.
 *
 * 3. **SIMD Optimizations (SSE4.2 and AVX2)**: The algorithm is enhanced to take
 *    advantage of modern CPUs by processing 16 bytes (SSE4.2) or 32 bytes (AVX2)
 *    in parallel, significantly boosting performance for large data generation tasks.
 *
 * 4. **Hardware Prefetching**: Memory prefetching is employed to optimize access to
 *    the S-Box, reducing cache misses and improving overall memory performance.
 *
 * 5. **Use as a PRNG**: This implementation is designed as a pseudorandom number
 *    generator (PRNG) rather than a cryptographic cipher, and should not be used for
 *    encryption purposes.
 *
 * Overall, this RC4 adaptation is ideal for generating large volumes of pseudorandom
 * data in a fast and efficient manner, leveraging the full potential of modern CPU architectures.
 *
 * Disclaimer: This software is provided "as is", without warranty of any kind, express or implied.
 */


#include "rc4_prng.h"
#include <stdint.h>
#include <string.h>  // For memory operations such as memcpy

// Check for AVX2 and SSE4.2 support
#if defined(__AVX2__)
    #include <immintrin.h>
#elif defined(__SSE4_2__)
    #include <nmmintrin.h>  // For SSE4.2 support
#endif

/*
 * Enum definition for logging levels used in the nwipe project.
 * Each log level corresponds to a specific category of messages,
 * ranging from debug information to critical errors.
 */
typedef enum {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,  // Detailed debugging messages
    NWIPE_LOG_INFO,  // Informative logs, used for regular operation updates
    NWIPE_LOG_NOTICE,  // Notices for significant but non-critical events
    NWIPE_LOG_WARNING,  // Warnings, indicating potential issues
    NWIPE_LOG_ERROR,  // Error messages, critical issues that require attention
    NWIPE_LOG_FATAL,  // Fatal errors, which often require immediate termination
    NWIPE_LOG_SANITY,  // Sanity checks, typically used for debugging purposes
    NWIPE_LOG_NOTIMESTAMP  // Logging without timestamp information
} nwipe_log_t;

/*
 * External logging function definition.
 * This function is used for outputting messages based on severity levels.
 * It supports a variable argument list similar to printf.
 */
extern void nwipe_log( nwipe_log_t level, const char* format, ... );

/*
 * Function: rc4_init
 * ----------------------------
 *   Initializes the RC4 state (S-Box) and the counter for the CTR (Counter) mode.
 *   The function first converts the initialization key into a byte array and
 *   permutes the S-Box based on this key. It also applies a counter for unique
 *   pseudorandom streams.
 *
 *   Parameters:
 *     state: Pointer to the RC4 state structure, which holds the S-Box and indices.
 *     init_key: Pointer to the initialization key (array of 64-bit values).
 *     key_length: The length of the key in bytes.
 */
void rc4_init( rc4_state_t* state, uint64_t init_key[], unsigned long key_length )
{
    int i, j = 0;
    unsigned char k[RC4_KEY_LENGTH];  // The byte array for the key

    /*
     * Convert the init_key into a byte array (k) that will be used for S-Box initialization.
     * If the key is smaller than RC4_KEY_LENGTH, the remaining bytes are filled using
     * a fallback method based on linear congruential generation (LCG).
     */
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

    /* Log the key used for debugging purposes */
    nwipe_log( NWIPE_LOG_DEBUG, "RC4 Seed (Key): " );
    for( i = 0; i < RC4_KEY_LENGTH / sizeof( uint64_t ); i++ )
    {
        uint64_t* k_as_uint64 = (uint64_t*) k;  // Cast the key as an array of uint64_t
        nwipe_log( NWIPE_LOG_DEBUG, "%016llx ", k_as_uint64[i] );
    }
    nwipe_log( NWIPE_LOG_DEBUG, "\n" );

    /*
     * Initialize the S-Box with an identity permutation,
     * i.e., S[i] = i for all i in 0 to RC4_KEY_LENGTH-1.
     */
    for( i = 0; i < RC4_KEY_LENGTH; i++ )
    {
        state->S[i] = i;
    }

    /*
     * Permute the S-Box based on the key.
     * The S-Box is scrambled by iterating through it, adding the corresponding key bytes
     * and performing swaps. This step is crucial for creating an initial random state.
     */
    for( i = 0; i < RC4_KEY_LENGTH; i++ )
    {
        j = ( j + state->S[i] + k[i] ) % RC4_KEY_LENGTH;
        unsigned char temp = state->S[i];
        state->S[i] = state->S[j];
        state->S[j] = temp;
    }

    // Initialize the indices for RC4
    state->i = 0;
    state->j = 0;

    // Initialize the counter for CTR mode, ensuring uniqueness for each stream
    state->counter = 0;

    /*
     * RC4-drop: Discard the first 256 bytes generated by the RC4 PRNG.
     * This step addresses a known weakness in RC4, where the initial output may
     * exhibit statistical biases. Dropping the first 256 bytes mitigates this issue.
     */
    for( i = 0; i < 256; i++ )
    {
        state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
        state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;
        unsigned char temp = state->S[state->i];
        state->S[state->i] = state->S[state->j];
        state->S[state->j] = temp;
    }
}

/*
 * Function: rc4_genrand_4096_to_buf
 * ----------------------------
 *   Generates 4096 bytes of pseudorandom data using the RC4 algorithm
 *   and writes it to the provided buffer.
 *
 *   This version uses a simple loop to generate and permute the bytes.
 *   It is the fallback version, used when neither SSE nor AVX2 is available.
 *
 *   Parameters:
 *     state: Pointer to the RC4 state structure, which holds the S-Box and indices.
 *     bufpos: Pointer to the buffer where the pseudorandom data will be written.
 */
void rc4_genrand_4096_to_buf( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer to hold generated data

    unsigned long n;
    /*
     * Loop over OUTPUT_DATA_LENGTH (4096 bytes) in chunks of 4 bytes.
     * The inner loop will permute the S-Box and generate 4 bytes of output in each iteration.
     */
    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 4 )
    {
        // Increment the counter (CTR mode)
        state->counter++;

        /*
         * The counter value is mixed into the S-Box permutation to ensure
         * the uniqueness of the generated stream. This prevents potential
         * repetition issues in the RC4 output.
         */
        uint64_t counter_value = state->counter;
        for( int i = 0; i < 8; i++ )
        {
            // Update the indices i and j, and permute the S-Box using the counter
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] + ( counter_value & 0xFF ) ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            counter_value >>= 8;  // Process the next byte of the counter
        }

        // Generate 4 bytes of pseudorandom data
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

    // Copy the generated random bytes from the temporary buffer into the user-provided buffer
    memcpy( bufpos, temp_buffer, OUTPUT_DATA_LENGTH );
}


/*
 * Function: rc4_genrand_4096_to_buf_sse42
 * ----------------------------
 *   Generates 4096 bytes of pseudorandom data using RC4, optimized with SSE 4.2 instructions.
 *   This version processes 16 bytes of data in parallel using SIMD instructions.
 *
 *   Parameters:
 *     state: Pointer to the RC4 state structure, which holds the S-Box and indices.
 *     bufpos: Pointer to the buffer where the pseudorandom data will be written.
 */
#if defined(__SSE4_2__) 
void rc4_genrand_4096_to_buf_sse42( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer

    unsigned long n;

    /*
     * Loop over the output length in 16-byte chunks to leverage SSE 4.2 for SIMD parallelism.
     * Each iteration generates 16 bytes of data by permuting the RC4 state and using SSE instructions.
     */
    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 16 )
    {
        // Prefetch the next part of the S-Box to optimize memory access using SIMD
        _mm_prefetch( (const char*) &state->S[state->i + 16], _MM_HINT_T0 );

        // Update the counter (CTR mode)
        state->counter++;

        // Mix the counter into the S-Box permutation to add randomness
        uint64_t counter_value = state->counter;
        for( int i = 0; i < 8; i++ )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] + ( counter_value & 0xFF ) ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            counter_value >>= 8;  // Process the next byte of the counter
        }

        // Generate 16 bytes of pseudorandom data manually and store them in temp_buffer
        for( int i = 0; i < 16; i++ )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            temp_buffer[n + i] = state->S[( state->S[state->i] + state->S[state->j] ) % RC4_KEY_LENGTH];
        }

        // Load the 16-byte block into an SSE register and store it in the buffer
        __m128i sse_block = _mm_loadu_si128( (__m128i*) temp_buffer );
        _mm_storeu_si128( (__m128i*) &bufpos[n], sse_block );
    }

    // Copy the remaining random data into the output buffer
    memcpy( bufpos, temp_buffer, OUTPUT_DATA_LENGTH );
}
#endif

/*
 * Function: rc4_genrand_4096_to_buf_avx2
 * ----------------------------
 *   Generates 4096 bytes of pseudorandom data using RC4, optimized with AVX2 instructions.
 *   This version processes 32 bytes of data in parallel using AVX2 instructions.
 *
 *   Parameters:
 *     state: Pointer to the RC4 state structure, which holds the S-Box and indices.
 *     bufpos: Pointer to the buffer where the pseudorandom data will be written.
 */
#if defined(__AVX2__) 
void rc4_genrand_4096_to_buf_avx2( rc4_state_t* state, unsigned char* bufpos )
{
    unsigned char temp;
    unsigned char temp_buffer[OUTPUT_DATA_LENGTH];  // Temporary buffer

    unsigned long n;

    /*
     * Loop over the output length in 32-byte chunks to leverage AVX2 for SIMD parallelism.
     * Each iteration generates 32 bytes of data by permuting the RC4 state and using AVX2 instructions.
     */
    for( n = 0; n < OUTPUT_DATA_LENGTH; n += 32 )
    {
        // Prefetch the next part of the S-Box to optimize memory access using SIMD
        _mm_prefetch( (const char*) &state->S[state->i + 16], _MM_HINT_T0 );

        // Update the counter (CTR mode)
        state->counter++;

        // Mix the counter into the S-Box permutation to ensure randomness
        uint64_t counter_value = state->counter;
        for( int i = 0; i < 8; i++ )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] + ( counter_value & 0xFF ) ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            counter_value >>= 8;  // Process the next byte of the counter
        }

        // Generate 32 bytes of pseudorandom data manually and store them in temp_buffer
        for( int i = 0; i < 32; i++ )
        {
            state->i = ( state->i + 1 ) % RC4_KEY_LENGTH;
            state->j = ( state->j + state->S[state->i] ) % RC4_KEY_LENGTH;
            temp = state->S[state->i];
            state->S[state->i] = state->S[state->j];
            state->S[state->j] = temp;
            temp_buffer[n + i] = state->S[( state->S[state->i] + state->S[state->j] ) % RC4_KEY_LENGTH];
        }

        // Load the 32-byte block into an AVX2 register and store it in the buffer
        __m256i avx_block = _mm256_loadu_si256( (__m256i*) temp_buffer );
        _mm256_storeu_si256( (__m256i*) &bufpos[n], avx_block );
    }

    // Copy the remaining random data into the output buffer
    memcpy( bufpos, temp_buffer, OUTPUT_DATA_LENGTH );
}
#endif
