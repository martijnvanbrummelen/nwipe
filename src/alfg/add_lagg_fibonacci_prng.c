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
 * Note: This implementation is designed for non-cryptographic applications and should not be
 * used where cryptographic security is required.
 */

#include "add_lagg_fibonacci_prng.h"
#include <string.h> // For memset, if needed

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
void add_lagg_fibonacci_init(add_lagg_fibonacci_state_t* state, uint64_t init_key[], unsigned long key_length)
{
    unsigned long i;
    uint32_t seed = 19650218UL;  // Default seed value if no key is provided

    /*
     * Initialize the state array with the provided key.
     * Each 64-bit key is split into two 32-bit values to populate the state array.
     */
    if (key_length > 0)
    {
        for (i = 0; i < key_length && i < STATE_SIZE; i++)
        {
            /* Extract the lower 32 bits of the current key and assign to the state */
            state->s[i] = (uint32_t)(init_key[i] & 0xFFFFFFFFUL);
            
            /* If there's room, extract the upper 32 bits of the current key for the next state element */
            if (++i < STATE_SIZE)
                state->s[i] = (uint32_t)(init_key[i - 1] >> 32);
        }
    }
    else
    {
        /* If no key is provided, initialize the first state element with the default seed */
        state->s[0] = seed;
        i = 1;
    }

    /*
     * Populate the remaining state array using a linear congruential generator (LCG).
     * The LCG parameters are chosen to suit 32-bit systems for efficient computation.
     */
    for (; i < STATE_SIZE; i++)
    {
        /* Apply the LCG formula: s[i] = (1812433253 * (s[i-1] ^ (s[i-1] >> 30)) + i) modulo 2^32 */
        state->s[i] = (1812433253UL * (state->s[i - 1] ^ (state->s[i - 1] >> 30)) + i) & MASK;
    }

    /* Initialize the index to start generating random numbers from the beginning of the state array */
    state->index = 0;
}

/*
 * Generates 256 bits (32 bytes) of pseudorandom data and writes it to the provided buffer.
 * The function produces eight 32-bit random numbers, concatenating them to form the 256-bit output.
 *
 * Parameters:
 * - state: Pointer to the initialized ALFG state structure.
 * - bufpos: Pointer to the buffer where the generated random data will be stored.
 */
void add_lagg_fibonacci_genrand_uint256_to_buf(add_lagg_fibonacci_state_t* state, unsigned char* bufpos)
{
    uint32_t* buf_as_uint32 = (uint32_t*)bufpos;  // Cast buffer position to a 32-bit integer pointer

    /* 
     * Generate eight 32-bit random numbers to form 256 bits of random data.
     * Each iteration computes a new state value and stores it in both the state array and the output buffer.
     */
    for (int i = 0; i < 8; i++)
    {
        /*
         * Calculate the indices for the two lagged values using bitmasking for efficient modulo operation.
         * This is possible because STATE_SIZE is a power of two.
         */
        int idx_a = (state->index + LAG_BIG) & (STATE_SIZE - 1);    // Index for the larger lag
        int idx_b = (state->index + LAG_SMALL) & (STATE_SIZE - 1);  // Index for the smaller lag

        /* 
         * Perform the additive Fibonacci operation:
         * new_value = (s[idx_a] + s[idx_b]) modulo 2^32
         */
        uint32_t result = (state->s[idx_a] + state->s[idx_b]) & MASK;

        /* 
         * Update the current state with the newly generated value.
         * This maintains the state array for future random number generations.
         */
        state->s[state->index] = result;

        /* 
         * Store the generated 32-bit random number into the output buffer.
         * This is done by writing directly to the buffer interpreted as a 32-bit integer array.
         */
        buf_as_uint32[i] = result;

        /* 
         * Advance the index, wrapping around using bitmasking to maintain it within STATE_SIZE.
         * This prepares the generator for the next round of random number generation.
         */
        state->index = (state->index + 1) & (STATE_SIZE - 1);
    }
}

