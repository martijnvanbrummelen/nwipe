/*
 * AES CTR PRNG Definitions
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This header file contains definitions for the AES (Advanced Encryption Standard)
 * implementation in CTR (Counter) mode for pseudorandom number generation, utilizing
 * OpenSSL for cryptographic functions.
 *
 * As the author of this work, I, Fabian Druschke, hereby release this work into the public
 * domain. I dedicate any and all copyright interest in this work to the public domain,
 * making it free to use for anyone for any purpose without any conditions, unless such
 * conditions are required by law.
 *
 * This software is provided "as is", without warranty of any kind, express or implied,
 * including but not limited to the warranties of merchantability, fitness for a particular
 * purpose and noninfringement. In no event shall the authors be liable for any claim,
 * damages or other liability, whether in an action of contract, tort or otherwise, arising
 * from, out of or in connection with the software or the use or other dealings in the software.
 *
 * USAGE OF OPENSSL IN THIS SOFTWARE:
 * This software uses OpenSSL for cryptographic operations. Users are responsible for
 * ensuring compliance with OpenSSL's licensing terms.
 */

#ifndef AES_CTR_PRNG_H
#define AES_CTR_PRNG_H

#include <stdint.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

// Structure to store the state of the AES-CTR random number generator
typedef struct
{
    EVP_CIPHER_CTX* ctx;
    unsigned char ivec[AES_BLOCK_SIZE];
    unsigned int num;
    unsigned char ecount[AES_BLOCK_SIZE];
} aes_ctr_state_t;

/**
 * Initializes the AES-CTR random number generator.
 *
 * @param state Pointer to the AES CTR PRNG state structure.
 * @param init_key Array containing the seed for key generation.
 * @param key_length Length of the seed array.
 * @return int Returns 0 on success, -1 on failure.
 */
int aes_ctr_prng_init(aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length);

/**
 * Generates a 256-bit random number using AES-CTR and stores it directly in the output buffer.
 *
 * @param state Pointer to the initialized AES CTR PRNG state.
 * @param bufpos Target buffer where the pseudorandom numbers will be written.
 * @return int Returns 0 on success, -1 on failure.
 */
int aes_ctr_prng_genrand_uint256_to_buf(aes_ctr_state_t* state, unsigned char* bufpos);

/**
 * Validates the pseudorandom data generation by generating test data and performing statistical tests.
 *
 * @param state Pointer to the initialized AES CTR PRNG state.
 * @return int Returns 0 on success, -1 on failure.
 */
int aes_ctr_prng_validate(aes_ctr_state_t* state);

/**
 * General cleanup function for AES CTR PRNG.
 *
 * @param state Pointer to the AES CTR PRNG state structure.
 * @return int Returns 0 on success.
 */
int aes_ctr_prng_general_cleanup(aes_ctr_state_t* state);

#endif  // AES_CTR_PRNG_H

