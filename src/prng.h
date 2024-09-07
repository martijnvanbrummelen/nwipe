/*
 *  prng.h: Pseudo Random Number Generator abstractions for nwipe.
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
 *
 */

#ifndef PRNG_H_
#define PRNG_H_

#include <sys/types.h>

/* A chunk of random data. */
typedef struct
{
    size_t length;  // Length of the entropy string in bytes.
    u8* s;  // The actual bytes of the entropy string.
} nwipe_entropy_t;

#define NWIPE_PRNG_INIT_SIGNATURE void **state, nwipe_entropy_t *seed
#define NWIPE_PRNG_READ_SIGNATURE void **state, void *buffer, size_t count

/* Function pointers for PRNG actions. */
typedef int ( *nwipe_prng_init_t )( NWIPE_PRNG_INIT_SIGNATURE );
typedef int ( *nwipe_prng_read_t )( NWIPE_PRNG_READ_SIGNATURE );

/* The generic PRNG definition. */
typedef struct
{
    const char* label;  // The name of the pseudo random number generator.
    nwipe_prng_init_t init;  // Inialize the prng state with the seed.
    nwipe_prng_read_t read;  // Read data from the prng.
} nwipe_prng_t;

/* Mersenne Twister prototypes. */
int nwipe_twister_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_twister_read( NWIPE_PRNG_READ_SIGNATURE );

/* ISAAC prototypes. */
int nwipe_isaac_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_isaac_read( NWIPE_PRNG_READ_SIGNATURE );
int nwipe_isaac64_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_isaac64_read( NWIPE_PRNG_READ_SIGNATURE );

/* ALFG prototypes. */
int nwipe_add_lagg_fibonacci_prng_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_add_lagg_fibonacci_prng_read( NWIPE_PRNG_READ_SIGNATURE );

/* XOROSHIRO-256 prototypes. */
int nwipe_xoroshiro256_prng_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_xoroshiro256_prng_read( NWIPE_PRNG_READ_SIGNATURE );

/* RC4 prototypes. */
int nwipe_rc4_prng_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_rc4_prng_read( NWIPE_PRNG_READ_SIGNATURE );

/* Size of the twister is not derived from the architecture, but it is strictly 4 bytes */
#define SIZE_OF_TWISTER 4

/* Size of the isaac/isaac64 is not derived from the architecture, but it is strictly 4 or 8 bytes */
#define SIZE_OF_ISAAC 4
#define SIZE_OF_ISAAC64 8

/* Size of the Lagged Fibonacci generator is not derived from the architecture, but it is strictly 32 bytes */
#define SIZE_OF_ADD_LAGG_FIBONACCI_PRNG 32

/* Size of the XOROSHIRO-256 is not derived from the architecture, but it is strictly 32 bytes */
#define SIZE_OF_XOROSHIRO256_PRNG 32

/* Size of the RC4 is not derived from the architecture, but it is strictly 4096 bytes */
#define SIZE_OF_RC4_PRNG 4096

#endif /* PRNG_H_ */
