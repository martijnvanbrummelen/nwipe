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

#include "mt19937ar-cok/mt19937ar-cok.h"
#include "isaac_rand/isaac_rand.h"
#include "isaac_rand/isaac64.h"
#include "aes/aes_ctr_prng.h" // AES-NI prototype

nwipe_prng_t nwipe_twister = { "Mersenne Twister (mt19937ar-cok)", nwipe_twister_init, nwipe_twister_read };

nwipe_prng_t nwipe_isaac = { "ISAAC (rand.c 20010626)", nwipe_isaac_init, nwipe_isaac_read };
nwipe_prng_t nwipe_isaac64 = { "ISAAC-64 (isaac64.c)", nwipe_isaac64_init, nwipe_isaac64_read };

/* AES-CTR-NI PRNG Structure */
nwipe_prng_t nwipe_aes_ctr_prng = { "AES-CTR-PRNG", nwipe_aes_ctr_prng_init, nwipe_aes_ctr_prng_read };


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


/* EXPERIMENTAL implementation of AES-128 in counter mode to provide high-quality random numbers */

int nwipe_aes_ctr_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising AES CTR PRNG" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( aes_ctr_state_t )  );
    }
    aes_ctr_prng_init( (aes_ctr_state_t*) *state, (unsigned long*) ( seed->s ), seed->length / sizeof( unsigned long ) );

    return 0;
}

int nwipe_aes_ctr_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_AES_CTR_PRNG;  // the values of aes_ctr_prng_genrand_uint32 is strictly 4 bytes

    /* AES CTR PRNG returns 4-bytes per call, so progress by 4 bytes. */
    for( size_t ii = 0; ii < words; ++ii )
    {
        u32_to_buffer( bufpos, aes_ctr_prng_genrand_uint32( (aes_ctr_state_t*) *state ), SIZE_OF_AES_CTR_PRNG );
        bufpos += SIZE_OF_AES_CTR_PRNG;
    }

    /* If there is some remainder copy only relevant number of bytes to not overflow the buffer. */
    const size_t remain = count % SIZE_OF_AES_CTR_PRNG;  // SIZE_OF_AES_CTR_PRNG is strictly 4 bytes
    if( remain > 0 )
    {
        u32_to_buffer( bufpos, aes_ctr_prng_genrand_uint32( (aes_ctr_state_t*) *state ), remain );
    }

    return 0;
}


