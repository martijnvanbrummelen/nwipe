/*
 *  prng.c: Pseudo Random Number Generator abstractions for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "nwipe.h"
#include "prng.h"
#include "context.h"
#include "logging.h"
#include <stdio.h>

#include "mt19937ar-cok/mt19937ar-cok.h"
#include "isaac_rand/isaac_rand.h"
#include "isaac_rand/isaac64.h"
#include "alfg/add_lagg_fibonacci_prng.h"  //Lagged Fibonacci generator prototype
#include "xor/xoroshiro256_prng.h"  //XORoshiro-256 prototype
#include "rc4/rc4_prng.h"  //RC4 protoype

nwipe_prng_t nwipe_twister = { "Mersenne Twister (mt19937ar-cok)", nwipe_twister_init, nwipe_twister_read };

nwipe_prng_t nwipe_isaac = { "ISAAC (rand.c 20010626)", nwipe_isaac_init, nwipe_isaac_read };
nwipe_prng_t nwipe_isaac64 = { "ISAAC-64 (isaac64.c)", nwipe_isaac64_init, nwipe_isaac64_read };

/* ALFG PRNG Structure */
nwipe_prng_t nwipe_add_lagg_fibonacci_prng = { "Lagged Fibonacci generator",
                                               nwipe_add_lagg_fibonacci_prng_init,
                                               nwipe_add_lagg_fibonacci_prng_read };
/* XOROSHIRO-256 PRNG Structure */
nwipe_prng_t nwipe_xoroshiro256_prng = { "XORoshiro-256", nwipe_xoroshiro256_prng_init, nwipe_xoroshiro256_prng_read };

/* RC4 PRNG Structure */
nwipe_prng_t nwipe_rc4_prng = { "RC4", nwipe_rc4_prng_init, nwipe_rc4_prng_read };

#if defined( __AVX2__ ) || defined( __SSE4_2__ )
#include <cpuid.h>
#if defined( __AVX2__ )
#include <immintrin.h>  // For _xgetbv and AVX intrinsics
#endif

// Function to check if SSE4.2 is supported
int check_sse42_support()
{
    uint32_t eax, ebx, ecx, edx;
    __cpuid( 1, eax, ebx, ecx, edx );

    // Check bit 20 of ECX register for SSE4.2 support
    return ( ecx & ( 1 << 20 ) ) != 0;
}

// Function to check if AVX2 is supported
int check_avx2_support()
{
#if defined( __AVX2__ )
    uint32_t eax, ebx, ecx, edx;

    // First check if OS supports XGETBV and AVX
    __cpuid( 1, eax, ebx, ecx, edx );

    // Check if the OS uses XSAVE/XRSTOR to manage XMM and YMM state
    if( ( ecx & ( 1 << 27 ) ) == 0 )
    {
        return 0;  // AVX not supported
    }

    // Check if XGETBV indicates the OS supports XMM, YMM state
    uint64_t xcr_feature_mask = _xgetbv( 0 );
    if( ( xcr_feature_mask & 0x6 ) != 0x6 )
    {
        return 0;  // AVX not enabled in the OS
    }

    // Check if AVX2 is supported (bit 5 of EBX from CPUID leaf 7)
    __cpuid_count( 7, 0, eax, ebx, ecx, edx );
    return ( ebx & ( 1 << 5 ) ) != 0;
#else
    return 0;  // AVX2 not supported by this compiler or platform
#endif
}

#else

// Fallback if neither AVX2 nor SSE4.2 is available or supported by the compiler/platform
int check_sse42_support()
{
    return 0;  // SSE4.2 is not supported
}

int check_avx2_support()
{
    return 0;  // AVX2 is not supported
}

#endif

/* Print given number of bytes from unsigned integer number to a byte stream buffer starting with low-endian. */
static inline void u32_to_buffer( u8* restrict buffer, u32 val, const int len )
{
    for( int i = 0; i < len; ++i )
    {
        buffer[i] = (u8) ( val & 0xFFUL );
        val >>= 8;
    }
}
static inline void u64_to_buffer( u8* restrict buffer, u64 val, const int len )
{
    for( int i = 0; i < len; ++i )
    {
        buffer[i] = (u8) ( val & 0xFFULL );
        val >>= 8;
    }
}
static inline u32 isaac_nextval( randctx* restrict ctx )
{
    if( ctx->randcnt == 0 )
    {
        isaac( ctx );
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}
static inline u64 isaac64_nextval( rand64ctx* restrict ctx )
{
    if( ctx->randcnt == 0 )
    {
        isaac64( ctx );
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}

int nwipe_twister_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising Mersenne Twister prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( twister_state_t ) );
    }
    twister_init( (twister_state_t*) *state, (u32*) ( seed->s ), seed->length / sizeof( u32 ) );
    return 0;
}

