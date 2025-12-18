/*
 *  prng.c: Pseudo Random Number Generator abstractions for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "nwipe.h"
#include "prng.h"
#include "context.h"
#include "logging.h"

#include "mt19937ar-cok/mt19937ar-cok.h"
#include "isaac_rand/isaac_rand.h"
#include "isaac_rand/isaac64.h"
#include "alfg/add_lagg_fibonacci_prng.h"  //Lagged Fibonacci generator prototype
#include "xor/xoroshiro256_prng.h"  //XORoshiro-256 prototype
#include "aes/aes_ctr_prng.h"  // AES-NI prototype

nwipe_prng_t nwipe_twister = { "Mersenne Twister", nwipe_twister_init, nwipe_twister_read };

nwipe_prng_t nwipe_isaac = { "ISAAC", nwipe_isaac_init, nwipe_isaac_read };
nwipe_prng_t nwipe_isaac64 = { "ISAAC-64", nwipe_isaac64_init, nwipe_isaac64_read };

/* ALFG PRNG Structure */
nwipe_prng_t nwipe_add_lagg_fibonacci_prng = { "Lagged Fibonacci",
                                               nwipe_add_lagg_fibonacci_prng_init,
                                               nwipe_add_lagg_fibonacci_prng_read };
/* XOROSHIRO-256 PRNG Structure */
nwipe_prng_t nwipe_xoroshiro256_prng = { "XORoshiro-256", nwipe_xoroshiro256_prng_init, nwipe_xoroshiro256_prng_read };

/* AES-CTR-NI PRNG Structure */
nwipe_prng_t nwipe_aes_ctr_prng = { "AES-CTR (Kernel)", nwipe_aes_ctr_prng_init, nwipe_aes_ctr_prng_read };

static const nwipe_prng_t* all_prngs[] = {
    &nwipe_twister,
    &nwipe_isaac,
    &nwipe_isaac64,
    &nwipe_add_lagg_fibonacci_prng,
    &nwipe_xoroshiro256_prng,
    &nwipe_aes_ctr_prng,
};

/* Print given number of bytes from unsigned integer number to a byte stream buffer starting with low-endian. */
static inline void u32_to_buffer( u8* restrict buffer, u32 val, const int len )
{
    for( int i = 0; i < len; ++i )
    {
        buffer[i] = (u8) ( val & 0xFFUL );
        val >>= 8;
    }
}
static inline void u64_to_buffer( u8* restrict buffer, u64 val, const int len )
{
    for( int i = 0; i < len; ++i )
    {
        buffer[i] = (u8) ( val & 0xFFULL );
        val >>= 8;
    }
}
static inline u32 isaac_nextval( randctx* restrict ctx )
{
    if( ctx->randcnt == 0 )
    {
        isaac( ctx );
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}
static inline u64 isaac64_nextval( rand64ctx* restrict ctx )
{
    if( ctx->randcnt == 0 )
    {
        isaac64( ctx );
        ctx->randcnt = RANDSIZ;
    }
    ctx->randcnt--;
    return ctx->randrsl[ctx->randcnt];
}

int nwipe_twister_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising Mersenne Twister prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( twister_state_t ) );
    }
    twister_init( (twister_state_t*) *state, (u32*) ( seed->s ), seed->length / sizeof( u32 ) );
    return 0;
}

int nwipe_twister_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_TWISTER;  // the values of twister_genrand_int32 is strictly 4 bytes

    /* Twister returns 4-bytes per call, so progress by 4 bytes. */
    for( size_t ii = 0; ii < words; ++ii )
    {
        u32_to_buffer( bufpos, twister_genrand_int32( (twister_state_t*) *state ), SIZE_OF_TWISTER );
        bufpos += SIZE_OF_TWISTER;
    }

    /* If there is some remainder copy only relevant number of bytes to not
     * overflow the buffer. */
    const size_t remain = count % SIZE_OF_TWISTER;  // SIZE_OF_TWISTER is strictly 4 bytes
    if( remain > 0 )
    {
        u32_to_buffer( bufpos, twister_genrand_int32( (twister_state_t*) *state ), remain );
    }

    return 0;
}

