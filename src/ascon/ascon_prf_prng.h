/* -------------------------------------------------------------------------
 *  ascon_prf_prf_prng.h
 *
 *  High-throughput streaming PRNG based on Ascon-PRF v1.3 (variant 6).
 *  Exposes a minimal API for cryptographically secure pseudorandom data
 *  generation using the full 320-bit permutation state.
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
 *
 *  DESIGN GOAL
 *  ------------  Serve as many bytes as possible per permutation.  By keeping
 *  all 320 bits of the Ascon state in RAM and copying them out after *every*
 *  12-round call, we harvest 40 bytes / permutation – 2.5× more efficient
 *  than calling `crypto_auth()` four times to get 64 bytes.
 *
 *  EXPORTED API (nwipe-style)
 *  --------------------------
 *      typedef struct ascon_prf_prng_state_s { … } ascon_prf_prng_state_t;
 *
 *      #define ASCON_PRNG_BLOCK_BYTES 40   // bytes squeezed per P12
 *
 *      void ascon_prf_prng_init(ascon_prf_prng_state_t *st,
 *                                const uint8_t seed[16]);
 *
 *      void ascon_prf_prng_gen (ascon_prf_prng_state_t *st,
 *                                uint8_t *out, size_t outlen);
 * ------------------------------------------------------------------------*/

#ifndef ASCON_PRF_PRNG_H
#define ASCON_PRF_PRNG_H

#include <stddef.h>
#include <stdint.h>

/* One P12 permutation ⇒ 5 × 64-bit words ⇒ 40 bytes */
#define ASCON_PRNG_BLOCK_BYTES 40

/* Full internal permutation state + a shadow buffer for cheap memcpy */
typedef struct ascon_prf_prng_state_s {
    uint64_t x0, x1, x2, x3, x4;          /* 5-word Ascon state          */
    uint8_t  idx;                         /* next unread byte in buf     */
    uint8_t  buf[ASCON_PRNG_BLOCK_BYTES]; /* cached squeeze bytes  */
} ascon_prf_prng_state_t;

/* Initialise with any 16-byte seed (key) – e.g. from /dev/urandom */
void ascon_prf_prng_init(ascon_prf_prng_state_t *st,
                          const uint8_t seed[16]);

/* Fill `out` with `outlen` bytes of keystream */
void ascon_prf_prng_gen (ascon_prf_prng_state_t *st,
                          uint8_t *out, size_t outlen);

#endif /* ASCON_PRF_PRNG_H */