int nwipe_twister_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_TWISTER;  // the values of twister_genrand_int32 is strictly 4 bytes

    /* Twister returns 4-bytes per call, so progress by 4 bytes. */
    for( size_t ii = 0; ii < words; ++ii )
    {
        u32_to_buffer( bufpos, twister_genrand_int32( (twister_state_t*) *state ), SIZE_OF_TWISTER );
        bufpos += SIZE_OF_TWISTER;
    }

    /* If there is some remainder copy only relevant number of bytes to not
     * overflow the buffer. */
    const size_t remain = count % SIZE_OF_TWISTER;  // SIZE_OF_TWISTER is strictly 4 bytes
    if( remain > 0 )
    {
        u32_to_buffer( bufpos, twister_genrand_int32( (twister_state_t*) *state ), remain );
    }

    return 0;
}

int nwipe_isaac_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    int count;
    randctx* isaac_state = *state;

    nwipe_log( NWIPE_LOG_NOTICE, "Initialising Isaac prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( randctx ) );
        isaac_state = *state;

        /* Check the memory allocation. */
        if( isaac_state == 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "malloc" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the isaac state." );
            return -1;
        }
    }

    /* Take the minimum of the isaac seed size and available entropy. */
    if( sizeof( isaac_state->randrsl ) < seed->length )
    {
        count = sizeof( isaac_state->randrsl );
    }
    else
    {
        memset( isaac_state->randrsl, 0, sizeof( isaac_state->randrsl ) );
        count = seed->length;
    }

    if( count == 0 )
    {
        /* Start ISACC without a seed. */
        randinit( isaac_state, 0 );
    }
    else
    {
        /* Seed the ISAAC state with entropy. */
        memcpy( isaac_state->randrsl, seed->s, count );

        /* The second parameter indicates that randrsl is non-empty. */
        randinit( isaac_state, 1 );
    }

    return 0;
}

int nwipe_isaac_read( NWIPE_PRNG_READ_SIGNATURE )
{
    randctx* isaac_state = *state;
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC;  // the values of isaac is strictly 4 bytes

    /* Isaac returns 4-bytes per call, so progress by 4 bytes. */
    for( size_t ii = 0; ii < words; ++ii )
    {
        /* get the next 32bit random number */
        u32_to_buffer( bufpos, isaac_nextval( isaac_state ), SIZE_OF_ISAAC );
        bufpos += SIZE_OF_ISAAC;
    }

    /* If there is some remainder copy only relevant number of bytes to not overflow the buffer. */
    const size_t remain = count % SIZE_OF_ISAAC;  // SIZE_OF_ISAAC is strictly 4 bytes
    if( remain > 0 )
    {
        u32_to_buffer( bufpos, isaac_nextval( isaac_state ), remain );
    }

    return 0;
}

int nwipe_isaac64_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    int count;
    rand64ctx* isaac_state = *state;

    nwipe_log( NWIPE_LOG_NOTICE, "Initialising ISAAC-64 prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( rand64ctx ) );
        isaac_state = *state;

        /* Check the memory allocation. */
        if( isaac_state == 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "malloc" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the isaac state." );
            return -1;
        }
    }

    /* Take the minimum of the isaac seed size and available entropy. */
    if( sizeof( isaac_state->randrsl ) < seed->length )
    {
        count = sizeof( isaac_state->randrsl );
    }
    else
    {
        memset( isaac_state->randrsl, 0, sizeof( isaac_state->randrsl ) );
        count = seed->length;
    }

    if( count == 0 )
    {
        /* Start ISACC without a seed. */
        rand64init( isaac_state, 0 );
    }
    else
    {
        /* Seed the ISAAC state with entropy. */
        memcpy( isaac_state->randrsl, seed->s, count );

        /* The second parameter indicates that randrsl is non-empty. */
        rand64init( isaac_state, 1 );
    }

    return 0;
}

int nwipe_isaac64_read( NWIPE_PRNG_READ_SIGNATURE )
{
    rand64ctx* isaac_state = *state;
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC64;  // the values of ISAAC-64 is strictly 8 bytes

    for( size_t ii = 0; ii < words; ++ii )
    {
        u64_to_buffer( bufpos, isaac64_nextval( isaac_state ), SIZE_OF_ISAAC64 );
        bufpos += SIZE_OF_ISAAC64;
    }

    /* If there is some remainder copy only relevant number of bytes to not overflow the buffer. */
    const size_t remain = count % SIZE_OF_ISAAC64;  // SIZE_OF_ISAAC64 is strictly 8 bytes
    if( remain > 0 )
    {
        u64_to_buffer( bufpos, isaac64_nextval( isaac_state ), remain );
    }

    return 0;
}

/* EXPERIMENTAL implementation of Lagged Fibonacci generator a lot of random numbers */
int nwipe_add_lagg_fibonacci_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    if( *state == NULL )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "Initialising Lagged Fibonacci generator PRNG" );
        *state = malloc( sizeof( add_lagg_fibonacci_state_t ) );
    }
    add_lagg_fibonacci_init(
        (add_lagg_fibonacci_state_t*) *state, (uint64_t*) ( seed->s ), seed->length / sizeof( uint64_t ) );

    return 0;
}

