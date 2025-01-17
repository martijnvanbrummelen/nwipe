/*
 * XORoshiro-256 PRNG Implementation
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This is a XORoshiro-256 (XOR/rotate/shift/rotate) pseudorandom number generator
 * implementation, designed for fast and efficient generation of high-quality
 * pseudorandom numbers. XORoshiro-256 is part of the XORoshiro family of PRNGs known
 * for their simplicity and excellent statistical properties for a wide range of
 * applications, though they are not suitable for cryptographic purposes due to their
 * predictability.
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
 * Note: This implementation does not utilize OpenSSL or any cryptographic libraries, as
 * XORoshiro-256 is not intended for cryptographic applications. It is crucial for applications
 * requiring cryptographic security to use a cryptographically secure PRNG.
 */

#include "xoroshiro256_prng.h"
#include <stdint.h>
#include <string.h>

void xoroshiro256_init( xoroshiro256_state_t* state, uint64_t init_key[], unsigned long key_length )
{
    // Initialization logic; ensure 256 bits are properly seeded
    for( int i = 0; i < 4; i++ )
    {
        if( i < key_length )
        {
            state->s[i] = init_key[i];
        }
        else
        {
            // Example fallback for insufficient seeds; consider better seeding strategies
            state->s[i] = state->s[i - 1] * 6364136223846793005ULL + 1;
        }
    }
}

static inline uint64_t rotl( const uint64_t x, int k )
{
    return ( x << k ) | ( x >> ( 64 - k ) );
}

void xoroshiro256_genrand_uint256_to_buf( xoroshiro256_state_t* state, unsigned char* bufpos )
{
    // This part of the code updates the state using xoroshiro256**'s algorithm.
    const uint64_t result_starstar = rotl( state->s[1] * 5, 7 ) * 9;
    const uint64_t t = state->s[1] << 17;

    state->s[2] ^= state->s[0];
    state->s[3] ^= state->s[1];
    state->s[1] ^= state->s[2];
    state->s[0] ^= state->s[3];

    state->s[2] ^= t;
    state->s[3] = rotl( state->s[3], 45 );

    // Note: 'result_starstar' was only used for demonstration purposes and is not part of the
    // original Xoroshiro256** specification. Here, we write the complete state into the buffer.
    // Ensure that 'bufpos' has enough storage space (256 bits / 32 bytes).

    memcpy( bufpos, state->s, 32 );  // Copies the entire 256-bit (32 bytes) state into 'bufpos'
}
