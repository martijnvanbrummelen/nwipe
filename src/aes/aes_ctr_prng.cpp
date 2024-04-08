#include "aes_ctr_prng.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstring>
#include <cstdlib>
#include <memory>  // Include for std::unique_ptr

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

// Logging function prototype, crucial for diagnostics and operational insights.
void nwipe_log( nwipe_log_t level, const char* format, ... );
}

// Custom deleters for OpenSSL's context structures. These are necessary because the OpenSSL
// API requires specific functions to free these contexts, and std::unique_ptr needs to know
// how to properly dispose of the pointers it manages.
auto EVP_MD_CTX_deleter = []( EVP_MD_CTX* ctx ) {
    if( ctx )
        EVP_MD_CTX_free( ctx );
};
auto EVP_CIPHER_CTX_deleter = []( EVP_CIPHER_CTX* ctx ) {
    if( ctx )
        EVP_CIPHER_CTX_free( ctx );
};

// Definition of smart pointers for OpenSSL's contexts using the custom deleters.
using EVP_MD_CTX_ptr = std::unique_ptr<EVP_MD_CTX, decltype( EVP_MD_CTX_deleter )>;
using EVP_CIPHER_CTX_ptr = std::unique_ptr<EVP_CIPHER_CTX, decltype( EVP_CIPHER_CTX_deleter )>;

// AesCtrState manages the state required by the AES CTR pseudorandom number generator,
// encapsulating all necessary data and operations for cleaner and safer code.
class AesCtrState
{
  public:
    EVP_CIPHER_CTX_ptr ctx;  // Smart pointer for automatic management of EVP_CIPHER_CTX.
    unsigned char ivec[AES_BLOCK_SIZE];  // Initialization vector for CTR mode.
    unsigned int num;  // OpenSSL's internal offset counter for CTR mode.
    unsigned char ecount[AES_BLOCK_SIZE];  // Buffer used by OpenSSL for CTR mode operations.

    // Constructor initializes the encryption context, the IV, and the ecount buffer.
    // It leverages smart pointers for automatic cleanup, enhancing safety.
    AesCtrState()
        : ctx( EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_deleter )
        , num( 0 )
    {
        std::memset( ivec, 0, AES_BLOCK_SIZE );  // Initialize IV with zeros.
        std::memset( ecount, 0, AES_BLOCK_SIZE );  // Initialize ecount with zeros.
        if( !ctx )
        {
            nwipe_log( NWIPE_LOG_FATAL, "Failed to create EVP_CIPHER_CTX for AES CTR PRNG." );
        }
        else
        {
            nwipe_log( NWIPE_LOG_NOTICE, "AES CTR PRNG state initialized." );
        }
    }
    // The destructor is omitted as the smart pointer automatically cleans up.
};

extern "C" {
// Function to create a new AES-CTR PRNG state, facilitating the use of C++ objects from C code.
void* create_aes_ctr_state()
{
    nwipe_log( NWIPE_LOG_DEBUG, "Creating AES CTR PRNG state." );
    return new AesCtrState();  // Use `new` here because we return a void pointer to C.
}

// Corresponding function to delete a previously created AES-CTR PRNG state.
void delete_aes_ctr_state( void* state )
{
    nwipe_log( NWIPE_LOG_DEBUG, "Deleting AES CTR PRNG state." );
    delete static_cast<AesCtrState*>( state );  // Cast back to AesCtrState* and delete.
}

// Initializes the AES-CTR PRNG with a seed, setting up the encryption context for generating random numbers.
void aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long* init_key, unsigned long key_length )
{
    auto* cppState = reinterpret_cast<AesCtrState*>( state );

    // Generate a 256-bit key from the seed using SHA-256.
    unsigned char key[32];
    EVP_MD_CTX_ptr mdctx( EVP_MD_CTX_new(), EVP_MD_CTX_deleter );
    if( !mdctx )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Failed to create EVP_MD_CTX for SHA-256." );
        return;
    }

    if( !EVP_DigestInit_ex( mdctx.get(), EVP_sha256(), nullptr )
        || !EVP_DigestUpdate(
            mdctx.get(), reinterpret_cast<unsigned char*>( init_key ), key_length * sizeof( unsigned long ) )
        || !EVP_DigestFinal_ex( mdctx.get(), key, nullptr ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "SHA-256 digest operation failed." );
        return;
    }

    // Initialize the AES encryption context with the derived key for CTR mode operation.
    if( !EVP_EncryptInit_ex( cppState->ctx.get(), EVP_aes_256_ctr(), nullptr, key, cppState->ivec ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "AES-256 CTR mode initialization failed." );
        return;
    }

    nwipe_log( NWIPE_LOG_INFO, "AES CTR PRNG initialized with provided seed." );
}

// Fills a buffer with pseudorandom data generated using AES-CTR mode. It's designed to be efficient
// and avoids logging on each call to prevent flooding the log with messages for every random number generated.
void aes_ctr_prng_genrand_uint256_to_buf( aes_ctr_state_t* state, unsigned char* bufpos )
{
    auto* cppState = reinterpret_cast<AesCtrState*>( state );
    unsigned char temp_buffer[32];  // Temporary buffer for pseudorandom output.
    int outlen;

    if( !EVP_EncryptUpdate( cppState->ctx.get(), temp_buffer, &outlen, temp_buffer, sizeof( temp_buffer ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "AES-256 CTR PRNG generation failed." );
        return;
    }

    std::memcpy( bufpos, temp_buffer, sizeof( temp_buffer ) );  // Transfer the generated data.
}
}  // extern "C"
