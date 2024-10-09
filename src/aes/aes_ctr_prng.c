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
#include <math.h>

typedef enum {
    NWIPE_LOG_NONE = 0,
    NWIPE_LOG_DEBUG,   // Debugging messages, detailed for troubleshooting
    NWIPE_LOG_INFO,    // Informative logs, for regular operation insights
    NWIPE_LOG_NOTICE,  // Notices for significant but non-critical events
    NWIPE_LOG_WARNING, // Warnings about potential errors
    NWIPE_LOG_ERROR,   // Error messages, significant issues that affect operation
    NWIPE_LOG_FATAL,   // Fatal errors, require immediate termination of the program
    NWIPE_LOG_SANITY,  // Sanity checks, used primarily in debugging phases
    NWIPE_LOG_NOTIMESTAMP  // Log entries without timestamp information
} nwipe_log_t;

extern void nwipe_log(nwipe_log_t level, const char* format, ...);

/* Function prototypes */
int aes_ctr_prng_validate(aes_ctr_state_t* state);
static double calculate_shannon_entropy(const unsigned int* byte_counts, size_t data_length);

/* Initializes the AES CTR pseudorandom number generator state.
   This function sets up the cryptographic context necessary for generating
   pseudorandom numbers using AES in CTR mode. It utilizes SHA-256 to derive
   a key from the provided seed, ensuring that the PRNG output is unpredictable
   and secure, provided the seed is kept secret and is sufficiently random.
   - state: Pointer to the AES CTR PRNG state structure.
   - init_key: Array containing the seed for key generation.
   - key_length: Length of the seed array. */
int aes_ctr_prng_init(aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length)
{
    assert(state != NULL && init_key != NULL && key_length > 0);  // Validate inputs

    unsigned char key[32];  // Storage for the 256-bit key
    memset(state->ivec, 0, AES_BLOCK_SIZE);  // Clear IV buffer
    state->num = 0;  // Reset the block counter
    memset(state->ecount, 0, AES_BLOCK_SIZE);  // Clear encryption count buffer

    nwipe_log(NWIPE_LOG_DEBUG, "Initializing AES CTR PRNG with provided seed.");  // Log initialization

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();  // Create new SHA-256 context
    if (!mdctx)
    {
        nwipe_log(NWIPE_LOG_FATAL,
                  "Failed to allocate EVP_MD_CTX for SHA-256, error code: %lu.",
                  ERR_get_error());  // Log context allocation failure
        return -1;  // Handle error
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1)
    {
        nwipe_log(NWIPE_LOG_FATAL,
                  "SHA-256 context initialization failed, error code: %lu.",
                  ERR_get_error());  // Log init failure
        EVP_MD_CTX_free(mdctx);
        return -1;  // Handle error
    }

    EVP_DigestUpdate(
        mdctx, (const unsigned char*)init_key, key_length * sizeof(unsigned long));  // Process the seed

    if (EVP_DigestFinal_ex(mdctx, key, NULL) != 1)
    {
        nwipe_log(NWIPE_LOG_FATAL,
                  "SHA-256 hash finalization failed, error code: %lu.",
                  ERR_get_error());  // Log finalization failure
        EVP_MD_CTX_free(mdctx);
        return -1;  // Handle error
    }
    EVP_MD_CTX_free(mdctx);
    mdctx = NULL;  // Clean up SHA-256 context

    state->ctx = EVP_CIPHER_CTX_new();  // Create new AES-256-CTR context
    if (!state->ctx)
    {
        nwipe_log(NWIPE_LOG_FATAL,
                  "Failed to allocate EVP_CIPHER_CTX, error code: %lu.",
                  ERR_get_error());  // Log cipher context failure
        return -1;  // Handle error
    }

    if (EVP_EncryptInit_ex(state->ctx, EVP_aes_256_ctr(), NULL, key, state->ivec) != 1)
    {
        nwipe_log(NWIPE_LOG_FATAL,
                  "AES-256-CTR encryption context initialization failed, error code: %lu.",
                  ERR_get_error());  // Log encryption init failure
        EVP_CIPHER_CTX_free(state->ctx);
        return -1;  // Handle error
    }

    // Validate the pseudorandom data generation
    if (aes_ctr_prng_validate(state) != 0)
    {
        nwipe_log(NWIPE_LOG_FATAL, "AES CTR PRNG validation failed.");
        EVP_CIPHER_CTX_free(state->ctx);
        return -1;  // Handle validation failure
    }

    nwipe_log(NWIPE_LOG_DEBUG, "AES CTR PRNG successfully initialized and validated.");  // Log success
    return 0;  // Exit successfully
}

/* Validates the pseudorandom data generation by generating 4KB of data and performing statistical tests.
   This function generates 4KB of pseudorandom data and performs:
   - Bit Frequency Test
   - Byte Frequency Test
   - Entropy Calculation
   - Checks for any obvious patterns
   - Thresholds are set to detect non-random behavior.
   - state: Pointer to the initialized AES CTR PRNG state.
   Returns 0 on success, -1 on failure. */
