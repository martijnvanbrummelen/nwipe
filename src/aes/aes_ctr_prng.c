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
extern int nwipe_check_entropy( uint64_t num );

/* Initializes the AES CTR pseudorandom number generator state.
   This function sets up the cryptographic context necessary for generating
   pseudorandom numbers using AES in CTR mode. It utilizes SHA-256 to derive
   a key from the provided seed, ensuring that the PRNG output is unpredictable
   and secure, provided the seed is kept secret and is sufficiently random.
   - state: Pointer to the AES CTR PRNG state structure.
   - init_key: Array containing the seed for key generation.
   - key_length: Length of the seed array. */
int aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length )
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
        return -1;  // Handle error
    }

    if( EVP_DigestInit_ex( mdctx, EVP_sha256(), NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "SHA-256 context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log init failure
        return -1;  // Handle error
    }

    EVP_DigestUpdate(
        mdctx, (const unsigned char*) init_key, key_length * sizeof( unsigned long ) );  // Process the seed

    if( EVP_DigestFinal_ex( mdctx, key, NULL ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "SHA-256 hash finalization failed, return code: %d.",
                   ERR_get_error() );  // Log finalization failure
        return -1;  // Handle error
    }
    EVP_MD_CTX_free( mdctx );
    mdctx = NULL;  // Clean up SHA-256 context

    state->ctx = EVP_CIPHER_CTX_new();  // Create new AES-256-CTR context
    if( !state->ctx )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "Failed to allocate EVP_CIPHER_CTX, return code: %d.",
                   ERR_get_error() );  // Log cipher context failure
        return -1;  // Handle error
    }

    if( EVP_EncryptInit_ex( state->ctx, EVP_aes_256_ctr(), NULL, key, state->ivec ) != 1 )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "AES-256-CTR encryption context initialization failed, return code: %d.",
                   ERR_get_error() );  // Log encryption init failure
        return -1;  // Handle error
    }

    nwipe_log( NWIPE_LOG_DEBUG, "AES CTR PRNG successfully initialized." );  // Log successful initialization
    /*
     * Generate a 256-bit random number and store it in a buffer.
     * This is done by defining a buffer that can hold 32 bytes (256 bits).
     * The buffer is first initialized to zero using memset to ensure that no residual data
     * from previous operations is present, which could compromise security.
     * Initializing the buffer is a good practice in cryptographic operations
     * to avoid any accidental data leakage or corruption.
     */
    unsigned char random_output[32];  // Buffer for 256-bit random number
    memset( random_output, 0, sizeof( random_output ) );  // Initialize the buffer to zero

    /*
     * Call the function aes_ctr_prng_genrand_uint256_to_buf to generate a secure 256-bit random number.
     * This function uses the AES-CTR PRNG state, which has been initialized earlier, to fill the buffer
     * with pseudorandom data. AES-CTR mode is chosen for its efficiency and strong cryptographic properties,
     * making it suitable for generating secure random numbers. If this function fails (returns non-zero),
     * it indicates an issue with the random number generation process, which is critical for the security
     * of the application.
     */
    if( aes_ctr_prng_genrand_uint256_to_buf( state, random_output ) != 0 )
    {
        /*
         * Log a fatal error message indicating that the random number generation has failed.
         * This is a critical error as it affects the integrity of the cryptographic operations.
         * The function then returns -1 to signal the failure, and the PRNG initialization
         * process is aborted to prevent the use of an uninitialized or faulty PRNG.
         */
        nwipe_log( NWIPE_LOG_FATAL, "Failed to generate 256-bit random number." );
        return -1;  // Handle error and exit the function
    }

    /*
     * Extract the first 64 bits (8 bytes) of the generated 256-bit random number.
     * Using the memcpy function, the first 8 bytes of data from the random_output buffer
     * are copied into the variable random_64bit. This 64-bit portion will be used for
     * entropy verification, which is a process to ensure that the generated random number
     * has enough randomness to be considered secure. Proper entropy is essential for
     * cryptographic applications to avoid predictability and ensure robust security.
     */
    uint64_t random_64bit;
    memcpy( &random_64bit, random_output, sizeof( uint64_t ) );

    /*
     * Verify the entropy of the extracted 64-bit number.
     * The nwipe_verify_entropy function is called with random_64bit as an argument.
     * This function checks if the 64-bit number meets the entropy requirements.
     * Sufficient entropy ensures that the generated random numbers are not predictable,
     * which is crucial for cryptographic security. If the function returns 1, it means
     * the entropy is sufficient, and the PRNG initialization is considered successful.
     * If the function returns 0, it indicates that the entropy check failed, meaning
     * the generated number might not be sufficiently random. The function logs an appropriate
     * message based on the result and returns 0 for success or -1 for failure.
     */
    if( nwipe_check_entropy( random_64bit ) == 1 )
    {
        /*
         * Log a debug message indicating that the PRNG has been successfully initialized
         * with sufficient entropy. This positive outcome ensures that the generated random
         * numbers will provide the necessary unpredictability for cryptographic operations.
         * The function returns 0 to signal successful initialization.
         */
        nwipe_log( NWIPE_LOG_INFO, "AES CTR PRNG successfully initialized with sufficient entropy." );
        return 0;  // Successful initialization
    }
    else
    {
        /*
         * Log an error message indicating that the entropy check has failed for the
         * generated random number. A failure in the entropy check suggests that the
         * generated number may not be random enough for secure cryptographic operations.
         * Without adequate entropy, the security of the cryptographic system could be compromised.
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
   generated using the AES-256-CTR mode of operation.
   - state: Pointer to the initialized AES CTR PRNG state.
   - bufpos: Target buffer where the pseudorandom numbers will be written. */
int aes_ctr_prng_genrand_uint256_to_buf( aes_ctr_state_t* state, unsigned char* bufpos )
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
        return -1;  // Handle error
    }

    memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );  // Copy pseudorandom bytes to buffer
    return 0;  // Exit successfully
}
// General cleanup function for AES CTR PRNG
int aes_ctr_prng_general_cleanup( aes_ctr_state_t* state )
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
