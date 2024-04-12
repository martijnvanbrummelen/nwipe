/*
 * AES CTR PRNG Implementation with Detailed Error Logging
 * Author: Fabian Druschke
 * Date: 2024-04-12
 *
 * This module provides an AES (Advanced Encryption Standard) implementation in CTR (Counter) mode
 * for pseudorandom number generation, enhanced with detailed error handling and logging using OpenSSL.
 * This version logs detailed error messages including function return codes to facilitate debugging
 * and troubleshooting in cryptographic operations.
 *
 * Use of OpenSSL requires compliance with its licensing, and this software manages OpenSSL resources
 * carefully to ensure both security and compliance. Errors are logged with detailed messages to aid
 * in diagnosing issues during development or in production environments.
 *
 * This software is released into the public domain, available for use without restrictions as permitted
 * by law. It is provided "as is", without any warranties, and the author is not liable for any consequences
 * from its use.
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

    nwipe_log( NWIPE_LOG_INFO, "AES CTR PRNG successfully initialized." );  // Log successful initialization
    return;  // Exit successfully

error:
    nwipe_log( NWIPE_LOG_FATAL,
               "Fatal error occured during PRNG init in OpenSSL. Please report this bug and include the logs and "
               "report to https://github.com/martijnvanbrummelen/nwipe/issues" );
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
    nwipe_log( NWIPE_LOG_FATAL,
               "Fatal error occured during RNG generation in OpenSSL. Please report this bug and include the logs and "
               "report to https://github.com/martijnvanbrummelen/nwipe/issues" );
    cleanup();  // Perform cleanup
    exit( 1 );  // Exit with failure status
}
