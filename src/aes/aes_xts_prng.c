/*
 * AES XTS PRNG Implementation with EVP_MD_BLAKE2 for Key Derivation
 * Author: Fabian Druschke
 * Date: 2024-03-13
 *
 * This header file contains definitions for the AES (Advanced Encryption Standard)
 * implementation in XTS (XEX-based tweaked-codebook mode with ciphertext stealing) mode
 * for pseudorandom number generation, utilizing OpenSSL for cryptographic functions and BLAKE2
 * for generating a 512-bit seed.
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

#include "aes_xts_prng.h"
#include <openssl/evp.h>
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
extern int nwipe_check_entropy( uint64_t num );

/* Initializes the AES XTS pseudorandom number generator state using BLAKE2b for key derivation.
   This function sets up the cryptographic context necessary for generating
   pseudorandom numbers using AES in XTS mode. It utilizes BLAKE2b to derive
   a 512-bit key from the provided seed, ensuring that the PRNG output is unpredictable
   and secure, provided the seed is kept secret and is sufficiently random.
   - state: Pointer to the AES XTS PRNG state structure.
   - init_key: Array containing the seed for key generation.
   - key_length: Length of the seed array. */
int aes_xts_prng_init( aes_xts_state_t* state, unsigned long init_key[], unsigned long key_length )
{
    assert( state != NULL && init_key != NULL && key_length > 0 );  // Validate inputs

    unsigned char key[64];  // Storage for the 512-bit key (2 x 256-bit)
    memset( state->ivec, 0, AES_BLOCK_SIZE );  // Clear IV buffer
    state->num = 0;  // Reset the block counter
    memset( state->ecount, 0, AES_BLOCK_SIZE );  // Clear encryption count buffer

    nwipe_log( NWIPE_LOG_DEBUG,
               "Initializing AES XTS PRNG with provided seed using BLAKE2b via EVP_MD." );  // Log initialization

    // Use EVP_MD for BLAKE2b hash computation
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();  // Create new EVP_MD context for BLAKE2b
    if( !mdctx )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "Failed to allocate EVP_MD_CTX for BLAKE2b, return code: %d.",
                   ERR_get_error() );  // Log context allocation failure
        return -1;  // Handle error
    }

    if( EVP_DigestInit_ex( mdctx, EVP_blake2b512(), NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "BLAKE2b context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log init failure
        EVP_MD_CTX_free( mdctx );  // Clean up context
        return -1;  // Handle error
    }

    if( EVP_DigestUpdate( mdctx, (const unsigned char*) init_key, key_length * sizeof( unsigned long ) ) != 1 )
    {
        nwipe_log(
            NWIPE_LOG_FATAL, "BLAKE2b hash update failed, return code: %d.", ERR_get_error() );  // Log update failure
        EVP_MD_CTX_free( mdctx );  // Clean up context
        return -1;  // Handle error
    }

    if( EVP_DigestFinal_ex( mdctx, key, NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "BLAKE2b hash finalization failed, return code: %d.",
                   ERR_get_error() );  // Log finalization failure
        EVP_MD_CTX_free( mdctx );  // Clean up context
        return -1;  // Handle error
    }

    EVP_MD_CTX_free( mdctx );  // Clean up context after successful hashing

    state->ctx = EVP_CIPHER_CTX_new();  // Create new AES-XTS-256 context
    if( !state->ctx )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "Failed to allocate EVP_CIPHER_CTX, return code: %d.",
                   ERR_get_error() );  // Log cipher context failure
        return -1;  // Handle error
    }

    if( EVP_EncryptInit_ex( state->ctx, EVP_aes_256_xts(), NULL, key, state->ivec ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "AES-XTS-256 encryption context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log encryption init failure
        EVP_CIPHER_CTX_free( state->ctx );  // Clean up context on failure
        return -1;  // Handle error
    }

    nwipe_log( NWIPE_LOG_DEBUG,
               "AES XTS PRNG successfully initialized using BLAKE2." );  // Log successful initialization
    /*
     * Generate a 512-bit random number and store it in a buffer.
     * This is done by defining a buffer that can hold 64 bytes (512 bits).
     * The buffer is first initialized to zero using memset to ensure that no residual data
     * from previous operations is present, which could compromise security.
     * Initializing the buffer is a good practice in cryptographic operations
     * to avoid any accidental data leakage or corruption.
     */
    unsigned char random_output[64];  // Buffer for 512-bit random number
    memset( random_output, 0, sizeof( random_output ) );  // Initialize the buffer to zero

    /*
     * Call the function aes_xts_prng_genrand_uint512_to_buf to generate a secure 512-bit random number.
     * This function uses the AES XTS PRNG state, which has been initialized earlier, to fill the buffer
     * with pseudorandom data. If this function fails (returns non-zero), it indicates an issue with
     * the random number generation process, which is critical for the security of the application.
     */
    if( aes_xts_prng_genrand_uint512_to_buf( state, random_output ) != 0 )
    {
        /*
         * Log a fatal error message indicating that the random number generation has failed.
         * This is a critical error as it affects the integrity of the cryptographic operations.
         * The function then returns -1 to signal the failure, and the PRNG initialization
         * process is aborted to prevent the use of an uninitialized or faulty PRNG.
         */
        nwipe_log( NWIPE_LOG_FATAL, "Failed to generate 512-bit random number." );
        return -1;  // Handle error and exit the function
    }

    /*
     * Extract the first 64 bits (8 bytes) of the generated 512-bit random number.
     * Using the memcpy function, the first 8 bytes of data from the random_output buffer
     * are copied into the variable random_64bit. This 64-bit portion will be used for
     * entropy verification, which is a process to ensure that the generated random number
     * has enough randomness to be considered secure. Proper entropy is essential for
     * cryptographic applications to avoid predictability.
     */
    uint64_t random_64bit;
    memcpy( &random_64bit, random_output, sizeof( uint64_t ) );

    /*
     * Verify the entropy of the extracted 64-bit number.
     * The nwipe_verify_entropy function is called with random_64bit as an argument.
     * This function checks if the 64-bit number meets the entropy requirements.
     * If the function returns 1, it means the entropy is sufficient, and the PRNG
     * initialization is considered successful. If the function returns 0, it indicates
     * that the entropy check failed, meaning the generated number might not be sufficiently random.
     * The function logs an appropriate message based on the result and returns 0 for success
     * or -1 for failure.
     */
    if( nwipe_check_entropy( random_64bit ) == 1 )
    {
        /*
         * Log a debug message indicating that the PRNG has been successfully initialized
         * with sufficient entropy. This is a positive outcome, and the function
         * returns 0 to signal successful initialization.
         */
        nwipe_log( NWIPE_LOG_INFO, "AES XTS PRNG successfully initialized with sufficient entropy." );
        return 0;  // Successful initialization
    }
    else
    {
        /*
         * Log an error message indicating that the entropy check has failed for the
         * generated random number. A failure in the entropy check suggests that the
         * generated number may not be random enough for secure cryptographic operations.
         * The function returns -1 to indicate this failure, ensuring that the faulty
         * PRNG is not used.
         */
        nwipe_log( NWIPE_LOG_FATAL, "Entropy check for generated random number failed." );
        return -1;  // Entropy check failed
    }

    return 0;  // Exit successfully
}

