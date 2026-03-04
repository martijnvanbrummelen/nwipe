/*
 * chacha20_test.h: ChaCha20 stream cipher CSPRNG for nwipe.
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * This software is provided "as is", without warranty of any kind.
 *
 * This implementation is intentionally kept simple and portable.
 * It should not require special hardware and run about everywhere.
 *
 * Hardware-specific accelerations are discouraged and, if present,
 * were not added by the original author. Anyone with 15 minutes of
 * time and the RFC should be able to understand and audit the code.
 */

#ifndef CHACHA20_TEST_H
#define CHACHA20_TEST_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    const char* name;
    const char* key_hex;
    const char* nonce_hex;
    uint64_t counter;
    const char* plaintext_hex;
    const char* ciphertext_hex;
} chacha20_test_vector_t;

extern const chacha20_test_vector_t chacha20_test_vectors[];
extern const size_t chacha20_test_vectors_count;

#endif /* CHACHA20_TEST_H */
