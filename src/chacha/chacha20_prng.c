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

#include "chacha20_prng.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

// Custom assert macro (if not already defined)
#ifndef NWIPE_ASSERT_H
#define NWIPE_ASSERT_H
#ifdef NDEBUG
    #define NWIPE_ASSERT(cond, level, fmt, ...) ((void)0)
#else
    #define NWIPE_ASSERT(cond, level, fmt, ...)                              \
        do {                                                                 \
            if (!(cond)) {                                                   \
                nwipe_log(level, "Assertion failed: " fmt, ##__VA_ARGS__);     \
                abort();                                                     \
            }                                                                \
        } while (0)
#endif
#endif

// Example log level definitions (adjust as needed for your logging system)
typedef enum {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,
    NWIPE_LOG_INFO,
    NWIPE_LOG_NOTICE,
    NWIPE_LOG_WARNING,
    NWIPE_LOG_ERROR,
    NWIPE_LOG_FATAL,
    NWIPE_LOG_SANITY,
    NWIPE_LOG_NOTIMESTAMP
} nwipe_log_t;

// Prototype declaration for the logging function (externally implemented)
extern void nwipe_log(nwipe_log_t level, const char* format, ...);

/**
 * Helper function to calculate the Shannon entropy based on byte frequencies.
 *
 * @param byte_counts Array with counts for each byte value (0-255).
 * @param data_length Total length of the data.
 * @return double Returns the calculated entropy in bits per byte.
 */
static double calculate_shannon_entropy(const unsigned int* byte_counts, size_t data_length)
{
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (byte_counts[i] > 0) {
            double probability = (double)byte_counts[i] / data_length;
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}

/**
 * Initializes the ChaCha20 PRNG state.
 * The key is derived using SHA-256 from the provided seed,
 * then the EVP_CIPHER_CTX is initialized with EVP_chacha20().
 *
 * @param state Pointer to the ChaCha20 PRNG state.
 * @param init_key Array containing the seed for key derivation.
 * @param key_length Length of the seed array.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_init(chacha20_state_t* state, unsigned long init_key[], unsigned long key_length)
{
    NWIPE_ASSERT(state != NULL && init_key != NULL && key_length > 0,
                 NWIPE_LOG_FATAL,
                 "Invalid parameters: state=%p, init_key=%p, key_length=%lu",
                 (void*)state, (void*)init_key, key_length);

    unsigned char key[32];  // 256-bit key
    memset(state->nonce, 0, CHACHA20_NONCE_SIZE);  // Set nonce to zero

    nwipe_log(NWIPE_LOG_DEBUG, "Initializing ChaCha20 PRNG with provided seed.");

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        nwipe_log(NWIPE_LOG_FATAL,
                  "Failed to allocate EVP_MD_CTX for SHA-256, error code: %lu.",
                  ERR_get_error());
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) {
        nwipe_log(NWIPE_LOG_FATAL,
                  "SHA-256 context initialization failed, error code: %lu.",
                  ERR_get_error());
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    EVP_DigestUpdate(mdctx, (const unsigned char*)init_key, key_length * sizeof(unsigned long));

    if (EVP_DigestFinal_ex(mdctx, key, NULL) != 1) {
        nwipe_log(NWIPE_LOG_FATAL,
                  "SHA-256 hash finalization failed, error code: %lu.",
                  ERR_get_error());
        EVP_MD_CTX_free(mdctx);
        return -1;
    }
    EVP_MD_CTX_free(mdctx);

    state->ctx = EVP_CIPHER_CTX_new();
    if (!state->ctx) {
        nwipe_log(NWIPE_LOG_FATAL,
                  "Failed to allocate EVP_CIPHER_CTX, error code: %lu.",
                  ERR_get_error());
        return -1;
    }

    if (EVP_EncryptInit_ex(state->ctx, EVP_chacha20(), NULL, key, state->nonce) != 1) {
        nwipe_log(NWIPE_LOG_FATAL,
                  "ChaCha20 encryption context initialization failed, error code: %lu.",
                  ERR_get_error());
        EVP_CIPHER_CTX_free(state->ctx);
        return -1;
    }

    // Validate the PRNG output
    if (chacha20_prng_validate(state) != 0) {
        nwipe_log(NWIPE_LOG_FATAL, "ChaCha20 PRNG validation failed.");
        EVP_CIPHER_CTX_free(state->ctx);
        return -1;
    }

    nwipe_log(NWIPE_LOG_DEBUG, "ChaCha20 PRNG successfully initialized and validated.");
    return 0;
}

/**
 * Validates the pseudorandom number generation by generating 4KB of test data
 * and performing statistical tests (bit frequency, byte frequency, entropy, and repetition tests).
 * Data is generated in 64-byte chunks.
 *
 * @param state Pointer to the initialized ChaCha20 PRNG state.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_validate(chacha20_state_t* state)
{
    NWIPE_ASSERT(state != NULL,
                 NWIPE_LOG_FATAL,
                 "Invalid parameter: state=%p",
                 (void*)state);

    const size_t test_data_size = 4096;  // 4KB of test data
    unsigned char* test_buffer = malloc(test_data_size);
    if (!test_buffer) {
        nwipe_log(NWIPE_LOG_ERROR, "Validation failed: Unable to allocate memory for test buffer.");
        return -1;
    }
    memset(test_buffer, 0, test_data_size);

    int outlen = 0;
    int total_generated = 0;
    // Input buffer of zeros (encrypting zeros yields the keystream)
    unsigned char zero_buf[64] = {0};

    while (total_generated < test_data_size) {
        int chunk_size = 64;  // 64-byte block
        if (EVP_EncryptUpdate(state->ctx, test_buffer + total_generated, &outlen,
                              zero_buf, chunk_size) != 1)
        {
            nwipe_log(NWIPE_LOG_ERROR,
                      "Failed to generate pseudorandom numbers during validation, error code: %lu.",
                      ERR_get_error());
            free(test_buffer);
            return -1;
        }
        total_generated += outlen;
    }

    // Bit frequency test
    unsigned long bit_count = 0;
    for (size_t i = 0; i < test_data_size; i++) {
        unsigned char byte = test_buffer[i];
        for (int j = 0; j < 8; j++) {
            if (byte & (1 << j))
                bit_count++;
        }
    }
    unsigned long total_bits = test_data_size * 8;
    double ones_ratio = (double)bit_count / total_bits;
    double zeros_ratio = 1.0 - ones_ratio;
    const double frequency_threshold = 0.02;  // 2% tolerance

    if (fabs(ones_ratio - 0.5) > frequency_threshold) {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Validation failed: Bit frequency test failed. Ones ratio: %.4f, Zeros ratio: %.4f",
                  ones_ratio, zeros_ratio);
        free(test_buffer);
        return -1;
    }

    // Byte frequency test and entropy calculation
    unsigned int byte_counts[256] = {0};
    for (size_t i = 0; i < test_data_size; i++) {
        byte_counts[test_buffer[i]]++;
    }
    double entropy = calculate_shannon_entropy(byte_counts, test_data_size);
    const double entropy_threshold = 7.5;  // Target: entropy ≥ 7.5 bits per byte
    if (entropy < entropy_threshold) {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Validation failed: Entropy too low. Calculated entropy: %.4f bits per byte",
                  entropy);
        free(test_buffer);
        return -1;
    }

    // Check for repeating patterns (e.g., all bytes are the same)
    int is_repeating = 1;
    for (size_t i = 1; i < test_data_size; i++) {
        if (test_buffer[i] != test_buffer[0]) {
            is_repeating = 0;
            break;
        }
    }
    if (is_repeating) {
        nwipe_log(NWIPE_LOG_ERROR, "Validation failed: Generated data contains repeating patterns.");
        free(test_buffer);
        return -1;
    }

    nwipe_log(NWIPE_LOG_DEBUG, "ChaCha20 PRNG validation passed. Entropy: %.4f bits per byte", entropy);
    free(test_buffer);
    return 0;
}

/**
 * Generates a 512-bit (64-byte) random block using ChaCha20 and writes it to the provided buffer.
 *
 * @param state Pointer to the initialized ChaCha20 PRNG state.
 * @param bufpos Target buffer where the pseudorandom data will be written.
 * @return int Returns 0 on success, -1 on failure.
 */
int chacha20_prng_genrand_uint512_to_buf(chacha20_state_t* state, unsigned char* bufpos)
{
    NWIPE_ASSERT(state != NULL && bufpos != NULL,
                 NWIPE_LOG_FATAL,
                 "Invalid parameters: state=%p, bufpos=%p",
                 (void*)state, (void*)bufpos);

    unsigned char temp_buffer[64] = {0};
    int outlen = 0;
    // Encrypting an input of zeros yields the keystream
    unsigned char input[64] = {0};

    if (EVP_EncryptUpdate(state->ctx, temp_buffer, &outlen, input, sizeof(input)) != 1) {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Failed to generate pseudorandom numbers, error code: %lu.",
                  ERR_get_error());
        return -1;
    }

    memcpy(bufpos, temp_buffer, sizeof(temp_buffer));
    return 0;
}

/**
 * General cleanup function for the ChaCha20 PRNG.
 * Frees all allocated resources and clears sensitive data.
 *
 * @param state Pointer to the ChaCha20 PRNG state.
 * @return int Returns 0 on success.
 */
int chacha20_prng_general_cleanup(chacha20_state_t* state)
{
    if (state != NULL) {
        if (state->ctx) {
            EVP_CIPHER_CTX_free(state->ctx);
            state->ctx = NULL;
        }
        memset(state->nonce, 0, CHACHA20_NONCE_SIZE);
    }
    return 0;
}