/* Generates pseudorandom numbers and writes them to a buffer.
   This function performs the core operation of producing pseudorandom data.
   It directly updates the buffer provided, filling it with pseudorandom bytes
   generated using the AES-XTS-256 mode of operation.
   - state: Pointer to the initialized AES XTS PRNG state.
   - bufpos: Target buffer where the pseudorandom numbers will be written. */
int aes_xts_prng_genrand_uint512_to_buf( aes_xts_state_t* state, unsigned char* bufpos )
{
    assert( state != NULL && bufpos != NULL );  // Validate inputs

    unsigned char temp_buffer[64];  // Temporary storage for pseudorandom bytes (512-bit)
    memset( temp_buffer, 0, sizeof( temp_buffer ) );  // Zero out temporary buffer
    int outlen;  // Length of data produced by encryption

    if( EVP_EncryptUpdate( state->ctx, temp_buffer, &outlen, temp_buffer, sizeof( temp_buffer ) ) != 1 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to generate pseudorandom numbers, return code: %d.",
                   ERR_get_error() );  // Log generation failure
        return -1;  // Handle error
    }

    memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );  // Copy pseudorandom bytes to buffer
    return 0;  // Exit successfully
}

// General cleanup function for AES XTS PRNG
int aes_xts_prng_general_cleanup( aes_xts_state_t* state )
{
    if( state != NULL )
    {
        // Free the EVP_CIPHER_CTX if it has been allocated
        if( state->ctx )
        {
            EVP_CIPHER_CTX_free( state->ctx );
            state->ctx = NULL;  // Nullify the pointer after free
        }

        // Clear sensitive information from the state
        memset( state->ivec, 0, AES_BLOCK_SIZE );
        memset( state->ecount, 0, AES_BLOCK_SIZE );
        state->num = 0;
    }
    return 0;
}
