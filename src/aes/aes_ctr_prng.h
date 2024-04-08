#ifndef AES_CTR_PRNG_H
#define AES_CTR_PRNG_H

#include <stdint.h>
#include <openssl/aes.h>
#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of the C++ class for C compatibility
typedef struct AesCtrState aes_ctr_state_t;

// Function to create an instance of the AES-CTR state (C++ object)
void* create_aes_ctr_state();

// Function to delete an instance of the AES-CTR state (C++ object)
void delete_aes_ctr_state( void* state );

// Initializes the AES-CTR pseudorandom number generator
void aes_ctr_prng_init( aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length );

// Generates a 256-bit random number using AES-CTR and stores it in the output buffer
void aes_ctr_prng_genrand_uint256_to_buf( aes_ctr_state_t* state, unsigned char* bufpos );

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AES_CTR_PRNG_H
