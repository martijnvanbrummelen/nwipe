#include "aes_ctr_prng.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstring>
#include <cstdlib>

// Extern "C" is used to ensure that the C++ compiler does not change the names of the functions
// inside it (name mangling), making it possible for C code to link against these functions.
extern "C" {

// Enum for logging levels, facilitating detailed control over how much information is logged.
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

// The logging function, which allows the code to log messages at different severity levels.
// This is crucial for diagnosing issues and understanding the program's behavior.
void nwipe_log( nwipe_log_t level, const char* format, ... );
}

// The AesCtrState class manages the state needed by the AES CTR pseudorandom number generator.
// It encapsulates all necessary data and operations, ensuring a clean and manageable code structure.
class AesCtrState
{
  public:
    EVP_CIPHER_CTX* ctx;  // Context for encryption. Essential for performing AES operations.
    unsigned char ivec[AES_BLOCK_SIZE];  // Initialization vector, crucial for CTR mode.
    unsigned int num;  // Used internally by OpenSSL for CTR mode operations.
    unsigned char ecount[AES_BLOCK_SIZE];  // Also used by OpenSSL in CTR mode encryption.

    // Constructor for the AesCtrState class. It initializes the encryption context and zeros out
    // the ivec and ecount arrays, setting up a known initial state for encryption operations.
    AesCtrState()
        : ctx( EVP_CIPHER_CTX_new() )
        , num( 0 )
    {
        if( !ctx )
        {
            // If EVP_CIPHER_CTX_new fails, log a fatal error. This is critical as it means
            // we cannot proceed with encryption or decryption operations.
            nwipe_log( NWIPE_LOG_FATAL, "Failed to create EVP_CIPHER_CTX for AES CTR PRNG." );
        }
        else
        {
            // On successful initialization, log a notice. This helps in confirming that the
            // PRNG state was successfully initialized, an important step in the setup.
            nwipe_log( NWIPE_LOG_NOTICE, "AES CTR PRNG state initialized." );
        }
        std::memset( ivec, 0, AES_BLOCK_SIZE );
        std::memset( ecount, 0, AES_BLOCK_SIZE );
    }

    // Destructor for the AesCtrState class. It ensures that resources, specifically the
    // encryption context, are cleaned up properly when an instance of the class is destroyed.
    ~AesCtrState()
    {
        if( ctx )
        {
            EVP_CIPHER_CTX_free( ctx );  // Proper cleanup of the context to avoid memory leaks.
            // Logging here helps confirm that resources were freed as expected, avoiding potential
            // resource management issues.
            nwipe_log( NWIPE_LOG_NOTICE, "AES CTR PRNG state destroyed." );
        }
    }
};

extern "C" {
// Function to create a new AES-CTR PRNG state. This serves as a bridge between C and C++,
// allowing the C code to create instances of the AesCtrState class.
void* create_aes_ctr_state()
{
    // Logs the creation of a new state. Useful for debugging and ensuring that states
    // are being managed as expected.
    nwipe_log( NWIPE_LOG_DEBUG, "Creating AES CTR PRNG state." );
    return static_cast<void*>( new AesCtrState() );
}

// Function to delete an existing AES-CTR PRNG state. Like create_aes_ctr_state,
// it allows the C code to interact with C++ objects.
void delete_aes_ctr_state( void* state )
{
    // Logs the deletion of a state. Critical for ensuring that resources are being
    // released appropriately.
    nwipe_log( NWIPE_LOG_DEBUG, "Deleting AES CTR PRNG state." );
    delete static_cast<AesCtrState*>( state );
}

// Initializes the AES-CTR PRNG with a given seed. This function bridges C++ object management
// with the C interface used by the rest of the application, allowing the initialization
// of the PRNG with specific seed data.
void aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long* init_key, unsigned long key_length )
{
    AesCtrState* cppState = reinterpret_cast<AesCtrState*>( state );

    unsigned char key[32];  // Buffer to hold the 256-bit key derived from the seed.
    // Create a new digest context for generating the key via SHA-256 hashing.
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if( !mdctx )
    {
        // Failure to create the digest context is a critical error and is logged as such.
        nwipe_log( NWIPE_LOG_FATAL, "Failed to create EVP_MD_CTX for SHA-256." );
        return;
    }

    // Initialize, update, and finalize the digest context to produce the key. Each step
    // is checked for success, with failures logged appropriately. This thorough checking
    // ensures that any issues in key generation are quickly identified and reported.
    if( !EVP_DigestInit_ex( mdctx, EVP_sha256(), NULL ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 initialization failed." );
        EVP_MD_CTX_free( mdctx );
        return;
    }

    if( !EVP_DigestUpdate( mdctx, reinterpret_cast<unsigned char*>( init_key ), key_length * sizeof( unsigned long ) ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 update with seed failed." );
        EVP_MD_CTX_free( mdctx );
        return;
    }

    if( !EVP_DigestFinal_ex( mdctx, key, NULL ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 finalization failed." );
        EVP_MD_CTX_free( mdctx );
        return;
    }

    EVP_MD_CTX_free( mdctx );  // Clean up the digest context after use.

    // Initialize the AES-CTR encryption context with the derived key. If this step fails,
    // it is logged as a fatal error because without successful initialization, the PRNG
    // cannot function.
    if( !EVP_EncryptInit_ex( cppState->ctx, EVP_aes_256_ctr(), NULL, key, cppState->ivec ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "AES-256 CTR mode initialization failed." );
        return;
    }

    // Log the successful initialization of the PRNG with the provided seed. This message
    // confirms to the user or developer that the PRNG is ready for use.
    nwipe_log( NWIPE_LOG_INFO, "AES CTR PRNG initialized with provided seed." );
}

// Generates pseudorandom numbers and writes them to a buffer. The function directly manipulates
// the provided buffer, filling it with pseudorandom data generated by the AES-CTR algorithm.
// Detailed error checking and logging ensure that any issues are promptly reported.
void aes_ctr_prng_genrand_uint128_to_buf( aes_ctr_state_t* state, unsigned char* bufpos )
{
    AesCtrState* cppState = reinterpret_cast<AesCtrState*>( state );
    unsigned char temp_buffer[16];  // Temporary buffer for the pseudorandom output.
    int outlen;

    // Attempt to generate the pseudorandom data. If this fails, an error is logged.
    // Given the nature of this operation (potentially called very frequently), logging
    // on success is omitted to avoid overwhelming the log files.
    if( !EVP_EncryptUpdate( cppState->ctx, temp_buffer, &outlen, temp_buffer, sizeof( temp_buffer ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "AES-256 CTR PRNG generation failed." );
        return;
    }

    std::memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );  // Copy the generated data to the output buffer.
}
}  // extern "C"