int aes_ctr_prng_validate(aes_ctr_state_t* state)
{
    assert(state != NULL);

    const size_t test_data_size = 4096;  // 4KB of data
    unsigned char* test_buffer = malloc(test_data_size);
    if (!test_buffer)
    {
        nwipe_log(NWIPE_LOG_ERROR, "Validation failed: Unable to allocate memory for test buffer.");
        return -1;
    }
    memset(test_buffer, 0, test_data_size);  // Zero out the buffer
    int outlen = 0;  // Length of data produced by encryption
    int total_generated = 0;

    // Generate 4KB of pseudorandom data
    while (total_generated < test_data_size)
    {
        int chunk_size = 512;  // Generate data in chunks to avoid large allocations
        if (EVP_EncryptUpdate(state->ctx, test_buffer + total_generated, &outlen,
                              test_buffer + total_generated, chunk_size) != 1)
        {
            nwipe_log(NWIPE_LOG_ERROR,
                      "Failed to generate pseudorandom numbers during validation, error code: %lu.",
                      ERR_get_error());  // Log generation failure
            free(test_buffer);
            return -1;  // Handle error
        }
        total_generated += outlen;
    }

    // Bit Frequency Test
    unsigned long bit_count = 0;
    for (size_t i = 0; i < test_data_size; i++)
    {
        unsigned char byte = test_buffer[i];
        for (int j = 0; j < 8; j++)
        {
            if (byte & (1 << j))
                bit_count++;
        }
    }

    unsigned long total_bits = test_data_size * 8;
    double ones_ratio = (double)bit_count / total_bits;
    double zeros_ratio = 1.0 - ones_ratio;

    // Acceptable deviation from 50%
    const double frequency_threshold = 0.02;  // 2%

    if (fabs(ones_ratio - 0.5) > frequency_threshold)
    {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Validation failed: Bit frequency test failed. Ones ratio: %.4f, Zeros ratio: %.4f",
                  ones_ratio, zeros_ratio);
        free(test_buffer);
        return -1;
    }

    // Byte Frequency Test and Entropy Calculation
    unsigned int byte_counts[256] = {0};
    for (size_t i = 0; i < test_data_size; i++)
    {
        byte_counts[test_buffer[i]]++;
    }

    // Calculate Shannon entropy
    double entropy = calculate_shannon_entropy(byte_counts, test_data_size);

    // Adjusted entropy threshold
    const double entropy_threshold = 7.5;  // Target entropy ≥ 7.5 bits per byte
    if (entropy < entropy_threshold)
    {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Validation failed: Entropy too low. Calculated entropy: %.4f bits per byte",
                  entropy);
        free(test_buffer);
        return -1;
    }

    // Check for repeating patterns (e.g., all bytes are the same)
    int is_repeating = 1;
    for (size_t i = 1; i < test_data_size; i++)
    {
        if (test_buffer[i] != test_buffer[0])
        {
            is_repeating = 0;
            break;
        }
    }

    if (is_repeating)
    {
        nwipe_log(NWIPE_LOG_ERROR, "Validation failed: Generated data contains repeating patterns.");
        free(test_buffer);
        return -1;
    }

    nwipe_log(NWIPE_LOG_DEBUG, "AES CTR PRNG validation passed. Entropy: %.4f bits per byte", entropy);
    free(test_buffer);
    return 0;  // Validation successful
}

/* Calculates the Shannon entropy of the data based on byte frequencies.
   - byte_counts: Array of counts for each byte value (0-255).
   - data_length: Total number of bytes in the data.
   Returns the calculated entropy in bits per byte. */
static double calculate_shannon_entropy(const unsigned int* byte_counts, size_t data_length)
{
    double entropy = 0.0;
    for (int i = 0; i < 256; i++)
    {
        if (byte_counts[i] > 0)
        {
            double probability = (double)byte_counts[i] / data_length;
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}

/* Generates pseudorandom numbers and writes them to a buffer.
   This function performs the core operation of producing pseudorandom data.
   It directly updates the buffer provided, filling it with pseudorandom bytes
   generated using the AES-256-CTR mode of operation.
   - state: Pointer to the initialized AES CTR PRNG state.
   - bufpos: Target buffer where the pseudorandom numbers will be written.
   Returns 0 on success, -1 on failure. */
int aes_ctr_prng_genrand_uint256_to_buf(aes_ctr_state_t* state, unsigned char* bufpos)
{
    assert(state != NULL && bufpos != NULL);  // Validate inputs

    unsigned char temp_buffer[32];  // Temporary storage for pseudorandom bytes
    memset(temp_buffer, 0, sizeof(temp_buffer));  // Zero out temporary buffer
    int outlen;  // Length of data produced by encryption

    if (EVP_EncryptUpdate(state->ctx, temp_buffer, &outlen, temp_buffer, sizeof(temp_buffer)) != 1)
    {
        nwipe_log(NWIPE_LOG_ERROR,
                  "Failed to generate pseudorandom numbers, error code: %lu.",
                  ERR_get_error());  // Log generation failure
        return -1;  // Handle error
    }

    memcpy(bufpos, temp_buffer, sizeof(temp_buffer));  // Copy pseudorandom bytes to buffer
    return 0;  // Exit successfully
}

/* General cleanup function for AES CTR PRNG.
   Frees allocated resources and clears sensitive data.
   - state: Pointer to the AES CTR PRNG state structure.
   Returns 0 on success. */
int aes_ctr_prng_general_cleanup(aes_ctr_state_t* state)
{
    if (state != NULL)
    {
        // Free the EVP_CIPHER_CTX if it has been allocated
        if (state->ctx)
        {
            EVP_CIPHER_CTX_free(state->ctx);
            state->ctx = NULL;  // Nullify the pointer after free
        }

        // Clear sensitive information from the state
        memset(state->ivec, 0, AES_BLOCK_SIZE);
        memset(state->ecount, 0, AES_BLOCK_SIZE);
        state->num = 0;
    }
    return 0;
}

