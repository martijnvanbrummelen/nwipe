/*
 * aes_ctr_prng_aesni.c  –  minimal AES‑256‑CTR PRNG (AES‑NI)
 * ---------------------------------------------------------
 *   • State struct is now exactly 256 bits (4×u64), defined in header.
 *   • Each call to aes_ctr_prng_genrand_uint256_to_buf() outputs 32 bytes
 *     (2 AES blocks) – no internal cache needed.
 *
 * Build:  gcc -O3 -march=native -maes -std=c11 aes_ctr_prng_aesni.c -o test
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

/* ─── Runtime AES‑NI detection ───────────────────────────────────────── */
static int cpu_has_aesni(void)
{
    unsigned int eax, ebx, ecx, edx;
    if(!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return 0;
    return (ecx & bit_AES) != 0;
}

/* ─── AES‑256 key schedule (global, process‑wide) ────────────────────── */
static __m128i g_rk[15];
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

static inline void aes_block(const uint8_t in[16], uint8_t out[16])
{
    __m128i m = _mm_loadu_si128((const __m128i*)in);
    m ^= g_rk[0];
    for(int i=1;i<14;++i) m = _mm_aesenc_si128(m, g_rk[i]);
    m = _mm_aesenclast_si128(m, g_rk[14]);
    _mm_storeu_si128((__m128i*)out, m);
}

/* ─── Counter increment (little endian) ─────────────────────────────── */
static inline void ctr_inc(uint64_t *lo, uint64_t *hi)
{
    if(++(*lo) == 0) ++(*hi);
}

/* ─── Public API ─────────────────────────────────────────────────────── */
int aes_ctr_prng_init(aes_ctr_state_t *st,
                      unsigned long    init_key[],
                      unsigned long    key_len)
{
    PRNG_ASSERT(st && init_key && key_len, "bad args");
    if(!cpu_has_aesni()){ fprintf(stderr,"CPU lacks AES‑NI\n"); return -1; }

    const uint8_t *seed = (const uint8_t*)init_key;
    size_t bytes = key_len * sizeof(unsigned long);
    if(bytes < 32){ fprintf(stderr,"Seed must be ≥32 bytes\n"); return -1; }

    /* Key schedule (global) */
    aes256_expand(seed);

    /* Initial counter (next 16 bytes if present, otherwise zero) */
    uint64_t ctr_lo = 0, ctr_hi = 0;
    if(bytes >= 48){ memcpy(&ctr_lo, seed+32, 8); memcpy(&ctr_hi, seed+40, 8); }
    st->s[0] = ctr_lo;  /* little endian */
    st->s[1] = ctr_hi;
    st->s[2] = 0; st->s[3] = 0; /* reserved */
    return 0;
}

int aes_ctr_prng_genrand_uint256_to_buf(aes_ctr_state_t *st, unsigned char *out)
{
    PRNG_ASSERT(st && out, "bad args");
    if(!g_rk_ready){ fprintf(stderr,"PRNG not initialised\n"); return -1; }

    uint8_t ctr_block[16];
    uint64_t lo = st->s[0];
    uint64_t hi = st->s[1];

    /* Block 0 */
    memcpy(ctr_block, &lo, 8); memcpy(ctr_block+8, &hi, 8);
    aes_block(ctr_block, out);
    ctr_inc(&lo,&hi);

    /* Block 1 */
    memcpy(ctr_block, &lo, 8); memcpy(ctr_block+8, &hi, 8);
    aes_block(ctr_block, out+16);
    ctr_inc(&lo,&hi);

    /* Save updated counter */
    st->s[0] = lo; st->s[1] = hi;
    return 0;
}

