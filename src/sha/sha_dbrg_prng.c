/*
 * SHA-512 HMAC DBRG (Deterministic Random Bit Generator) Implementation
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

#include "sha_dbrg_prng.h"
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

void sha_dbrg_prng_init( sha_dbrg_state_t* state, unsigned long init_key[], unsigned long key_length )
{
    EVP_MD_CTX* mdctx;
    const EVP_MD* md = EVP_sha512();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex( mdctx, md, NULL );

    // Process the initial key
    EVP_DigestUpdate( mdctx, init_key, key_length * sizeof( unsigned long ) );
    // Finalize the digest, forming the initial seed state
    EVP_DigestFinal_ex( mdctx, state->seed, NULL );

    EVP_MD_CTX_free( mdctx );
}

static void next_state( sha_dbrg_state_t* state )
{
    EVP_MD_CTX* mdctx;
    const EVP_MD* md = EVP_sha512();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex( mdctx, md, NULL );

    // Update the state by re-hashing
    EVP_DigestUpdate( mdctx, state->seed, sizeof( state->seed ) );
    EVP_DigestFinal_ex( mdctx, state->seed, NULL );

    EVP_MD_CTX_free( mdctx );
}

void sha_dbrg_prng_genrand_uint512_to_buf( sha_dbrg_state_t* state, unsigned char* bufpos )
{
    // Generate random data based on the current state
    EVP_MD_CTX* mdctx;
    const EVP_MD* md = EVP_sha512();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex( mdctx, md, NULL );
    EVP_DigestUpdate( mdctx, state->seed, sizeof( state->seed ) );
    EVP_DigestFinal_ex( mdctx, bufpos, NULL );

    EVP_MD_CTX_free( mdctx );

    // Update the state for the next generation
    next_state( state );
}
