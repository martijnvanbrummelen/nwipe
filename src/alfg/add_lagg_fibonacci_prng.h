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

/* 
 * Constants defining the state size and lag values.
 * STATE_SIZE must be a power of two for efficient indexing using bitmasking.
 */
#define STATE_SIZE 64           // Size of the state array, ensuring a long period
#define LAG_BIG 55              // Larger lag value for the Fibonacci operation
#define LAG_SMALL 24            // Smaller lag value for the Fibonacci operation
#define MASK 0xFFFFFFFFUL       // Mask to enforce 32-bit overflow

/*
 * Structure representing the state of the ALFG.
 * - s: Array holding the state values.
 * - index: Current position in the state array.
 */
typedef struct {
    uint32_t s[STATE_SIZE];    // State array containing the internal state
    uint32_t index;            // Current index for generating the next number
} add_lagg_fibonacci_state_t;

/*
 * Initializes the ALFG state with a given key.
 * If the key length is insufficient to fill the state array, a linear congruential generator (LCG)
 * is used to populate the remaining state values.
 *
 * Parameters:
 * - state: Pointer to the ALFG state structure to be initialized.
 * - init_key: Array of 64-bit integers used as the initial key.
 * - key_length: Number of elements in the init_key array.
 */
void add_lagg_fibonacci_init(add_lagg_fibonacci_state_t* state, uint64_t init_key[], unsigned long key_length);

/*
 * Generates 256 bits (32 bytes) of pseudorandom data and writes it to the provided buffer.
 * The function produces eight 32-bit random numbers, concatenating them to form the 256-bit output.
 *
 * Parameters:
 * - state: Pointer to the initialized ALFG state structure.
 * - bufpos: Pointer to the buffer where the generated random data will be stored.
 */
void add_lagg_fibonacci_genrand_uint256_to_buf(add_lagg_fibonacci_state_t* state, unsigned char* bufpos);

#endif // ADD_LAGG_FIBONACCI_PRNG_H