int nwipe_isaac_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    int count;
    randctx* isaac_state = *state;

    nwipe_log( NWIPE_LOG_NOTICE, "Initialising Isaac prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( randctx ) );
        isaac_state = *state;

        /* Check the memory allocation. */
        if( isaac_state == 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "malloc" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the isaac state." );
            return -1;
        }
    }

    /* Take the minimum of the isaac seed size and available entropy. */
    if( sizeof( isaac_state->randrsl ) < seed->length )
    {
        count = sizeof( isaac_state->randrsl );
    }
    else
    {
        memset( isaac_state->randrsl, 0, sizeof( isaac_state->randrsl ) );
        count = seed->length;
    }

    if( count == 0 )
    {
        /* Start ISACC without a seed. */
        randinit( isaac_state, 0 );
    }
    else
    {
        /* Seed the ISAAC state with entropy. */
        memcpy( isaac_state->randrsl, seed->s, count );

        /* The second parameter indicates that randrsl is non-empty. */
        randinit( isaac_state, 1 );
    }

    return 0;
}

int nwipe_isaac_read( NWIPE_PRNG_READ_SIGNATURE )
{
    randctx* isaac_state = *state;
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC;  // the values of isaac is strictly 4 bytes

    /* Isaac returns 4-bytes per call, so progress by 4 bytes. */
    for( size_t ii = 0; ii < words; ++ii )
    {
        /* get the next 32bit random number */
        u32_to_buffer( bufpos, isaac_nextval( isaac_state ), SIZE_OF_ISAAC );
        bufpos += SIZE_OF_ISAAC;
    }

    /* If there is some remainder copy only relevant number of bytes to not overflow the buffer. */
    const size_t remain = count % SIZE_OF_ISAAC;  // SIZE_OF_ISAAC is strictly 4 bytes
    if( remain > 0 )
    {
        u32_to_buffer( bufpos, isaac_nextval( isaac_state ), remain );
    }

    return 0;
}

int nwipe_isaac64_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    int count;
    rand64ctx* isaac_state = *state;

    nwipe_log( NWIPE_LOG_NOTICE, "Initialising ISAAC-64 prng" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( rand64ctx ) );
        isaac_state = *state;

        /* Check the memory allocation. */
        if( isaac_state == 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "malloc" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the isaac state." );
            return -1;
        }
    }

    /* Take the minimum of the isaac seed size and available entropy. */
    if( sizeof( isaac_state->randrsl ) < seed->length )
    {
        count = sizeof( isaac_state->randrsl );
    }
    else
    {
        memset( isaac_state->randrsl, 0, sizeof( isaac_state->randrsl ) );
        count = seed->length;
    }

    if( count == 0 )
    {
        /* Start ISACC without a seed. */
        rand64init( isaac_state, 0 );
    }
    else
    {
        /* Seed the ISAAC state with entropy. */
        memcpy( isaac_state->randrsl, seed->s, count );

        /* The second parameter indicates that randrsl is non-empty. */
        rand64init( isaac_state, 1 );
    }

    return 0;
}

int nwipe_isaac64_read( NWIPE_PRNG_READ_SIGNATURE )
{
    rand64ctx* isaac_state = *state;
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ISAAC64;  // the values of ISAAC-64 is strictly 8 bytes

    for( size_t ii = 0; ii < words; ++ii )
    {
        u64_to_buffer( bufpos, isaac64_nextval( isaac_state ), SIZE_OF_ISAAC64 );
        bufpos += SIZE_OF_ISAAC64;
    }

    /* If there is some remainder copy only relevant number of bytes to not overflow the buffer. */
    const size_t remain = count % SIZE_OF_ISAAC64;  // SIZE_OF_ISAAC64 is strictly 8 bytes
    if( remain > 0 )
    {
        u64_to_buffer( bufpos, isaac64_nextval( isaac_state ), remain );
    }

    return 0;
}

/* Implementation of Lagged Fibonacci generator a lot of random numbers */
int nwipe_add_lagg_fibonacci_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    if( *state == NULL )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "Initialising Lagged Fibonacci generator PRNG" );
        *state = malloc( sizeof( add_lagg_fibonacci_state_t ) );
    }
    add_lagg_fibonacci_init(
        (add_lagg_fibonacci_state_t*) *state, (uint64_t*) ( seed->s ), seed->length / sizeof( uint64_t ) );

    return 0;
}

