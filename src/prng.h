#ifndef PRNG_H_
#define PRNG_H_

#include <sys/types.h>

/* A chunk of random data. */
typedef struct
{
    size_t length;  // Length of the entropy string in bytes.
    u8* s;  // The actual bytes of the entropy string.
} nwipe_entropy_t;

#define NWIPE_PRNG_INIT_SIGNATURE void **state, nwipe_entropy_t *seed
#define NWIPE_PRNG_READ_SIGNATURE void **state, void *buffer, size_t count

/* Function pointers for PRNG actions. */
typedef int (*nwipe_prng_init_t)(NWIPE_PRNG_INIT_SIGNATURE);
typedef int (*nwipe_prng_read_t)(NWIPE_PRNG_READ_SIGNATURE);

/* The generic PRNG definition. */
typedef struct
{
    const char* label;      // The name of the pseudo random number generator.
    nwipe_prng_init_t init; // Initialize the prng state with the seed.
    nwipe_prng_read_t read; // Read data from the prng.
} nwipe_prng_t;

/* Mersenne Twister prototypes. */
int nwipe_twister_init(NWIPE_PRNG_INIT_SIGNATURE);
int nwipe_twister_read(NWIPE_PRNG_READ_SIGNATURE);

/* ISAAC prototypes. */
int nwipe_isaac_init(NWIPE_PRNG_INIT_SIGNATURE);
int nwipe_isaac_read(NWIPE_PRNG_READ_SIGNATURE);
int nwipe_isaac64_init(NWIPE_PRNG_INIT_SIGNATURE);
int nwipe_isaac64_read(NWIPE_PRNG_READ_SIGNATURE);

/* AES-CTR-NI prototypes. */
int nwipe_aes_ctr_prng_init(NWIPE_PRNG_INIT_SIGNATURE);
int nwipe_aes_ctr_prng_read(NWIPE_PRNG_READ_SIGNATURE);

/* Size of the twister is not derived from the architecture, but it is strictly 4 bytes */
#define SIZE_OF_TWISTER 4

/* Size of the isaac/isaac64 is not derived from the architecture, but it is strictly 4 or 8 bytes */
#define SIZE_OF_ISAAC 4
#define SIZE_OF_ISAAC64 8

/* Size of the AES-CTR is not derived from the architecture, but it is strictly 4 or 8 bytes */
#define SIZE_OF_AES_CTR_PRNG 4

#endif /* PRNG_H_ */
