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
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "nwipe.h"
#include "prng.h"
#include "logging.h"

#include "mt19937ar-cok.h"
#include "isaac_rand.h"

nwipe_prng_t nwipe_twister = {"Mersenne Twister (mt19937ar-cok)", nwipe_twister_init, nwipe_twister_read};

nwipe_prng_t nwipe_isaac = {"ISAAC (rand.c 20010626)", nwipe_isaac_init, nwipe_isaac_read};

int nwipe_u32tobuffer( u8* buffer, u32 rand, int len )
{
    /*
     * Print given number of bytes from unsigned integer number to a byte stream buffer starting with low-endian.
     */
    int i;
    u8 c;  // single char
    if( len > sizeof( u32 ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Tried to print longer number than the value passed." );
        len = sizeof( u32 );
    }

    for( i = 0; i < len; i++ )
    {
        c = rand & 0xFFUL;
        rand = rand >> 8;
        buffer[i] = c;
    }
    return 0;
}

int nwipe_twister_init( NWIPE_PRNG_INIT_SIGNATURE )
{
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
    u32 i = 0;
    u32 ii;
    u32 words = count / SIZE_OF_TWISTER;  // the values of twister_genrand_int32 is strictly 4 bytes
    u32 remain = count % SIZE_OF_TWISTER;  // the values of twister_genrand_int32 is strictly 4 bytes

    /* Twister returns 4-bytes per call, so progress by 4 bytes. */
    for( ii = 0; ii < words; ++ii )
    {
        nwipe_u32tobuffer( (u8*) ( buffer + i ), twister_genrand_int32( (twister_state_t*) *state ), SIZE_OF_TWISTER );
        i = i + SIZE_OF_TWISTER;
    }

    /* If there is some remainder copy only relevant number of bytes to not
     * overflow the buffer. */
    if( remain > 0 )
    {
        nwipe_u32tobuffer( (u8*) ( buffer + i ), twister_genrand_int32( (twister_state_t*) *state ), remain );
    }

    return 0;
}

int nwipe_isaac_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    int count;
    randctx* isaac_state = *state;

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
    /* The purpose of this function is unclear, as it does not do anything except immediately return !
     * Because the variables in the macro NWIPE_PRNG_READ_SIGNATURE were then unused this throws
     * up a handful of compiler warnings, related to variables being unused. To stop the compiler warnings
     * I've simply put in a (void) var so that compiler sees the variable are supposed to be unused.
     *
     * As this code works, I thought it best not to remove this function, just in case it serves
     * some purpose or is there for future use.
     */

    (void) state;
    (void) buffer;
    (void) count;

    return 0;
}
