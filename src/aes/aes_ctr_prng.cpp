/**
 * @file
 * @brief High-throughput AES-CTR PRNG for nwipe using Linux AF_ALG.
 *
 * @details
 * This translation unit implements a cryptographically strong pseudorandom
 * byte stream based on AES-CTR, leveraging the Linux kernel's crypto API
 * (AF_ALG) for hardware-accelerated AES (AES-NI/VAES/NEON/SVE where available).
 *
 * Motivation:
 * - nwipe must supply multi-GB/s of random data to saturate modern NVMe/RAID.
 * - User-space OpenSSL-based paths in older builds plateaued around ~250 MB/s
 *   on some systems due to syscall/memory-copy patterns not tuned for the
 *   workload.
 * - The kernel provides highly optimized AES implementations and scheduling.
 *
 * Key properties:
 * - A single AF_ALG operation socket is opened *once per thread* and reused
 *   for all generation calls (low syscall overhead).
 * - Each generation produces a fixed-size chunk (see CHUNK) by issuing exactly
 *   two syscalls: `sendmsg()` (to supply IV and length) and `read()` (to fetch
 *   the keystream).
 * - Counter management (increment) is done in user space for determinism.
 *
 * @warning
 * IV (Counter) Encoding:
 *   This implementation builds the 128-bit AES-CTR IV by storing two 64-bit
 *   limbs in **little-endian** order (low limb at IV[0..7], high limb at
 *   IV[8..15]) and then incrementing the 128-bit value in little-endian form.
 *   This deviates from the big-endian counter semantics commonly assumed by
 *   RFC-style AES-CTR specifications. The stream remains secure (uniqueness
 *   of IVs is preserved) but is **not interoperable** with generic RFC-CTR
 *   streams. See `aes_ctr_prng.h` for a prominent header-level note.
 *
 * Threading:
 * - `tls_op_fd` is thread-local; each thread owns its kernel op-socket.
 * - The kernel cipher is re-entrant. No shared state in this TU is writable
 *   across threads.
 *
 * Error handling:
 * - Functions return `0` on success and `-1` on failure. When underlying
 *   syscalls fail, `-1` is returned; callers may consult `errno` as usual.
 */

// ============================================================================================
//  WHY THIS FILE EXISTS
//  --------------------------------------------------------------------------------------------
//  nwipe, a secure disk-wiping tool, needs cryptographically strong random data at multi-GB/s
//  in order to keep up with today’s NVMe and RAID arrays.  Users complained when the classic
//  user-space OpenSSL path plateaued around ~250 MB/s on modern CPUs.  The Linux kernel
//  already ships an extremely fast AES implementation (with transparent AES-NI / VAES / NEON
//  acceleration) that can be accessed from user space via the AF_ALG socket family.  By
//  delegating the heavy crypto to the kernel we gain all of the following *for free*:
//      • Perfectly tuned instruction selection per CPU (AES-NI, VAES, SVE, etc.)
//      • Full cache-line prefetch scheduling written by kernel crypto maintainers
//      • Zero-copy when the cipher runs in the same core
//      • Automatic fall-back to software if the CPU lacks AES-NI
//
//  DESIGN OVERVIEW (TL;DR)
//  ----------------------
//  ┌─ userspace ───────────────────────────────────────────────────────────────────────────────┐
//  │             +-------------------------------+                                             │
//  │   nwipe     | aes_ctr_state_t (256 bit)     |      (1) initialise, store key+counter      │
//  │             +-------------------------------+                                             │
//  │                     │                                                           ▲         │
//  │                     │ (2) sendmsg() + read() per fixed-size chunk               │         │
//  └─────────────────────┼───────────────────────────────────────────────────────────┤ kernel  │
//                        │                                                           │ space   │
//         persistent FD  ▼                                                           │         │
//                 ┌──────────────────────┐                                           │         │
//                 │ AF_ALG op socket     │ (ctr(aes))                                │         │
//                 └──────────────────────┘                                           └─────────┘
//
//  Public ABI stability:
//  ---------------------
//  The fd is *not* part of the public state (preserves libnwipe ABI). A TU-local,
//  thread-local descriptor is used internally; multiple PRNG instances per thread
//  share the same op-socket as needed.
//
//  Safety / threading:
//  -------------------
//  • The kernel cipher is re-entrant. Thread-local fd avoids cross-thread hazards.
//  • Counter increments occur in user space; one aes_ctr_state_t per thread.
// ==============================================================================================

#include "aes_ctr_prng.h"     // public header (256-bit state, extern "C" API)
#include <sys/socket.h>       // socket(), bind(), accept(), sendmsg()
#include <linux/if_alg.h>     // AF_ALG constants and skcipher API
#include <unistd.h>           // read(), close()
#include <cstring>            // memcpy(), memset(), strcpy()
#include <array>              // std::array for control buffer

