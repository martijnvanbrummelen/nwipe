#include <openssl/evp.h>
#include <cstring>
#include <cstdlib>

// Assuming aes_ctr_state_t and related constants are defined similarly in a C++ header

extern "C" {
#include "aes_ctr_prng.h"
}

// C++ PRNG class definition
class AesCtrPrngCpp {
public:
    EVP_CIPHER_CTX* ctx;
    unsigned char ivec[AES_BLOCK_SIZE];
    unsigned char ecount[AES_BLOCK_SIZE];
    int num;

    AesCtrPrngCpp() : ctx(nullptr), num(0) {
        memset(ivec, 0, AES_BLOCK_SIZE);
        memset(ecount, 0, AES_BLOCK_SIZE);
    }

    ~AesCtrPrngCpp() {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }

    void init(unsigned long* seed, unsigned long seed_len) {
        unsigned char key[32]; // Space for a 256-bit key

        // Initialize IV and counter
        memset(ivec, 0, AES_BLOCK_SIZE);
        num = 0;
        memset(ecount, 0, AES_BLOCK_SIZE);

        // Use EVP for SHA-256 hashing to generate the key from the provided seed
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(mdctx, reinterpret_cast<unsigned char*>(seed), seed_len * sizeof(unsigned long));
        EVP_DigestFinal_ex(mdctx, key, NULL);
        EVP_MD_CTX_free(mdctx);

        ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, ivec);
    }

    void generate(uint8_t* buffer, size_t count) {
        size_t blocks = count / 16;
        for (size_t i = 0; i < blocks; ++i) {
            EVP_EncryptUpdate(ctx, buffer + (i * 16), NULL, buffer + (i * 16), 16);
        }

        // Handle remaining bytes
        unsigned char temp[16];
        if (count % 16 > 0) {
            EVP_EncryptUpdate(ctx, temp, NULL, temp, 16);
            memcpy(buffer + (blocks * 16), temp, count % 16);
        }
    }
};

// C interface functions calling into the C++ implementation
void aes_ctr_prng_init(aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length) {
    AesCtrPrngCpp* prng = new AesCtrPrngCpp();
    prng->init(init_key, key_length);
    state->ctx = reinterpret_cast<void*>(prng);
}

void aes_ctr_prng_genrand_uint128_to_buf(aes_ctr_state_t* state, unsigned char* bufpos) {
    AesCtrPrngCpp* prng = reinterpret_cast<AesCtrPrngCpp*>(state->ctx);
    prng->generate(bufpos, 16);
}