/* Implementation of XORoroshiro256 algorithm to provide high-quality, but a lot of random numbers */
int nwipe_xoroshiro256_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initialising XORoroshiro-256 PRNG" );

    if( *state == NULL )
    {
        /* This is the first time that we have been called. */
        *state = malloc( sizeof( xoroshiro256_state_t ) );
    }
    xoroshiro256_init( (xoroshiro256_state_t*) *state, (uint64_t*) ( seed->s ), seed->length / sizeof( uint64_t ) );

    return 0;
}

int nwipe_add_lagg_fibonacci_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;

    /* Loop to fill the buffer with blocks directly from the Fibonacci algorithm */
    for( size_t ii = 0; ii < words; ++ii )
    {
        add_lagg_fibonacci_genrand_uint256_to_buf( (add_lagg_fibonacci_state_t*) *state, bufpos );
        bufpos += SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;  // Move to the next block
    }

    /* Handle remaining bytes if count is not a multiple of SIZE_OF_ADD_LAGG_FIBONACCI_PRNG */
    const size_t remain = count % SIZE_OF_ADD_LAGG_FIBONACCI_PRNG;
    if( remain > 0 )
    {
        unsigned char temp_output[16];  // Temporary buffer for the last block
        add_lagg_fibonacci_genrand_uint256_to_buf( (add_lagg_fibonacci_state_t*) *state, temp_output );

        // Copy the remaining bytes
        memcpy( bufpos, temp_output, remain );
    }

    return 0;  // Success
}

int nwipe_xoroshiro256_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    u8* restrict bufpos = buffer;
    size_t words = count / SIZE_OF_XOROSHIRO256_PRNG;

    /* Loop to fill the buffer with blocks directly from the XORoshiro256 algorithm */
    for( size_t ii = 0; ii < words; ++ii )
    {
        xoroshiro256_genrand_uint256_to_buf( (xoroshiro256_state_t*) *state, bufpos );
        bufpos += SIZE_OF_XOROSHIRO256_PRNG;  // Move to the next block
    }

    /* Handle remaining bytes if count is not a multiple of SIZE_OF_XOROSHIRO256_PRNG */
    const size_t remain = count % SIZE_OF_XOROSHIRO256_PRNG;
    if( remain > 0 )
    {
        unsigned char temp_output[16];  // Temporary buffer for the last block
        xoroshiro256_genrand_uint256_to_buf( (xoroshiro256_state_t*) *state, temp_output );

        // Copy the remaining bytes
        memcpy( bufpos, temp_output, remain );
    }

    return 0;  // Success
}

/**
 * @brief Initialize the AES-CTR PRNG state for this thread.
 *
 * @details
 * Initializes the thread-local PRNG based on the supplied seed and resets the
 * ring-buffer prefetch cache. The underlying AES-CTR implementation uses a
 * persistent AF_ALG operation socket per thread, opened lazily by
 * aes_ctr_prng_init(). The public state only stores a 128-bit counter while
 * the kernel keeps the expanded AES key schedule.
 *
 * @param[in,out] state  Pointer to an opaque PRNG state handle. If `*state` is
 *                       `NULL`, this function allocates it with `calloc()`.
 * @param[in]     seed   Seed material (must contain at least 32 bytes).
 * @param[in]     ...    Remaining parameters as defined by NWIPE_PRNG_INIT_SIGNATURE.
 *
 * @note
 * The ring is intentionally left empty to keep init fast. Callers may choose to
 * "prefill" by invoking refill_stash_thread_local(*state, SIZE_OF_AES_CTR_PRNG)
 * once to amortize first-use latency for tiny reads.
 *
 * @retval 0  Success.
 * @retval -1 Allocation or initialization failure (already logged).
 */

