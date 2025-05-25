/*
 * aes_ctr_prng_parallel.c – AES‑CTR PRNG (4‑way parallel, AES‑NI / VAES)
 * ---------------------------------------------------------------------
 *  • Compile‑time parameters
 *        – PRNG_PARALLEL   : 4   (number of 128‑bit blocks per call)
 *        – PRNG_ROUNDS     : 10  (AES‑128 ⇒ 10 + final round)
 *  • Run‑time dispatch
 *        – VAES+AVX‑512 path (Ice‑Lake+, SapphireRapids, Zen4 AVX‑512, …)
 *        – classic AES‑NI path (any CPU with AES‑NI; AVX2 suffices)
 *  • API compatible mit vorheriger Version – erzeugt jetzt 64 Bytes / call
 *
 *  Build (fallback‑safe):
 *      gcc -O3 -maes -mavx2 -std=c11 \
 *          -DPRNG_PARALLEL=4 -DPRNG_ROUNDS=10 \
 *          aes_ctr_prng_parallel.c -o prng_test
 *
 *  Copyright (C) 2025 Fabian Druschke – MIT/Ascon‑Lizenz wie Original
 */

#include "aes_ctr_prng.h"
#include <immintrin.h>
#include <cpuid.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PRNG_ASSERT
# define PRNG_ASSERT(c,m) do{ if(!(c)){ fprintf(stderr,"[FATAL] %s\n",m); abort(); }}while(0)
#endif