/* EXPERIMENTAL implementation of XORoroshiro256 algorithm to provide high-quality, but a lot of random numbers */
int nwipe_xoroshiro256_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising XORoroshiro-256 PRNG" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( xoroshiro256_state_t ) );
    }
    xoroshiro256_init( (xoroshiro256_state_t*) *state, (uint64_t*) ( seed->s ), seed->length / sizeof( uint64_t ) );

    return 0;
}

int nwipe_add_lagg_fibonacci_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;

    /* Loop to fill the buffer with blocks directly from the Fibonacci algorithm */
    for( size_t ii = 0; ii < words; ++ii )
    {
        add_lagg_fibonacci_genrand_uint256_to_buf( (add_lagg_fibonacci_state_t*) *state, bufpos );
        bufpos += SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;  // Move to the next block
    }

    /* Handle remaining bytes if count is not a multiple of SIZE_OF_ADD_LAGG_FIBONACCI_PRNG */
    const size_t remain = count % SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;
    if( remain > 0 )
    {
        unsigned char temp_output[16];  // Temporary buffer for the last block
        add_lagg_fibonacci_genrand_uint256_to_buf( (add_lagg_fibonacci_state_t*) *state, temp_output );

        // Copy the remaining bytes
        memcpy( bufpos, temp_output, remain );
    }

    return 0;  // Success
}

int nwipe_xoroshiro256_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_XOROSHIRO256_PRNG;

    /* Loop to fill the buffer with blocks directly from the XORoroshiro256 algorithm */
    for( size_t ii = 0; ii < words; ++ii )
    {
        xoroshiro256_genrand_uint256_to_buf( (xoroshiro256_state_t*) *state, bufpos );
        bufpos += SIZE_OF_XOROSHIRO256_PRNG;  // Move to the next block
    }

    /* Handle remaining bytes if count is not a multiple of SIZE_OF_XOROSHIRO256_PRNG */
    const size_t remain = count % SIZE_OF_XOROSHIRO256_PRNG;
    if( remain > 0 )
    {
        unsigned char temp_output[16];  // Temporary buffer for the last block
        xoroshiro256_genrand_uint256_to_buf( (xoroshiro256_state_t*) *state, temp_output );

        // Copy the remaining bytes
        memcpy( bufpos, temp_output, remain );
    }

    return 0;  // Success
}

int nwipe_rc4_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising RC4 PRNG" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( rc4_state_t ) );
    }
    rc4_init( (rc4_state_t*) *state, (uint64_t*) ( seed->s ), seed->length / sizeof( uint64_t ) );

    return 0;
}

// The main RC4 PRNG read function with AVX2 and SSE4.2 detection
int nwipe_rc4_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;  // Buffer position pointer
    size_t words = count / SIZE_OF_RC4_PRNG;  // Number of 4096-byte blocks

    // Check if the CPU supports AVX2 or SSE4.2
    int use_avx2 = check_avx2_support();
    int use_sse4 = check_sse42_support();

    /* Loop to fill the buffer with blocks directly from the RC4 algorithm */
    for( size_t ii = 0; ii < words; ++ii )
    {
        if( use_avx2 )
        {
#if defined( __AVX2__ )
            // Use AVX2-optimized version
            rc4_genrand_4096_to_buf_avx2( (rc4_state_t*) *state, bufpos );
#else
            // Fallback to generic version if AVX2 is not compiled
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, bufpos );
#endif
        }
        else if( use_sse4 )
        {
#if defined( __SSE4_2__ )
            // Use SSE4.2-optimized version
            rc4_genrand_4096_to_buf_sse42( (rc4_state_t*) *state, bufpos );
#else
            // Fallback to generic version if SSE4.2 is not compiled
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, bufpos );
#endif
        }
        else
        {
            // Fallback to generic version
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, bufpos );
        }
        bufpos += SIZE_OF_RC4_PRNG;  // Move to the next block
    }

    /* Handle remaining bytes if count is not a multiple of SIZE_OF_RC4_PRNG */
    const size_t remain = count % SIZE_OF_RC4_PRNG;
    if( remain > 0 )
    {
        unsigned char temp_output[SIZE_OF_RC4_PRNG];  // Temporary buffer for the last block

        if( use_avx2 )
        {
#if defined( __AVX2__ )
            // Use AVX2-optimized version
            rc4_genrand_4096_to_buf_avx2( (rc4_state_t*) *state, temp_output );
#else
            // Fallback to generic version if AVX2 is not compiled
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, temp_output );
#endif
        }
        else if( use_sse4 )
        {
#if defined( __SSE4_2__ )
            // Use SSE4.2-optimized version
            rc4_genrand_4096_to_buf_sse42( (rc4_state_t*) *state, temp_output );
#else
            // Fallback to generic version if SSE4.2 is not compiled
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, temp_output );
#endif
        }
        else
        {
            // Fallback to generic version
            rc4_genrand_4096_to_buf( (rc4_state_t*) *state, temp_output );
        }

        // Copy the remaining bytes to the buffer
        memcpy( bufpos, temp_output, remain );
    }

    return 0;  // Success
}
