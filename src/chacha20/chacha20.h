/*
 * chacha20.h: ChaCha20 stream cipher CSPRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 *
 * This implementation is intentionally kept simple and portable.
 * It should not require special hardware and run about everywhere.
 *
 * Hardware-specific accelerations are discouraged and, if present,
 * were not added by the original author. Anyone with 15 minutes of
 * time and the RFC should be able to understand and audit the code.
 */

#ifndef CHACHA20_H
#define CHACHA20_H

#include <stdint.h>
#include <stddef.h>

/*
 * chacha20_state_t - ChaCha20 CSPRNG state.
 *
 * s[0..3]    constants
 * s[4..11]   256-bit key
 * s[12..13]  64-bit block counter (low, high)
 * s[14..15]  64-bit nonce
 *
 * One per thread. Do not share across threads.
 */
typedef struct chacha20_state_s
{
    uint32_t s[16]; /* state words (see above) */
    uint8_t keystream_buf[64]; /* buffered keystream block */
    size_t keystream_pos; /* next unused byte (0-63; 64 = empty) */
} chacha20_state_t;

/*
 * chacha20_self_test - Run all ChaCha20 test vectors.
 *
 * Returns:  0     all tests passed
 *           1..N  one-based index of the first failing vector
 *          -1     fatal error (bad hex data, allocation failure, etc.)
 */
int chacha20_self_test( void );

/*
 * chacha20_prng_init - Initialize state from a seed buffer of at least 40 bytes.
 *
 * Returns:  0  success
 *          -1  failure (null pointer or insufficient seed length)
 */
int chacha20_prng_init( chacha20_state_t* state, const uint8_t seed[], size_t seed_length );

/*
 * chacha20_prng_genrand_to_buf - Generate len bytes of keystream into out.
 *
 * Returns:  0  success
 *          -1  failure (null pointer)
 */
int chacha20_prng_genrand_to_buf( chacha20_state_t* state, uint8_t* out, size_t len );

#endif /* CHACHA20_H */
