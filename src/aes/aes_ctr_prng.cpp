#include <openssl/evp.h>
#include <cstring>
#include <cstdlib>
#include <memory>

// Assuming aes_ctr_state_t and related constants are defined similarly in a C++ header

extern "C" {
#include "aes_ctr_prng.h"
}

// Custom deleter for EVP_MD_CTX
struct EVP_MD_CTX_Deleter {
    void operator()(EVP_MD_CTX* ctx) const {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
    }
};

// Custom deleter for EVP_CIPHER_CTX
struct EVP_CIPHER_CTX_Deleter {
    void operator()(EVP_CIPHER_CTX* ctx) const {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};

class AesCtrPrngCpp {
public:
    std::unique_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_Deleter> ctx;
    unsigned char ivec[AES_BLOCK_SIZE];
    unsigned char ecount[AES_BLOCK_SIZE];
    int num;

    AesCtrPrngCpp() : ctx(nullptr), num(0) {
        memset(ivec, 0, AES_BLOCK_SIZE);
        memset(ecount, 0, AES_BLOCK_SIZE);
    }

    void init(unsigned long* seed, unsigned long seed_len) {
        unsigned char key[32]; // Space for a 256-bit key

        // Initialize IV and counter
        memset(ivec, 0, AES_BLOCK_SIZE);
        num = 0;
        memset(ecount, 0, AES_BLOCK_SIZE);

        // Use EVP for SHA-256 hashing to generate the key from the provided seed
        std::unique_ptr<EVP_MD_CTX, EVP_MD_CTX_Deleter> mdctx(EVP_MD_CTX_new());
        EVP_DigestInit_ex(mdctx.get(), EVP_sha256(), NULL);
        EVP_DigestUpdate(mdctx.get(), reinterpret_cast<unsigned char*>(seed), seed_len * sizeof(unsigned long));
        EVP_DigestFinal_ex(mdctx.get(), key, NULL);

        ctx.reset(EVP_CIPHER_CTX_new());
        EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_ctr(), NULL, key, ivec);
    }

    void generate(uint8_t* buffer, size_t count) {
        size_t blocks = count / 16;
        for (size_t i = 0; i < blocks; ++i) {
            EVP_EncryptUpdate(ctx.get(), buffer + (i * 16), NULL, buffer + (i * 16), 16);
        }

        // Handle remaining bytes
        unsigned char temp[16];
        if (count % 16 > 0) {
            EVP_EncryptUpdate(ctx.get(), temp, NULL, temp, 16);
            memcpy(buffer + (blocks * 16), temp, count % 16);
        }
    }
};

// C interface functions calling into the C++ implementation
void aes_ctr_prng_init(aes_ctr_state_t* state, unsigned long init_key[], unsigned long key_length) {
    AesCtrPrngCpp* prng = new AesCtrPrngCpp();
    prng->init(init_key, key_length);
    state->ctx = reinterpret_cast<void*>(prng); // Store the C++ object pointer in the C structure
}

void aes_ctr_prng_genrand_uint128_to_buf(aes_ctr_state_t* state, unsigned char* bufpos) {
    AesCtrPrngCpp* prng = reinterpret_cast<AesCtrPrngCpp*>(state->ctx);
    prng->generate(bufpos, 16);
}