/*
 * High-throughput wrapper with a thread-local ring-buffer prefetch
 * ----------------------------------------------------------------
 * This glue layer implements NWIPE_PRNG_INIT / NWIPE_PRNG_READ around the
 * persistent kernel-AES PRNG. It maintains a lock-free, thread-local ring
 * buffer ("stash") that caches keystream blocks produced in fixed-size chunks
 * (SIZE_OF_AES_CTR_PRNG; e.g., 16 KiB or 256 KiB).
 *
 * Rationale:
 *  - Nwipe frequently requests small slices (e.g., 32 B, 512 B, 4 KiB). Issuing
 *    one kernel call per small read would be syscall- and copy-bound.
 *  - By fetching larger chunks and serving small reads from the ring buffer,
 *    we reduce syscall rate and memory traffic and approach memcpy-limited
 *    throughput on modern CPUs with AES acceleration.
 *
 * Why a ring buffer (over a linear stash + memmove):
 *  - No O(n) memmove() when the buffer fills with a tail of unread bytes.
 *  - Constant-time head/tail updates via modulo arithmetic.
 *  - Better cache locality and fewer TLB/cache misses; improved prefetching.
 */

/** @def NW_THREAD_LOCAL
 *  @brief Portable thread-local specifier for C11 and GNU C.
 *
 *  The ring buffer and its indices are thread-local, so no synchronization
 *  (locks/atomics) is required. Do not share this state across threads.
 */
#if defined( __STDC_VERSION__ ) && __STDC_VERSION__ >= 201112L
#define NW_THREAD_LOCAL _Thread_local
#else
#define NW_THREAD_LOCAL __thread
#endif

/** @def NW_ALIGN
 *  @brief Minimal alignment helper for hot buffers/structures.
 *
 *  64-byte alignment targets typical cacheline boundaries to reduce false
 *  sharing and improve hardware prefetch effectiveness for linear scans.
 */
#if defined( __GNUC__ ) || defined( __clang__ )
#define NW_ALIGN( N ) __attribute__( ( aligned( N ) ) )
#else
#define NW_ALIGN( N ) _Alignas( N )
#endif

/**
 * @def STASH_CAPACITY
 * @brief Ring capacity in bytes (power-of-two; multiple of CHUNK).
 *
 * @details
 * Defaults to 1 MiB. Must be:
 *   - a power of two (allows modulo via bitmask),
 *   - a multiple of SIZE_OF_AES_CTR_PRNG, so each produced chunk fits whole.
 *
 * @note
 * Practical choices: 512 KiB … 4 MiB depending on CHUNK size and workload.
 * For SIZE_OF_AES_CTR_PRNG = 256 KiB, 1 MiB yields four in-flight chunks and
 * works well for nwipe’s small-read patterns.
 */
#ifndef STASH_CAPACITY
#define STASH_CAPACITY ( 1u << 20 ) /* 1 MiB */
#endif

#if defined( __STDC_VERSION__ ) && __STDC_VERSION__ >= 201112L
_Static_assert( ( STASH_CAPACITY & ( STASH_CAPACITY - 1 ) ) == 0, "STASH_CAPACITY must be a power of two" );
_Static_assert( ( STASH_CAPACITY % SIZE_OF_AES_CTR_PRNG ) == 0,
                "STASH_CAPACITY must be a multiple of SIZE_OF_AES_CTR_PRNG" );
#endif

/** @brief Thread-local ring buffer storage for prefetched keystream. */
NW_THREAD_LOCAL static unsigned char stash[STASH_CAPACITY] NW_ALIGN( 64 );

/**
 * @name Ring indices (thread-local)
 * @{
 * @var rb_head  Next read position (consumer cursor).
 * @var rb_tail  Next write position (producer cursor).
 * @var rb_count Number of valid bytes currently stored.
 *
 * @invariant
 *   - 0 <= rb_count <= STASH_CAPACITY
 *   - rb_head, rb_tail in [0, STASH_CAPACITY)
 *   - (rb_tail - rb_head) mod STASH_CAPACITY == rb_count
 *
 * @warning
 * These variables are TLS and must not be accessed from or shared with other
 * threads. One PRNG instance per thread.
 * @}
 */
NW_THREAD_LOCAL static size_t rb_head = 0; /* next byte to read */
NW_THREAD_LOCAL static size_t rb_tail = 0; /* next byte to write */
NW_THREAD_LOCAL static size_t rb_count = 0; /* occupied bytes */

/**
 * @brief Free space available in the ring (bytes).
 * @return Number of free bytes (0 … STASH_CAPACITY).
 */
