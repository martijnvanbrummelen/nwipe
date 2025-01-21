/*
 * Additive Lagged Fibonacci Generator (ALFG) Implementation definitions
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This is an implementation of the Additive Lagged Fibonacci Generator (ALFG),
 * a pseudorandom number generator known for its simplicity and good statistical properties
 * for a wide range of applications. ALFGs are particularly noted for their long periods
 * and efficiency in generating sequences of random numbers. However, like many other PRNGs,
 * they are not suitable for cryptographic purposes due to their predictability.
 *
 * As the author of this implementation, I, Fabian Druschke, hereby release this work into
 * the public domain. I dedicate any and all copyright interest in this work to the public
 * domain, making it free to use for anyone for any purpose without any conditions, unless
 * such conditions are required by law.
 *
 * This software is provided "as is", without warranty of any kind, express or implied,
 * including but not limited to the warranties of merchantability, fitness for a particular
 * purpose, and noninfringement. In no event shall the authors be liable for any claim,
 * damages, or other liability, whether in an action of contract, tort, or otherwise, arising
 * from, out of, or in connection with the software or the use or other dealings in the software.
 *
 * Note: This implementation is designed for non-cryptographic applications and should not be
 * used where cryptographic security is required.
 */

#ifndef ADD_LAGG_FIBONACCI_PRNG_H
#define ADD_LAGG_FIBONACCI_PRNG_H

#include <stdint.h>

// State definition for the Additive Lagged Fibonacci Generator
typedef struct
{
    uint64_t s[64];  // State array
    unsigned int index;  // Current index in the state array
} add_lagg_fibonacci_state_t;

// Function prototypes
void add_lagg_fibonacci_init( add_lagg_fibonacci_state_t* state, uint64_t init_key[], unsigned long key_length );
void add_lagg_fibonacci_genrand_uint256_to_buf( add_lagg_fibonacci_state_t* state, unsigned char* bufpos );

#endif  // ADD_LAGG_FIBONACCI_PRNG_H
