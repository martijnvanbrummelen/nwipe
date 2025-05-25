#ifndef AES_CTR_PRNG_H
#define AES_CTR_PRNG_H

/* Minimal public header for AES-256-CTR PRNG (AES-NI backend) */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PRNG state: exactly 256 bits (4 × 64-bit words) */
typedef struct aes_ctr_state_s {
    uint64_t s[4];   /* s[0]=ctr_lo, s[1]=ctr_hi, s[2..3]=reserved */
} aes_ctr_state_t;

/* Initialise with ≥32 bytes of seed (init_key as unsigned-long array) */
int aes_ctr_prng_init(aes_ctr_state_t *state,
                      unsigned long    init_key[],
                      unsigned long    key_length);

/* Generate one 256-bit random value and write it to bufpos (32 bytes) */
int aes_ctr_prng_genrand_uint256_to_buf(aes_ctr_state_t *state,
                                        unsigned char   *bufpos);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AES_CTR_PRNG_H */

