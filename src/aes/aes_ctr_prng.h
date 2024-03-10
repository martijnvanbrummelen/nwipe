#ifndef AES_CTR_RNG_H
#define AES_CTR_RNG_H

#include <stdint.h>
#include <openssl/aes.h>

// Structure to store the state of the AES-CTR random number generator
typedef struct
{
    AES_KEY aes_key;
    unsigned char ivec[AES_BLOCK_SIZE];
    unsigned int num;
    unsigned char ecount[AES_BLOCK_SIZE];
} aes_ctr_state_t;

// Initializes the AES-CTR random number generator
void init_aes_ctr( aes_ctr_state_t* state, const unsigned char* key );

// Generates a 32-bit integer using AES-CTR
unsigned int aes_ctr_generate_uint32( aes_ctr_state_t* state );

#endif  // AES_CTR_RNG_H