/* ─── CPU Feature helpers ───────────────────────────────────────────── */
static int cpu_has_aesni(void)
{
    unsigned int eax, ebx, ecx, edx;
    if(!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    return (ecx & bit_AES) != 0;
}

/* --- AVX-512/VAES DISABLED --- */
/*
static int cpu_has_vaes_avx512(void)
{
    unsigned int eax, ebx, ecx, edx;
    if(!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 0;
    return ((ecx & (1u<<9)) && (ebx & (1u<<16)));
}
*/

/* ─── Round keys (global) ───────────────────────────────────────────── */
static __m128i g_rk[15];              /* always sized for AES‑256 schedule */
static int     g_rk_ready = 0;

static inline __m128i rk_step (__m128i a, __m128i b, int rc)
{
    __m128i t = _mm_aeskeygenassist_si128(b, rc);
    t = _mm_shuffle_epi32(t, _MM_SHUFFLE(3,3,3,3));
    a ^= _mm_slli_si128(a,4); a ^= _mm_slli_si128(a,4); a ^= _mm_slli_si128(a,4);
    return a ^ t;
}
static inline __m128i rk_step2(__m128i a)
{
    __m128i t = _mm_aeskeygenassist_si128(a, 0x00);
    t = _mm_shuffle_epi32(t, _MM_SHUFFLE(2,2,2,2));
    a ^= _mm_slli_si128(a,4); a ^= _mm_slli_si128(a,4); a ^= _mm_slli_si128(a,4);
    return a ^ t;
}
static void aes256_expand(const uint8_t key[32])
{
    __m128i k0 = _mm_loadu_si128((const __m128i*)key);
    __m128i k1 = _mm_loadu_si128((const __m128i*)(key+16));
    g_rk[0]=k0; g_rk[1]=k1;
    g_rk[2]= k0 = rk_step (k0,k1,0x01);
    g_rk[3]= k1 = rk_step2(k0);
    g_rk[4]= k0 = rk_step (k0,k1,0x02);
    g_rk[5]= k1 = rk_step2(k0);
    g_rk[6]= k0 = rk_step (k0,k1,0x04);
    g_rk[7]= k1 = rk_step2(k0);
    g_rk[8]= k0 = rk_step (k0,k1,0x08);
    g_rk[9]= k1 = rk_step2(k0);
    g_rk[10]=k0 = rk_step (k0,k1,0x10);
    g_rk[11]=k1 = rk_step2(k0);
    g_rk[12]=k0 = rk_step (k0,k1,0x20);
    g_rk[13]=k1 = rk_step2(k0);
    g_rk[14]=        rk_step (k0,k1,0x40);
    g_rk_ready = 1;
}

/* ─── Counter helpers ───────────────────────────────────────────────── */
static inline void ctr_inc(uint64_t *lo, uint64_t *hi, uint64_t n)
{
    uint64_t old = *lo;
    *lo += n;
    if(*lo < old) ++(*hi);            /* Übertrag */
}

/* ─── Dispatcher‑Methode‑Pointer ────────────────────────────────────── */
static int (*g_genrand_func)(aes_ctr_state_t*, unsigned char*) = NULL;

/* ─── 128‑bit helper (AVX2 fallback) ────────────────────────────────── */
static inline __m128i make_ctr(uint64_t lo, uint64_t hi)
{
    return _mm_set_epi64x((long long)hi, (long long)lo); /* little‑endian */
}

static int genrand4x_avx2(aes_ctr_state_t *st, unsigned char *out)
{
    uint64_t lo = st->s[0];
    uint64_t hi = st->s[1];

    __m128i ctr0 = make_ctr(lo    , hi);
    __m128i ctr1 = make_ctr(lo + 1, hi);
    __m128i ctr2 = make_ctr(lo + 2, hi);
    __m128i ctr3 = make_ctr(lo + 3, hi);

    __m128i b0 = _mm_xor_si128(ctr0, g_rk[0]);
    __m128i b1 = _mm_xor_si128(ctr1, g_rk[0]);
    __m128i b2 = _mm_xor_si128(ctr2, g_rk[0]);
    __m128i b3 = _mm_xor_si128(ctr3, g_rk[0]);

    for(int i=1;i<PRNG_ROUNDS;++i){
        b0 = _mm_aesenc_si128(b0, g_rk[i]);
        b1 = _mm_aesenc_si128(b1, g_rk[i]);
        b2 = _mm_aesenc_si128(b2, g_rk[i]);
        b3 = _mm_aesenc_si128(b3, g_rk[i]);
    }
    b0 = _mm_aesenclast_si128(b0, g_rk[PRNG_ROUNDS]);
    b1 = _mm_aesenclast_si128(b1, g_rk[PRNG_ROUNDS]);
    b2 = _mm_aesenclast_si128(b2, g_rk[PRNG_ROUNDS]);
    b3 = _mm_aesenclast_si128(b3, g_rk[PRNG_ROUNDS]);

    _mm_storeu_si128((__m128i*)(out     ), b0);
    _mm_storeu_si128((__m128i*)(out + 16), b1);
    _mm_storeu_si128((__m128i*)(out + 32), b2);
    _mm_storeu_si128((__m128i*)(out + 48), b3);

    ctr_inc(&lo,&hi, PRNG_PARALLEL);
    st->s[0] = lo; st->s[1] = hi;
    return 0;
}

/* --- AVX-512/VAES DISABLED --- */
/*
__attribute__((target("vaes,avx512f")))
static int genrand4x_vaes(aes_ctr_state_t *st, unsigned char *out)
{
    uint64_t lo = st->s[0];
    uint64_t hi = st->s[1];

    __m512i ctr = _mm512_set_epi64((long long)hi+0, (long long)(lo+3),
                                   (long long)hi+0, (long long)(lo+2),
                                   (long long)hi+0, (long long)(lo+1),
                                   (long long)hi+0, (long long)(lo+0));

    __m512i rk = _mm512_broadcast_i64x2(g_rk[0]);
    __m512i b  = _mm512_xor_si512(ctr, rk);

    for(int i=1;i<PRNG_ROUNDS;++i){
        rk = _mm512_broadcast_i64x2(g_rk[i]);
        b  = _mm512_aesenc_epi128(b, rk);
    }
    rk = _mm512_broadcast_i64x2(g_rk[PRNG_ROUNDS]);
    b  = _mm512_aesenclast_epi128(b, rk);

    _mm512_storeu_si512((__m512i*)out, b);

    ctr_inc(&lo,&hi, PRNG_PARALLEL);
    st->s[0] = lo; st->s[1] = hi;
    return 0;
}
*/

/* ─── Public API ────────────────────────────────────────────────────── */
int aes_ctr_prng_init(aes_ctr_state_t *st,
                      unsigned long    init_key[],
                      unsigned long    key_len)
{
    PRNG_ASSERT(st && init_key && key_len, "bad args");
    if(!cpu_has_aesni()){
        fprintf(stderr,"CPU lacks AES‑NI – unsupported PRNG\n");
        return -1;
    }

    const uint8_t *seed = (const uint8_t*)init_key;
    size_t bytes = key_len * sizeof(unsigned long);
    if(bytes < 32){ fprintf(stderr,"Seed must be ≥32 bytes\n"); return -1; }

    aes256_expand(seed);

    uint64_t ctr_lo = 0, ctr_hi = 0;
    if(bytes >= 48){ memcpy(&ctr_lo, seed+32, 8); memcpy(&ctr_hi, seed+40, 8); }
    st->s[0] = ctr_lo; st->s[1] = ctr_hi; st->s[2] = st->s[3] = 0;

    /* Always use AVX2 fallback */
    g_genrand_func = genrand4x_avx2;
    return 0;
}

int aes_ctr_prng_genrand_uint256_to_buf(aes_ctr_state_t *st, unsigned char *out)
{
    PRNG_ASSERT(st && out, "bad args");
    PRNG_ASSERT(g_rk_ready && g_genrand_func, "PRNG not initialised");
    return g_genrand_func(st, out);
}