static inline size_t rb_free( void )
{
    return STASH_CAPACITY - rb_count;
}

/**
 * @brief Contiguous readable bytes starting at @c rb_head (no wrap).
 * @return Number of contiguous bytes available to read without split memcpy.
 */
static inline size_t rb_contig_used( void )
{
    size_t to_end = STASH_CAPACITY - rb_head;
    return ( rb_count < to_end ) ? rb_count : to_end;
}

/**
 * @brief Contiguous writable bytes starting at @c rb_tail (no wrap).
 * @return Number of contiguous bytes available to write without wrap.
 */
static inline size_t rb_contig_free( void )
{
    size_t to_end = STASH_CAPACITY - rb_tail;
    size_t free = rb_free();
    return ( free < to_end ) ? free : to_end;
}

/**
 * @brief Ensure at least @p need bytes are buffered in the ring.
 *
 * @details
 * Production model:
 *  - The kernel PRNG produces keystream in fixed-size chunks
 *    (SIZE_OF_AES_CTR_PRNG bytes; e.g., 16 KiB or 256 KiB).
 *  - We only ever append *whole* chunks. If total free space is less than one
 *    chunk, no production occurs (non-blocking style); the caller should first
 *    consume data and try again.
 *
 * Wrap handling:
 *  - Fast path: if a contiguous free region of at least one chunk exists at
 *    @c rb_tail, generate directly into @c stash + rb_tail (zero extra copies).
 *  - Wrap path: otherwise, generate one chunk into a small temporary buffer and
 *    split-copy into [rb_tail..end) and [0..rest). This case is infrequent and
 *    still cheaper than memmoving ring contents.
 *
 * @param[in] state  Pointer to the AES-CTR state (per-thread).
 * @param[in] need   Minimum number of bytes the caller would like to have ready.
 *
 * @retval 0  Success (or no space to produce yet).
 * @retval -1 PRNG failure (aes_ctr_prng_genrand_128k_to_buf() error).
 *
 * @warning
 * Thread-local only. Do not call concurrently from multiple threads that share
 * the same TLS variables.
 */
static int refill_stash_thread_local( void* state, size_t need )
{
    while( rb_count < need )
    {
        /* Not enough total free space for a full CHUNK → let the caller read first. */
        if( rb_free() < SIZE_OF_AES_CTR_PRNG )
            break;

        size_t cf = rb_contig_free();
        if( cf >= SIZE_OF_AES_CTR_PRNG )
        {
            /* Fast path: generate straight into the ring. */
            if( aes_ctr_prng_genrand_128k_to_buf( (aes_ctr_state_t*) state, stash + rb_tail ) != 0 )
                return -1;
            rb_tail = ( rb_tail + SIZE_OF_AES_CTR_PRNG ) & ( STASH_CAPACITY - 1 );
            rb_count += SIZE_OF_AES_CTR_PRNG;
        }
        else
        {
            /* Wrap path: temporary production, then split-copy. */
            unsigned char tmp[SIZE_OF_AES_CTR_PRNG];
            if( aes_ctr_prng_genrand_128k_to_buf( (aes_ctr_state_t*) state, tmp ) != 0 )
                return -1;
            size_t first = STASH_CAPACITY - rb_tail; /* bytes to physical end */
            memcpy( stash + rb_tail, tmp, first );
            memcpy( stash, tmp + first, SIZE_OF_AES_CTR_PRNG - first );
            rb_tail = ( rb_tail + SIZE_OF_AES_CTR_PRNG ) & ( STASH_CAPACITY - 1 );
            rb_count += SIZE_OF_AES_CTR_PRNG;
        }
    }
    return 0;
}

/* ---------------- PRNG INIT ---------------- */

/**
 * @brief Thread-local initialization wrapper around @c aes_ctr_prng_init().
 *
 * @param[in,out] state  Address of the caller’s PRNG state pointer. If `*state`
 *                       is `NULL`, this function allocates one `aes_ctr_state_t`.
 * @param[in]     seed   Seed descriptor as defined by NWIPE_PRNG_INIT_SIGNATURE.
 *
 * @retval 0  Success.
 * @retval -1 Allocation or backend initialization failure (logged).
 *
 * @note
 * Resets the ring buffer to empty. Consider a one-time prefill if your workload
 * is dominated by tiny reads.
 */
