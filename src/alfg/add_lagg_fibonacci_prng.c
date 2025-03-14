/*
 * Additive Lagged Fibonacci Generator (ALFG) Implementation
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
 * Actually it uses subtraction as the core operation, making it conceptually closer to a
 * "Subtractive Lagged Fibonacci Generator" (SLFG) variant, but well..
 *
 * Note: This implementation is designed for non-cryptographic applications and should not be
 * used where cryptographic security is required.
 */

#include "add_lagg_fibonacci_prng.h"
#include <stdint.h>
#include <string.h>

#define STATE_SIZE 64  // Size of the state array, sufficient for a high period
#define LAG_BIG 55  // Large lag, e.g., 55
#define LAG_SMALL 24  // Small lag, e.g., 24
#define MODULUS ( 1ULL << 48 )  // Modulus for the operations, here 2^48 for simple handling

void add_lagg_fibonacci_init( add_lagg_fibonacci_state_t* state, uint64_t init_key[], unsigned long key_length )
{
    // Simple initialization: Fill the state with the key values and then with a linear combination of them
    for( unsigned long i = 0; i < STATE_SIZE; i++ )
    {
        if( i < key_length )
        {
            state->s[i] = init_key[i];
        }
        else
        {
            // Simple method to generate further state values. Should be improved for serious applications.
            state->s[i] = ( 6364136223846793005ULL * state->s[i - 1] + 1 ) % MODULUS;
        }
    }
    state->index = 0;  // Initialize the index for the first generation
}

void add_lagg_fibonacci_genrand_uint256_to_buf( add_lagg_fibonacci_state_t* state, unsigned char* bufpos )
{
    uint64_t* buf_as_uint64 = (uint64_t*) bufpos;  // Interprets bufpos as a uint64_t array for direct assignment
    int64_t result;  // Use signed integer to handle potential negative results from subtraction

    for (int i = 0; i < 4; i++) {
        // Subtract the two previous numbers in the sequence
        result = (int64_t)state->s[(state->index + LAG_BIG) % STATE_SIZE] - (int64_t)state->s[(state->index + LAG_SMALL) % STATE_SIZE];

        // Handle borrow if result is negative
        if (result < 0) {
            result += MODULUS;
            // Optionally set a borrow flag or adjust the next operation based on borrow logic
        }

        // Store the result (after adjustment) back into the state, ensuring it's positive and within range
        state->s[state->index] = (uint64_t)result;

        // Write the result into buf_as_uint64
        buf_as_uint64[i] = state->s[state->index];

        // Update the index for the next round
        state->index = (state->index + 1) % STATE_SIZE;
    }
}

