/*
 * AES CTR PRNG Implementation
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

#include "aes_ctr_prng.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef enum {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,  // Debugging messages, detailed for troubleshooting
    NWIPE_LOG_INFO,  // Informative logs, for regular operation insights
    NWIPE_LOG_NOTICE,  // Notices for significant but non-critical events
    NWIPE_LOG_WARNING,  // Warnings about potential errors
    NWIPE_LOG_ERROR,  // Error messages, significant issues that affect operation
    NWIPE_LOG_FATAL,  // Fatal errors, require immediate termination of the program
    NWIPE_LOG_SANITY,  // Sanity checks, used primarily in debugging phases
    NWIPE_LOG_NOTIMESTAMP  // Log entries without timestamp information
} nwipe_log_t;

extern void nwipe_log( nwipe_log_t level, const char* format, ... );
extern void cleanup( void );

/* Initializes the AES CTR pseudorandom number generator state.
   This function sets up the cryptographic context necessary for generating
   pseudorandom numbers using AES in CTR mode. It utilizes SHA-256 to derive
   a key from the provided seed, ensuring that the PRNG output is unpredictable
   and secure, provided the seed is kept secret and is sufficiently random.
   - state: Pointer to the AES CTR PRNG state structure.
   - init_key: Array containing the seed for key generation.
   - key_length: Length of the seed array. */
void aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length )
{
    assert( state != NULL && init_key != NULL && key_length > 0 );  // Validate inputs

    unsigned char key[32];  // Storage for the 256-bit key
    memset( state->ivec, 0, AES_BLOCK_SIZE );  // Clear IV buffer
    state->num = 0;  // Reset the block counter
    memset( state->ecount, 0, AES_BLOCK_SIZE );  // Clear encryption count buffer

    nwipe_log( NWIPE_LOG_DEBUG, "Initializing AES CTR PRNG with provided seed." );  // Log initialization

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();  // Create new SHA-256 context
    if( !mdctx )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "Failed to allocate EVP_MD_CTX for SHA-256, return code: %d.",
                   ERR_get_error() );  // Log context allocation failure
        goto error;  // Handle error
    }

    if( EVP_DigestInit_ex( mdctx, EVP_sha256(), NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "SHA-256 context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log init failure
        goto error;  // Handle error
    }

    EVP_DigestUpdate(
        mdctx, (const unsigned char*) init_key, key_length * sizeof( unsigned long ) );  // Process the seed

    if( EVP_DigestFinal_ex( mdctx, key, NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "SHA-256 hash finalization failed, return code: %d.",
                   ERR_get_error() );  // Log finalization failure
        goto error;  // Handle error
    }
    EVP_MD_CTX_free( mdctx );
    mdctx = NULL;  // Clean up SHA-256 context

    state->ctx = EVP_CIPHER_CTX_new();  // Create new AES-256-CTR context
    if( !state->ctx )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "Failed to allocate EVP_CIPHER_CTX, return code: %d.",
                   ERR_get_error() );  // Log cipher context failure
        goto error;  // Handle error
    }

    if( EVP_EncryptInit_ex( state->ctx, EVP_aes_256_ctr(), NULL, key, state->ivec ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "AES-256-CTR encryption context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log encryption init failure
        goto error;  // Handle error
    }

    nwipe_log( NWIPE_LOG_DEBUG, "AES CTR PRNG successfully initialized." );  // Log successful initialization
    return;  // Exit successfully

error:
    nwipe_log( NWIPE_LOG_SANITY,"Fatal error occured during PRNG init in OpenSSL." );
    if( mdctx )
        EVP_MD_CTX_free( mdctx );  // Ensure clean up if initialized
    if( state->ctx )
        EVP_CIPHER_CTX_free( state->ctx );  // Clean up if cipher context was created
    cleanup();  // Perform additional cleanup
    exit( 1 );  // Exit with failure status
}

/* Generates pseudorandom numbers and writes them to a buffer.
   This function performs the core operation of producing pseudorandom data.
   It directly updates the buffer provided, filling it with pseudorandom bytes
   generated using the AES-256-CTR mode of operation.
   - state: Pointer to the initialized AES CTR PRNG state.
   - bufpos: Target buffer where the pseudorandom numbers will be written. */
void aes_ctr_prng_genrand_uint256_to_buf( aes_ctr_state_t* state, unsigned char* bufpos )
{
    assert( state != NULL && bufpos != NULL );  // Validate inputs

    unsigned char temp_buffer[32];  // Temporary storage for pseudorandom bytes
    memset( temp_buffer, 0, sizeof( temp_buffer ) );  // Zero out temporary buffer
    int outlen;  // Length of data produced by encryption

    if( EVP_EncryptUpdate( state->ctx, temp_buffer, &outlen, temp_buffer, sizeof( temp_buffer ) ) != 1 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to generate pseudorandom numbers, return code: %d.",
                   ERR_get_error() );  // Log generation failure
        goto error;  // Handle error
    }

    memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );  // Copy pseudorandom bytes to buffer
    return;  // Exit successfully

error:
    nwipe_log( NWIPE_LOG_FATAL,"Fatal error occured during RNG generation in OpenSSL." );
    cleanup();  // Perform cleanup
    exit( 1 );  // Exit with failure status
}

// General cleanup function for AES CTR PRNG
void aes_ctr_prng_general_cleanup(aes_ctr_state_t* state) {
    if (state != NULL) {
        // Free the EVP_CIPHER_CTX if it has been allocated
        if (state->ctx) {
            EVP_CIPHER_CTX_free(state->ctx);
            state->ctx = NULL;  // Nullify the pointer after free
        }

        // Clear sensitive information from the state
        memset(state->ivec, 0, AES_BLOCK_SIZE);
        memset(state->ecount, 0, AES_BLOCK_SIZE);
        state->num = 0;
    }
}