// ----------------------------------------------------------------------------------------------
//  CONFIGURABLE CHUNK SIZE
// ----------------------------------------------------------------------------------------------
//  The per-call output size (CHUNK) can be configured at compile time via
//  AES_CTR_PRNG_CHUNK_BYTES. Default is 128 KiB.
//  Example:
//      gcc -DAES_CTR_PRNG_CHUNK_BYTES="(64u*1024u)" ...
// ----------------------------------------------------------------------------------------------
#ifndef AES_CTR_PRNG_CHUNK_BYTES
#define AES_CTR_PRNG_CHUNK_BYTES (128u * 1024u)   // 128 KiB default
#endif

// ----------------------------------------------------------------------------------------------
//  GLOBAL 256-BIT KEY
// ----------------------------------------------------------------------------------------------
//  • Loaded from user-supplied seed in aes_ctr_prng_init().
//  • Intended to remain constant for the process lifetime (or until re-init).
//  • Exposed (non-static) so out-of-TU tests can assert correct key handling.
//
//  @note Consider zeroizing on shutdown to avoid key retention in core dumps.
// ----------------------------------------------------------------------------------------------
unsigned char global_key[32];

// ----------------------------------------------------------------------------------------------
//  THREAD-LOCAL OPERATION SOCKET  (one per nwipe thread)
// ----------------------------------------------------------------------------------------------
//  Portable TLS qualifier: C++11 `thread_local` or GCC/Clang `__thread` for C builds.
//
//  @invariant tls_op_fd == -1  ⇒  this thread has not opened the op-socket yet.
//             tls_op_fd >=  0  ⇒  valid AF_ALG operation socket for "ctr(aes)".
//
//  @thread_safety Thread-local; no synchronization required.
// ----------------------------------------------------------------------------------------------
#if defined(__cplusplus) && __cplusplus >= 201103L
    #define PRNG_THREAD_LOCAL thread_local
#else
    #define PRNG_THREAD_LOCAL __thread
#endif

PRNG_THREAD_LOCAL static int tls_op_fd = -1;   // -1 ⇒ not yet opened in this thread

// ----------------------------------------------------------------------------------------------
//  CONSTANTS / INTERNAL HELPERS
// ----------------------------------------------------------------------------------------------
namespace {

/**
 * @brief AES block size in bytes (by specification).
 */
constexpr std::size_t AES_BLOCK = 16u;

/**
 * @brief Fixed-size generation granularity per kernel call.
 * @details
 * Adjust at build time via AES_CTR_PRNG_CHUNK_BYTES to balance syscall
 * overhead vs. latency and memory traffic.
 * Typical values: 16 KiB (legacy default), 64 KiB, 128 KiB.
 */
constexpr std::size_t CHUNK = AES_CTR_PRNG_CHUNK_BYTES;

static_assert(CHUNK % AES_BLOCK == 0,
              "AES_CTR_PRNG_CHUNK_BYTES must be a multiple of AES_BLOCK (16 bytes)");

/// Number of AES-CTR blocks produced per CHUNK.
constexpr std::size_t BLOCKS_PER_CHUNK = CHUNK / AES_BLOCK;

/**
 * @brief Store a 64-bit integer in little-endian byte order.
 *
 * @param v   64-bit value.
 * @param buf Destination pointer; must point to at least 8 writable bytes.
 *
 * @note
 * This function enforces a little-endian layout regardless of host endianness.
 * For hot paths you may consider an optimized version using memcpy/bswap on
 * big-endian hosts instead of byte-wise stores.
 */
static inline void store64_le(uint64_t v, unsigned char *buf)
{
    for (int i = 0; i < 8; ++i)
        buf[i] = static_cast<unsigned char>(v >> (8 * i));
}

/**
 * @class ControlBuilder
 * @brief Helper to assemble `msghdr` and control messages for AF_ALG.
 *
 * @details
 * Builds the control payload for one `sendmsg()` call against an AF_ALG
 * skcipher operation socket:
 *  - Control message #1: `ALG_SET_OP = ALG_OP_ENCRYPT`
 *  - Control message #2: `ALG_SET_IV` with a 128-bit IV
 *  - Data iovec: points at the plaintext buffer (here: zero-bytes of length CHUNK)
 *
 * All data structures live on the stack; constructing this helper is O(1).
 *
 * @note
 * The kernel expects `ivlen` as a host-endian u32 followed by `iv` bytes.
 * "Network order not required" is intentional for AF_ALG skcipher IVs.
 */
class ControlBuilder {
public:
    /**
     * @param iv    128-bit IV (counter value), passed as 16 bytes.
     * @param plain Pointer to plaintext buffer (here: all-zero array).
     * @param len   Plaintext length in bytes; determines keystream length.
     */
    ControlBuilder(const unsigned char iv[16], void *plain, size_t len)
    {
        // ---------- Data iovec ----------
        iov_.iov_base = plain;
        iov_.iov_len  = len;

        // ---------- msghdr --------------
        msg_.msg_name       = nullptr;              // already bound via bind()
        msg_.msg_namelen    = 0;
        msg_.msg_iov        = &iov_;
        msg_.msg_iovlen     = 1;
        msg_.msg_control    = control_.data();
        msg_.msg_controllen = control_.size();
        msg_.msg_flags      = 0;

        // ---------- CMSG #1 : ALG_SET_OP = ENCRYPT ----------
        cmsghdr *c1 = CMSG_FIRSTHDR(&msg_);
        c1->cmsg_level = SOL_ALG;
        c1->cmsg_type  = ALG_SET_OP;
        c1->cmsg_len   = CMSG_LEN(sizeof(uint32_t));
        *reinterpret_cast<uint32_t*>(CMSG_DATA(c1)) = ALG_OP_ENCRYPT;

        // ---------- CMSG #2 : ALG_SET_IV ----------
        cmsghdr *c2 = CMSG_NXTHDR(&msg_, c1);
        c2->cmsg_level = SOL_ALG;
        c2->cmsg_type  = ALG_SET_IV;
        c2->cmsg_len   = CMSG_LEN(sizeof(uint32_t) + 16);
        uint32_t ivlen = 16;                                   // host endian; not network order
        std::memcpy(CMSG_DATA(c2), &ivlen, sizeof(ivlen));
        std::memcpy(CMSG_DATA(c2) + sizeof(ivlen), iv, 16);
    }

