/*
 * AES XTS PRNG Definitions
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This header file contains definitions for the AES (Advanced Encryption Standard)
 * implementation in XTS (XEX-based Tweaked-codebook mode with ciphertext Stealing) mode 
 * for pseudorandom number generation, utilizing OpenSSL for cryptographic functions.
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

#ifndef AES_XTS_RNG_H
#define AES_XTS_RNG_H

#include <stdint.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

// Structure to store the state of the AES-XTS random number generator
typedef struct
{
    EVP_CIPHER_CTX* ctx;              // Encryption context for AES-XTS
    unsigned char ivec[AES_BLOCK_SIZE];  // Initialization vector
    unsigned int num;                 // Counter for the number of blocks processed
    unsigned char ecount[AES_BLOCK_SIZE]; // Encryption count buffer for feedback
} aes_xts_state_t;

// Initializes the AES-XTS random number generator
int aes_xts_prng_init(aes_xts_state_t* state, unsigned long init_key[], unsigned long key_length);

// Generates a 512-bit random number using AES-XTS and stores it directly in the output buffer
int aes_xts_prng_genrand_uint512_to_buf(aes_xts_state_t* state, unsigned char* bufpos);

// General cleanup function for AES XTS PRNG
int aes_xts_prng_general_cleanup(aes_xts_state_t* state);

#endif  // AES_XTS_RNG_H

