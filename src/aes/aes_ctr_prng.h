#ifndef AES_CTR_PRNG_H
#define AES_CTR_PRNG_H

/* Minimal public header for AES-256-CTR PRNG (Linux AF_ALG backend)
 *
 * Implementation detail:
 *   - Uses a persistent AF_ALG "ctr(aes)" operation socket opened at init.
 *   - No socket setup overhead during generation – only sendmsg + read.
 *   - Thread-safety: Not safe unless externally synchronized.
 *
 * Public state remains exactly 256 bits (4×64-bit words) to allow for
 * minimalistic integration in nwipe and similar tools.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PRNG state: exactly 256 bits (4 × 64-bit words)
 *
 * s[0] = counter low
 * s[1] = counter high
 * s[2], s[3] = reserved
 */
typedef struct aes_ctr_state_s {
    uint64_t s[4];
} aes_ctr_state_t;

/* Initialize with >=32 bytes of seed (init_key as unsigned-long array)
 *
 * On first call, also opens the persistent AF_ALG socket.
 * Returns 0 on success, -1 on failure.
 */
int aes_ctr_prng_init(aes_ctr_state_t *state,
                      unsigned long    init_key[],
                      unsigned long    key_length);

/* Generate one 128 KiB chunk of random data into bufpos.
 *
 * Returns 0 on success, -1 on failure.
 * Uses the persistent AF_ALG socket.
 */
int aes_ctr_prng_genrand_128k_to_buf(aes_ctr_state_t *state,
                                    unsigned char   *bufpos);

/* Optional: Close the persistent AF_ALG socket at program shutdown.
 *
 * Not required by nwipe, but recommended for tools embedding this code.
 */
int aes_ctr_prng_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AES_CTR_PRNG_H */

