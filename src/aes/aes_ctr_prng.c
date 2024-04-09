#include "aes_ctr_prng.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <string.h>

// Prototype for the logging function, allowing detailed messages to be recorded.

typedef enum nwipe_log_t_ {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,  // Output only when --verbose option used on cmd line.
    NWIPE_LOG_INFO,  // General Info not specifically relevant to the wipe.
    NWIPE_LOG_NOTICE,  // Most logging happens at this level related to wiping.
    NWIPE_LOG_WARNING,  // Things that the user should know about.
    NWIPE_LOG_ERROR,  // Non-fatal errors that result in failure.
    NWIPE_LOG_FATAL,  // Errors that cause the program to exit.
    NWIPE_LOG_SANITY,  // Programming errors.
    NWIPE_LOG_NOTIMESTAMP  // logs the message without the timestamp
} nwipe_log_t;

extern void nwipe_log( nwipe_log_t level, const char* format, ... );

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
    unsigned char key[32];  // Space for a 256-bit key, suitable for AES-256 encryption.

    // Initialize the IV (initialization vector) and counter to zero.
    // This is crucial for CTR mode to start counting from a known state.
    memset( state->ivec, 0, AES_BLOCK_SIZE );
    state->num = 0;
    memset( state->ecount, 0, AES_BLOCK_SIZE );

    // Logging the start of PRNG initialization, important for debugging and audits.
    nwipe_log( NWIPE_LOG_DEBUG, "Initializing AES CTR PRNG with provided seed." );

    // Using EVP for SHA-256 hashing to generate a key from the provided seed.
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if( !mdctx )
    {
        // Critical failure: unable to allocate memory for the digest context.
        nwipe_log( NWIPE_LOG_FATAL, "Failed to allocate EVP_MD_CTX for SHA-256." );
        return;
    }

    if( !EVP_DigestInit_ex( mdctx, EVP_sha256(), NULL ) )
    {
        // Logging the failure to initialize the SHA-256 context.
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 context initialization failed." );
        EVP_MD_CTX_free( mdctx );
        return;
    }

    EVP_DigestUpdate( mdctx, (unsigned char*) init_key, key_length * sizeof( unsigned long ) );

    // Completing the SHA-256 hash computation to produce the final key.
    if( !EVP_DigestFinal_ex( mdctx, key, NULL ) )
    {
        // Logging the failure to finalize the SHA-256 hash, critical for key generation.
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 hash finalization failed." );
        EVP_MD_CTX_free( mdctx );
        return;
    }
    EVP_MD_CTX_free( mdctx );  // Clean up the SHA-256 context after use.

    state->ctx = EVP_CIPHER_CTX_new();
    if( !state->ctx )
    {
        // Critical failure: unable to allocate memory for the cipher context.
        nwipe_log( NWIPE_LOG_FATAL, "Failed to allocate EVP_CIPHER_CTX." );
        return;
    }

    // Initializing the AES-256-CTR encryption context with the derived key and zero IV.
    if( !EVP_EncryptInit_ex( state->ctx, EVP_aes_256_ctr(), NULL, key, state->ivec ) )
    {
        // Logging the failure to initialize the encryption context, crucial for PRNG operation.
        nwipe_log( NWIPE_LOG_FATAL, "AES-256-CTR encryption context initialization failed." );
        return;
    }

    // Logging successful PRNG initialization to provide assurance of readiness.
    nwipe_log( NWIPE_LOG_INFO, "AES CTR PRNG successfully initialized." );
}

/* Generates pseudorandom numbers and writes them to a buffer.
   This function performs the core operation of producing pseudorandom data.
   It directly updates the buffer provided, filling it with pseudorandom bytes
   generated using the AES-256-CTR mode of operation.
   - state: Pointer to the initialized AES CTR PRNG state.
   - bufpos: Target buffer where the pseudorandom numbers will be written. */
void aes_ctr_prng_genrand_uint256_to_buf( aes_ctr_state_t* state, unsigned char* bufpos )
{
    unsigned char temp_buffer[32];  // Intermediate buffer for 256-bit pseudorandom output.
    memset(temp_buffer, 0, sizeof(temp_buffer));
    int outlen;

    // Generating pseudorandom numbers. No logging here to avoid excessive log entries.
    if( !EVP_EncryptUpdate( state->ctx, temp_buffer, &outlen, temp_buffer, sizeof( temp_buffer ) ) )
    {
        // Logging only in case of failure to highlight operational issues.
        nwipe_log( NWIPE_LOG_ERROR, "Failed to generate pseudorandom numbers." );
        return;
    }

    // Safely transferring the generated pseudorandom data into the provided buffer.
    memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );
}