int nwipe_aes_ctr_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
    nwipe_log( NWIPE_LOG_NOTICE, "Initializing AES-CTR PRNG (thread-local ring buffer)" );

    if( *state == NULL )
    {
        *state = calloc( 1, sizeof( aes_ctr_state_t ) );
        if( *state == NULL )
        {
            nwipe_log( NWIPE_LOG_FATAL, "calloc() failed for PRNG state" );
            return -1;
        }
    }

    int rc = aes_ctr_prng_init(
        (aes_ctr_state_t*) *state, (unsigned long*) seed->s, seed->length / sizeof( unsigned long ) );
    if( rc != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "aes_ctr_prng_init() failed" );
        return -1;
    }

    /* Reset ring to empty. */
    rb_head = rb_tail = rb_count = 0;
    return 0;
}

/* ---------------- PRNG READ ---------------- */

/**
 * @brief Copy @p count bytes of keystream into @p buffer.
 *
 * @details
 * Strategy:
 *  - If the request is "large" (>= CHUNK) and the ring is empty, use the
 *    direct-fill fast path and generate full CHUNKs directly into the output
 *    buffer to avoid an extra memcpy.
 *  - Otherwise, serve from the ring:
 *      * Ensure at least one byte is available via @c refill_stash_thread_local
 *        (non-blocking; production occurs only if one full CHUNK fits).
 *      * Copy the largest contiguous block starting at @c rb_head.
 *      * Opportunistically prefetch when sufficient free space exists to keep
 *        latency low for upcoming small reads.
 *
 * @param[out] buffer     Destination buffer to receive keystream.
 * @param[in]  count      Number of bytes to generate and copy.
 * @param[in]  ...        Remaining parameters as defined by NWIPE_PRNG_READ_SIGNATURE.
 *
 * @retval 0  Success (exactly @p count bytes written).
 * @retval -1 Backend/IO failure (already logged).
 *
 * @warning
 * Per-thread API: do not share this state across threads.
 */
int nwipe_aes_ctr_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
    unsigned char* out = buffer;
    size_t bytes_left = count;

    /* Fast path: for large reads, bypass the ring if currently empty.
     * Generate full CHUNKs directly into the destination to save one memcpy. */
    while( bytes_left >= SIZE_OF_AES_CTR_PRNG && rb_count == 0 )
    {
        if( aes_ctr_prng_genrand_128k_to_buf( (aes_ctr_state_t*) *state, out ) != 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR, "PRNG direct fill failed" );
            return -1;
        }
        out += SIZE_OF_AES_CTR_PRNG;
        bytes_left -= SIZE_OF_AES_CTR_PRNG;
    }

    /* General path: serve from ring, refilling as needed. */
    while( bytes_left > 0 )
    {
        /* Ensure at least one byte is available for tiny reads. Refill only
         * produces if a full CHUNK fits; otherwise we try again once consumer
         * progress frees enough space. */
        if( rb_count == 0 )
        {
            if( refill_stash_thread_local( *state, 1 ) != 0 )
            {
                nwipe_log( NWIPE_LOG_ERROR, "PRNG refill failed" );
                return -1;
            }
            if( rb_count == 0 )
                continue; /* still no room for a CHUNK yet */
        }

        /* Copy the largest contiguous span starting at rb_head. */
        size_t avail = rb_contig_used();
        size_t take = ( bytes_left < avail ) ? bytes_left : avail;

        memcpy( out, stash + rb_head, take );

        rb_head = ( rb_head + take ) & ( STASH_CAPACITY - 1 );
        rb_count -= take;
        out += take;
        bytes_left -= take;

        /* Opportunistic prefetch to hide latency of future small reads. */
        if( rb_free() >= ( 2 * SIZE_OF_AES_CTR_PRNG ) )
        {
            if( refill_stash_thread_local( *state, SIZE_OF_AES_CTR_PRNG ) != 0 )
            {
                nwipe_log( NWIPE_LOG_ERROR, "PRNG opportunistic refill failed" );
                return -1;
            }
        }
    }
    return 0;
}

/* ----------------------------------------------------------------------
 * PRNG benchmark / auto-selection core
 * ---------------------------------------------------------------------- */

