/*
 * ChaCha20 PRNG Definitions
 * Author: Fabian Druschke (adapted for ChaCha20)
 * Date: 2025-03-08
 *
 * This header file contains definitions for the ChaCha20 implementation
 * for pseudorandom number generation using OpenSSL.
 *
 * This code is released into the public domain.
 */

#ifndef CHACHA20_PRNG_H
#define CHACHA20_PRNG_H

#include <stdint.h>
#include <openssl/evp.h>

// ChaCha20 uses a 12-byte nonce
#define CHACHA20_NONCE_SIZE 12

// Structure to hold the state of the ChaCha20 PRNG
typedef struct {
    EVP_CIPHER_CTX* ctx;
    unsigned char nonce[CHACHA20_NONCE_SIZE];
} chacha20_state_t;

/**
 * Initializes the ChaCha20 PRNG.
 *
 * @param state Pointer to the ChaCha20 PRNG state structure.
 * @param init_key Array containing the seed for key derivation.
 * @param key_length Length of the seed array.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_init(chacha20_state_t* state, unsigned long init_key[], unsigned long key_length);

/**
 * Generates a 512-bit (64-byte) random block using ChaCha20 and writes it directly
 * to the target buffer.
 *
 * @param state Pointer to the initialized ChaCha20 PRNG state.
 * @param bufpos Target buffer where the pseudorandom data will be written.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_genrand_uint512_to_buf(chacha20_state_t* state, unsigned char* bufpos);

/**
 * Validates the pseudorandom number generation by generating test data and performing
 * statistical tests (bit frequency, byte frequency, entropy, and repetition pattern tests).
 *
 * @param state Pointer to the initialized ChaCha20 PRNG state.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_validate(chacha20_state_t* state);

/**
 * General cleanup function for the ChaCha20 PRNG.
 *
 * @param state Pointer to the ChaCha20 PRNG state structure.
 * @return int Returns 0 on success.
 */
int chacha20_prng_general_cleanup(chacha20_state_t* state);

#endif  // CHACHA20_PRNG_H

