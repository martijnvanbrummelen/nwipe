/*
 * RC4 PRNG Header File
 * Author: [Your Name]
 * Date: 2024-09-07
 *
 * This header file provides function declarations and data structures for the
 * RC4-based pseudorandom number generator implementation. The RC4 algorithm
 * is not suitable for cryptographic purposes but can be used for non-secure
 * pseudorandom data generation.
 *
 * As the author of this header file, I, [Your Name], hereby release this work into
 * the public domain. I dedicate any and all copyright interest in this work to the public
 * domain, making it free to use for anyone for any purpose without any conditions, unless
 * such conditions are required by law.
 *
 * This software is provided "as is", without warranty of any kind, express or implied,
 * including but not limited to the warranties of merchantability, fitness for a particular
 * purpose, and noninfringement. In no event shall the authors be liable for any claim,
 * damages, or other liability, whether in an action of contract, tort, or otherwise, arising
 * from, out of, or in connection with the software or the use or other dealings in the software.
 */

#ifndef RC4_PRNG_H
#define RC4_PRNG_H

#include <stdint.h>

// Constants
#define RC4_KEY_LENGTH 256  // Size of the S-Box
#define OUTPUT_DATA_LENGTH 4096  // Amount of random data to generate (4096 bytes)

// RC4 key structure to hold the S-Box and indices
typedef struct rc4_state_s
{
    unsigned char S[RC4_KEY_LENGTH];  // S-Box (Permutation table)
    unsigned char i, j;  // Indices for the key scheduling
} rc4_state_t;

// Function to initialize the RC4 key with the given key material
// init_key: The initial key used to seed the RC4 PRNG
// key_length: The length of the init_key in 64-bit blocks
void rc4_init( rc4_state_t* state, uint64_t init_key[], unsigned long key_length );

// Function to generate 4096 random bytes and write them into the provided buffer
// bufpos: The buffer where the generated random bytes will be written
void rc4_genrand_4096_to_buf( rc4_state_t* state, unsigned char* bufpos );
void rc4_sse4_genrand( rc4_state_t* state, unsigned char* bufpos );
void rc4_avx2_genrand( rc4_state_t* state, unsigned char* bufpos );

#endif  // RC4_PRNG_H