    /// @return Prepared msghdr suitable for `sendmsg()`.
    struct msghdr *msg() { return &msg_; }

private:
    // Control buffer sufficient for both control messages.
    std::array<char,
               CMSG_SPACE(sizeof(uint32_t)) +
               CMSG_SPACE(sizeof(uint32_t) + 16)> control_{};
    struct msghdr msg_{};
    struct iovec  iov_{};
};

/**
 * @brief Open a "ctr(aes)" skcipher operation socket via AF_ALG.
 *
 * @details
 * Performs the `socket()` → `bind()` → `setsockopt(ALG_SET_KEY)` → `accept()`
 * sequence. The returned fd is the operation socket used for `sendmsg()`+`read()`.
 *
 * @param key  AES key (32 bytes for AES-256).
 * @return Operation socket fd (>= 0) on success, or -1 on failure.
 *
 * @warning
 * This function does not set `FD_CLOEXEC` nor handle `SIGPIPE`. Consider using
 * `SOCK_CLOEXEC` on `socket()` and `accept4()` and `MSG_NOSIGNAL` on `sendmsg()`
 * in hardened builds.
 */
static int open_ctr_socket(const unsigned char key[32])
{
    // 1. Create transform socket (AF_ALG family).
    int tfm = ::socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfm < 0) return -1;

    // 2. Describe requested algorithm: type = "skcipher", name = "ctr(aes)".
    sockaddr_alg sa = {};
    sa.salg_family = AF_ALG;
    std::strcpy(reinterpret_cast<char*>(sa.salg_type),  "skcipher");
    std::strcpy(reinterpret_cast<char*>(sa.salg_name),  "ctr(aes)");

    if (::bind(tfm, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(tfm); return -1;
    }

    // 3. Upload 256-bit key.
    if (::setsockopt(tfm, SOL_ALG, ALG_SET_KEY, key, 32) < 0) {
        ::close(tfm); return -1;
    }

    // 4. Accept operation socket — the fd we will use for sendmsg/read.
    int op = ::accept(tfm, nullptr, nullptr);
    ::close(tfm);      // transform socket no longer needed
    return op;         // may be -1 on error
}

/**
 * @brief Increment a 128-bit little-endian counter by @p n AES blocks.
 *
 * @details
 * The counter is represented as two 64-bit little-endian limbs in state->s[0..1].
 * The increment is performed modulo 2^128 with carry propagation from low to high.
 *
 * @param st PRNG state with s[0]=lo, s[1]=hi limbs.
 * @param n  Number of AES blocks to add.
 *
 * @note
 * This is **little-endian** counter arithmetic; see the big file-level warning
 * about non-RFC CTR semantics.
 */
static void ctr_add(aes_ctr_state_t *st, uint64_t n)
{
    uint64_t old = st->s[0];
    st->s[0] += n;
    if (st->s[0] < old) ++st->s[1];   // handle carry
}

} // namespace (anonymous)

