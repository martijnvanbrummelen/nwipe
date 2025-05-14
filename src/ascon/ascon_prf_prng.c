/* -------------------------------------------------------------------------
 *  ascon_prf_prf_prng.c
 *
 *  High-throughput, single-file Ascon-PRF v1.3 stream generator.
 *  Based on the NIST lightweight cryptography finalist Ascon.
 *
 *  Copyright (C) 2025 Fabian Druschke
 *  This file is released under the terms of the Ascon reference implementation:
 *
 *  Permission is hereby granted, free of charge, to use, copy, modify,
 *  and distribute this software and its documentation for any purpose,
 *  without fee, and without written agreement, provided that the above
 *  copyright notice and this paragraph appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 * ------------------------------------------------------------------------*/

#include "ascon_prf_prng.h"
#include <string.h>   /* memcpy */

/* === 1.  Low-level helpers ============================================= */

/* Round constants (from the Ascon specification, least-significant byte) */
#define RC0 0xf0u
#define RC1 0xe1u
#define RC2 0xd2u
#define RC3 0xc3u
#define RC4 0xb4u
#define RC5 0xa5u
#define RC6 0x96u
#define RC7 0x87u
#define RC8 0x78u
#define RC9 0x69u
#define RCa 0x5au
#define RCb 0x4bu

/* 64-bit right-rotation (positive, 0 ≤ n < 64) */
#define ROR64(x,n)  (((x) >> (n)) | ((x) << (64-(n))))

/* === 2.  Single Ascon round (5-lane Keccak-χ + linear diffusions) ======= */
#define ASCON_ROUND(a,b,c,d,e, C) do {                        \
    (c) ^= (uint64_t)(C);                                     \
    (a) ^= (e); (e) ^= (d); (c) ^= (b);                       \
    uint64_t t0 = (a) ^ (~(b) & (c));                         \
    uint64_t t2 = (c) ^ (~(d) & (e));                         \
    uint64_t t4 = (e) ^ (~(a) & (b));                         \
    uint64_t t1 = (b) ^ (~(c) & (d));                         \
    uint64_t t3 = (d) ^ (~(e) & (a));                         \
    t1 ^= t0; t3 ^= t2; t0 ^= t4;                             \
    (c) = t2 ^ ROR64(t2, 1) ^ ROR64(t2, 6);                   \
    (d) = t3 ^ ROR64(t3,10) ^ ROR64(t3,17);                   \
    (e) = t4 ^ ROR64(t4, 7) ^ ROR64(t4,41);                   \
    (a) = t0 ^ ROR64(t0,19) ^ ROR64(t0,28);                   \
    (b) = t1 ^ ROR64(t1,39) ^ ROR64(t1,61);                   \
    (c) = ~(c);                                               \
} while (0)

/* === 3.  Twelve-round permutation P12 =================================== */
static inline void ascon_prf_permute_p12(uint64_t *x0, uint64_t *x1,
                                     uint64_t *x2, uint64_t *x3,
                                     uint64_t *x4)
{
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC0);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC1);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC2);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC3);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC4);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC5);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC6);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC7);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC8);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RC9);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RCa);
    ASCON_ROUND(*x0,*x1,*x2,*x3,*x4, RCb);
}

/* === 4.  Initialisation ================================================== */
void ascon_prf_prng_init(ascon_prf_prng_state_t *st,
                          const uint8_t seed[16])
{
    uint64_t k0, k1;
    memcpy(&k0, seed,     8);
    memcpy(&k1, seed + 8, 8);

    /* ASCON_PRF_IV (variant 6, 12 rounds, zero-length message) */
    st->x0 = 0x0010200000cc0006ull;
    st->x1 = k0;
    st->x2 = k1;
    st->x3 = 0;
    st->x4 = 0;

    ascon_prf_permute_p12(&st->x0,&st->x1,&st->x2,&st->x3,&st->x4);

    st->idx = ASCON_PRNG_BLOCK_BYTES;   /* force refill on first use */
}

/* Copy 5×64-bit state words into `buf` and immediately permute again */
static inline void ascon_prf_refill(ascon_prf_prng_state_t *st)
{
    memcpy(st->buf +  0, &st->x0, 8);
    memcpy(st->buf +  8, &st->x1, 8);
    memcpy(st->buf + 16, &st->x2, 8);
    memcpy(st->buf + 24, &st->x3, 8);
    memcpy(st->buf + 32, &st->x4, 8);

    ascon_prf_permute_p12(&st->x0,&st->x1,&st->x2,&st->x3,&st->x4);
    st->idx = 0;
}

/* === 5.  Generate ======================================================== */
void ascon_prf_prng_gen(ascon_prf_prng_state_t *st,
                         uint8_t *out, size_t outlen)
{
    while (outlen) {
        if (st->idx == ASCON_PRNG_BLOCK_BYTES)
            ascon_prf_refill(st);

        size_t avail = (size_t)ASCON_PRNG_BLOCK_BYTES - st->idx;
        size_t n     = (outlen < avail) ? outlen : avail;

        memcpy(out, st->buf + st->idx, n);

        st->idx += (uint8_t)n;
        out     += n;
        outlen  -= n;
    }
}
