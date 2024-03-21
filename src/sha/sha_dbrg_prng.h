/*
 * SHA-512 HMAC DBRG (Deterministic Random Bit Generator) Definitions
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This header file contains definitions and functionality for implementing a
 * Deterministic Random Bit Generator using the SHA-512 HMAC (Hash-Based Message Authentication Code) algorithm,
 * leveraging OpenSSL for cryptographic operations.
 *
 * As the author of this work, I, Fabian Druschke, hereby release this work into the public
 * domain. I dedicate any and all copyright interest in this work to the public domain,
 * making it freely available for use by anyone for any purpose, without any conditions,
 * unless such conditions are required by law.
 *
 * This software is provided "as is", without warranty of any kind, express or implied,
 * including but not limited to the warranties of merchantability, fitness for a particular
 * purpose, and noninfringement. In no event shall the authors be liable for any claim,
 * damages, or other liability, whether in an action of contract, tort or otherwise,
 * arising from, out of, or in connection with the software or the use or other dealings in the software.
 *
 * USAGE OF OPENSSL IN THIS SOFTWARE:
 * This software utilizes OpenSSL for its cryptographic functions. Users of this software
 * are responsible for ensuring they are in compliance with OpenSSL's licensing terms.
 */

#ifndef SHA_DRBG_PRNG_H
#define SHA_DRBG_PRNG_H

#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

// State definition for the SHA-512-based PRNG
typedef struct
{
    unsigned char seed[64];  // SHA-512 output size for the state
} sha_dbrg_state_t;

// Function prototypes
void sha_dbrg_prng_init( sha_dbrg_state_t* state, unsigned long init_key[], unsigned long key_length );
void sha_dbrg_prng_genrand_uint512_to_buf( sha_dbrg_state_t* state, unsigned char* bufpos );

#endif  // SHA_DRBG_PRNG_H