// =================================================================================================
//  PUBLIC C API IMPLEMENTATION
// =================================================================================================
extern "C" {

/**
 * @brief Initialize PRNG state and lazily open the per-thread AF_ALG socket.
 *
 * @param[out] state       Pointer to PRNG state (must be non-null).
 * @param[in]  init_key    Seed as an array of unsigned long; must provide >= 32 bytes.
 * @param[in]  key_length  Number of `unsigned long` words in @p init_key.
 *
 * @retval 0  Success.
 * @retval -1 Invalid parameters or AF_ALG setup failure.
 *
 * @details
 * - Zeroes the entire state and copies the first 128 bits of the seed into the
 *   128-bit counter (little-endian limb order).
 * - Saves the first 256 bits as the AES-256 key in @c global_key.
 * - Opens the AF_ALG operation socket for "ctr(aes)" on first call in this
 *   thread and stores the fd in thread-local storage.
 *
 * @warning
 * The chosen IV scheme is little-endian and not RFC-interoperable.
 * Do not mix with external AES-CTR generators expecting big-endian counters.
 */
int aes_ctr_prng_init(aes_ctr_state_t *state,
                      unsigned long    init_key[],
                      unsigned long    key_length)
{
    if (!state || !init_key || key_length * sizeof(unsigned long) < 32)
        return -1;

    // Zero entire state, then put seed[0..15] into counter (LE limbs).
    std::memset(state, 0, sizeof(*state));
    std::memcpy(state->s, init_key, sizeof(uint64_t) * 2);

    // Remember full AES-256 key (32 bytes) for possible re-opens.
    std::memcpy(global_key, init_key, 32);

    // Open per-thread socket on first call in this thread.
    if (tls_op_fd == -1) {
        tls_op_fd = open_ctr_socket(global_key);
        if (tls_op_fd < 0) return -1;
    }
    return 0;
}

/**
 * @brief Produce exactly CHUNK bytes of keystream into @p bufpos.
 *
 * @param[in]  state   PRNG state (counter source).
 * @param[out] bufpos  Destination buffer; must hold at least CHUNK bytes.
 *
 * @retval 0  Success (CHUNK bytes written).
 * @retval -1 Parameter error or syscall failure.
 *
 * @details
 * Sequence per call:
 *  1. Assemble a 128-bit IV by storing s[0] (low) and s[1] (high) as
 *     little-endian 64-bit words into a 16-byte buffer.
 *  2. Build the AF_ALG control message (ALG_SET_OP=ENCRYPT, ALG_SET_IV=IV)
 *     and data iovec pointing to a static all-zero plaintext of length CHUNK.
 *  3. `sendmsg()` the request and `read()` back exactly CHUNK bytes of
 *     ciphertext — which, because plaintext is zero, equals the keystream.
 *  4. Increment the 128-bit counter by `BLOCKS_PER_CHUNK`.
 *
 * @note
 * The zero-plaintext buffer is static and zero-initialized once; the kernel
 * will not read uninitialized memory. Using zero plaintext is standard for
 * obtaining the raw AES-CTR keystream.
 */
int aes_ctr_prng_genrand_128k_to_buf(aes_ctr_state_t *state,
                                    unsigned char   *bufpos)
{
    if (!state || !bufpos || tls_op_fd < 0)
        return -1;

    // --- Construct 128-bit IV from counter (little-endian limbs) -------------
    unsigned char iv[16];
    store64_le(state->s[0], iv);       // little-endian low limb
    store64_le(state->s[1], iv + 8);   // little-endian high limb

    // --- Build msghdr ---------------------------------------------------------
    static unsigned char zeros[CHUNK] = {0};   // static → zero-initialised once
    ControlBuilder ctl(iv, zeros, CHUNK);

    // --- sendmsg() + read() ---------------------------------------------------
    if (::sendmsg(tls_op_fd, ctl.msg(), 0) != (ssize_t)CHUNK) return -1;
    if (::read   (tls_op_fd, bufpos, CHUNK) != (ssize_t)CHUNK) return -1;

    // --- Advance counter ------------------------------------------------------
    ctr_add(state, BLOCKS_PER_CHUNK);
    return 0;
}

/**
 * @brief Optional cleanup helper (explicitly closes the per-thread op-socket).
 *
 * @retval 0 Always succeeds.
 *
 * @details
 * The kernel will close FDs at process exit, but explicit shutdown helps
 * test harnesses and avoids keeping descriptors alive across exec().
 * Consider zeroizing @c global_key here for defense-in-depth.
 */
int aes_ctr_prng_shutdown(void)
{
    if (tls_op_fd >= 0) {
        ::close(tls_op_fd);
        tls_op_fd = -1;
    }
    return 0;
}

} // extern "C"