static double nwipe_prng_monotonic_seconds( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
}

/* einfacher LCG zum Seed-Befüllen – nur für Benchmark, kein Kryptokram */
static void nwipe_prng_make_seed( unsigned char* seed, size_t len )
{
    unsigned long t = (unsigned long) time( NULL );
    unsigned long x = ( t ^ 0xA5A5A5A5UL ) + (unsigned long) (uintptr_t) seed;

    for( size_t i = 0; i < len; i++ )
    {
        x = x * 1664525UL + 1013904223UL;
        seed[i] = (unsigned char) ( ( x >> 16 ) & 0xFF );
    }
}

static void* nwipe_prng_alloc_aligned( size_t alignment, size_t size )
{
    void* p = NULL;
    if( posix_memalign( &p, alignment, size ) != 0 )
        return NULL;
    return p;
}

static void nwipe_prng_bench_one( const nwipe_prng_t* prng,
                                  nwipe_prng_bench_result_t* out,
                                  void* io_buf,
                                  size_t io_block,
                                  double seconds_per_prng )
{
    void* state = NULL;

    unsigned char seedbuf[4096];
    nwipe_entropy_t seed;
    seed.s = (u8*) seedbuf;
    seed.length = sizeof( seedbuf );
    nwipe_prng_make_seed( seedbuf, sizeof( seedbuf ) );

    out->prng = prng;
    out->mbps = 0.0;
    out->seconds = 0.0;
    out->bytes = 0;
    out->rc = 0;

    int rc = prng->init( &state, &seed );
    if( rc != 0 )
    {
        out->rc = rc;
        if( state )
            free( state );
        return;
    }

    const double t0 = nwipe_prng_monotonic_seconds();
    double now = t0;

    while( ( now - t0 ) < seconds_per_prng )
    {
        rc = prng->read( &state, io_buf, io_block );
        if( rc != 0 )
        {
            out->rc = rc;
            break;
        }

        out->bytes += (unsigned long long) io_block;
        now = nwipe_prng_monotonic_seconds();
    }

    out->seconds = now - t0;
    if( out->rc == 0 && out->seconds > 0.0 )
    {
        out->mbps = ( (double) out->bytes / ( 1024.0 * 1024.0 ) ) / out->seconds;
    }

    if( state )
        free( state );
}

int nwipe_prng_benchmark_all( double seconds_per_prng,
                              size_t io_block_bytes,
                              nwipe_prng_bench_result_t* results,
                              size_t results_count )
{
    if( results == NULL || results_count == 0 )
        return 0;

    /* Anzahl PRNGs begrenzen auf results_count */
    size_t max = sizeof( all_prngs ) / sizeof( all_prngs[0] );
    if( results_count < max )
        max = results_count;

    void* io_buf = nwipe_prng_alloc_aligned( 4096, io_block_bytes );
    if( !io_buf )
    {
        nwipe_log( NWIPE_LOG_ERROR, "PRNG benchmark: unable to allocate %zu bytes buffer", io_block_bytes );
        return -1;
    }

    for( size_t i = 0; i < max; i++ )
    {
        nwipe_prng_bench_result_t* r = &results[i];
        memset( r, 0, sizeof( *r ) );
        nwipe_prng_bench_one( all_prngs[i], r, io_buf, io_block_bytes, seconds_per_prng );
    }

    free( io_buf );
    return (int) max;
}

const nwipe_prng_t* nwipe_prng_select_fastest( double seconds_per_prng,
                                               size_t io_block_bytes,
                                               nwipe_prng_bench_result_t* results,
                                               size_t results_count )
{
    int n = nwipe_prng_benchmark_all( seconds_per_prng, io_block_bytes, results, results_count );
    if( n <= 0 )
        return NULL;

    const nwipe_prng_t* best = NULL;
    double best_mbps = 0.0;

    for( int i = 0; i < n; i++ )
    {
        if( results[i].rc == 0 && results[i].mbps > best_mbps )
        {
            best_mbps = results[i].mbps;
            best = results[i].prng;
        }
    }

    if( best == NULL )
    {
        nwipe_log( NWIPE_LOG_WARNING, "Auto PRNG selection: no successful PRNG benchmark" );
    }

    return best;
}
