/*
 * XORoshiro-256 PRNG Definitions
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This header file contains definitions for the XORoshiro-256 pseudorandom number generator
 * (PRNG) implementation. XORoshiro-256 is part of the XORoshiro family of PRNGs, known for
 * their simplicity, efficiency, and high-quality pseudorandom number generation suitable for
 * a wide range of applications, excluding cryptographic purposes due to its predictable nature.
 *
 * As the author of this work, I, Fabian Druschke, hereby release this work into the public
 * domain. I dedicate any and all copyright interest in this work to the public domain, making
 * it free to use for anyone for any purpose without any conditions, unless such conditions are
 * required by law.
 *
 * This software is provided "as is", without warranty of any kind, express or implied,
 * including but not limited to the warranties of merchantability, fitness for a particular
 * purpose, and noninfringement. In no event shall the authors be liable for any claim,
 * damages, or other liability, whether in an action of contract, tort, or otherwise, arising
 * from, out of, or in connection with the software or the use or other dealings in the software.
 *
 * Note: This implementation does not utilize any cryptographic libraries, as XORoshiro-256 is
 * not intended for cryptographic applications. It is crucial for applications requiring
 * cryptographic security to use a cryptographically secure PRNG.
 */

#ifndef XOROSHIRO256_PRNG_H
#define XOROSHIRO256_PRNG_H

#include <stdint.h>

// Structure to store the state of the xoroshiro256** random number generator
typedef struct xoroshiro256_state_s
{
    uint64_t s[4];
} xoroshiro256_state_t;

// Initializes the xoroshiro256** random number generator with a seed
void xoroshiro256_init( xoroshiro256_state_t* state, uint64_t init_key[], unsigned long key_length );

// Generates a 256-bit random number using xoroshiro256** and stores it directly in the output buffer
void xoroshiro256_genrand_uint256_to_buf( xoroshiro256_state_t* state, unsigned char* bufpos );

#endif  // XOROSHIRO256_PRNG_H
