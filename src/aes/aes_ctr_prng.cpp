// ============================================================================================
//  aes_ctr_prng.cpp  —  High‑Throughput AES‑256‑CTR PRNG for nwipe
//  --------------------------------------------------------------------------------------------
//  WHY THIS FILE EXISTS
//  --------------------
//  nwipe, a secure disk‑wiping tool, needs cryptographically strong random data at multi‑GB/s
//  in order to keep up with today’s NVMe and RAID arrays.  Users complained when the classic
//  user‑space OpenSSL path plateaued around ~250 MB/s on modern CPUs.  The Linux kernel
//  already ships an extremely fast AES implementation (with transparent AES‑NI / VAES / NEON
//  acceleration) that can be accessed from user space via the AF_ALG socket family.  By
//  delegating the heavy crypto to the kernel we gain all of the following *for free*:
//      • Perfectly tuned instruction selection per CPU (AES‑NI, VAES, SVE, etc.)
//      • Full cache‑line prefetch scheduling written by kernel crypto maintainers
//      • Zero‑copy when the cipher runs in the same core
//      • Automatic fall‑back to software if the CPU lacks AES‑NI
//
//  DESIGN OVERVIEW (TL;DR)
//  ----------------------
//  ┌─ userspace ───────────────────────────────────────────────────────────────────────────────┐
//  │             +-------------------------------+                                             │
//  │   nwipe     | aes_ctr_state_t (256 bit)     |      (1) initialise, store key+counter       │
//  │             +-------------------------------+                                             │
//  │                     │                                                           ▲         │
//  │                     │ (2) sendmsg() + read() per 16 KiB chunk                   │         │
//  └─────────────────────┼───────────────────────────────────────────────────────────┤ kernel  │
//                        │                                                           │ space   │
//         persistent FD  ▼                                                           │         │
//                ┌──────────────────────┐                                           │         │
//                │ AF_ALG op socket     │ (ctr(aes))                                │         │
//                └──────────────────────┘                                           └─────────┘
//
//  Key idea: **The socket is opened once** (in aes_ctr_prng_init) and kept open for the entire
//  lifetime of the process.  Each PRNG call only needs two inexpensive syscalls:
//      • sendmsg()  — tells the kernel the IV (i.e. current counter) + plaintext length
//      • read()     — returns the ciphertext (= keystream) into our output buffer
//  That is less overhead than memcpy() at these block sizes.
//
//  PUBLIC STATE (aes_ctr_state_t) REMAINS 256 bit
//  ---------------------------------------------
//  We consciously do *NOT* fold the file descriptor into the public state because that would
//  destroy ABI compatibility with libnwipe.  Instead, g_op_fd below is TU‑local (file‑static).
//  Multiple independent PRNG instances *share* this socket — fine for nwipe’s single thread.
//
//  SAFETY / THREADING
//  ------------------
//  • The kernel cipher itself is re-entrant; thread-local FD guarantees call-site safety.
//  • Counter increment (`ctr_add`) is done entirely in user space; no atomic ops needed because
//    each thread owns its own `aes_ctr_state_t` instance.
//
// ==============================================================================================

#include "aes_ctr_prng.h"     // public header (256-bit state, extern "C" API)
#include <sys/socket.h>       // socket(), bind(), accept(), sendmsg()
#include <linux/if_alg.h>     // AF_ALG constants
#include <unistd.h>           // read(), close()
#include <cstring>            // memcpy(), memset(), strcpy()
#include <array>              // std::array for control buffer

// ----------------------------------------------------------------------------------------------
//  GLOBAL 256-BIT KEY
//  ----------------------------------------------------------------------------------------------
//  • Loaded from the user-supplied seed in aes_ctr_prng_init().
//  • Constant for the lifetime of the process.
//  • Exposed (non-static) so unit tests in another TU can verify it.
unsigned char global_key[32];

// ----------------------------------------------------------------------------------------------
//  THREAD-LOCAL OPERATION SOCKET  (one per nwipe thread)
//  ----------------------------------------------------------------------------------------------
//  Portable TLS qualifier: C++11 `thread_local` or GCC/Clang `__thread` for C compilation.
#if defined(__cplusplus) && __cplusplus >= 201103L
    #define PRNG_THREAD_LOCAL thread_local
#else
    #define PRNG_THREAD_LOCAL __thread
#endif

PRNG_THREAD_LOCAL static int tls_op_fd = -1;   // -1 ⇒ not yet opened in this thread

// ----------------------------------------------------------------------------------------------
//  CONSTANTS
// ----------------------------------------------------------------------------------------------
namespace {

constexpr std::size_t CHUNK            = 1u << 14;   // 16 KiB produced per kernel call
constexpr std::size_t AES_BLOCK        = 16u;        // fixed by AES spec
constexpr std::size_t BLOCKS_PER_CHUNK = CHUNK / AES_BLOCK;  // 1024 CTR blocks

// Little-endian 64-bit store helper.
static inline void store64_le(uint64_t v, unsigned char *buf)
{
    for (int i = 0; i < 8; ++i)
        buf[i] = static_cast<unsigned char>(v >> (8 * i));
}

// ==============================================================================================
//  ControlBuilder — assembles the msghdr + control messages for AF_ALG
// ==============================================================================================
//  • Control message #1  ALG_SET_OP = ALG_OP_ENCRYPT
//  • Control message #2  ALG_SET_IV = 128-bit IV (our counter)
//  • Data iovec          points to `plain` (all-zero buffer, length CHUNK)
//
//  Everything lives on the stack, so constructing ControlBuilder is basically free.
//
class ControlBuilder {
public:
    ControlBuilder(const unsigned char iv[16], void *plain, size_t len)
    {
        // ---------- Data iovec ----------
        iov_.iov_base = plain;
        iov_.iov_len  = len;

        // ---------- msghdr --------------
        msg_.msg_name       = nullptr;              // already bound
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
        uint32_t ivlen = 16;                                   // network order not required
        std::memcpy(CMSG_DATA(c2), &ivlen, sizeof(ivlen));
        std::memcpy(CMSG_DATA(c2) + sizeof(ivlen), iv, 16);
    }

    struct msghdr *msg() { return &msg_; }

private:
    // Enough space for both control messages.
    std::array<char,
               CMSG_SPACE(sizeof(uint32_t)) +
               CMSG_SPACE(sizeof(uint32_t) + 16)> control_{};
    struct msghdr msg_{};
    struct iovec  iov_{};
};

// ----------------------------------------------------------------------------------------------
//  open_ctr_socket() — perform socket → bind → setsockopt → accept sequence
// ----------------------------------------------------------------------------------------------
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

// Increment 128-bit counter by n blocks (little-endian addition).
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

// -----------------------------------------------------------------------------------------------
//  aes_ctr_prng_init()
//  • Clears state, copies first 128 bits of seed into counter, saves 256-bit key globally.
//  • Lazily opens thread-local AF_ALG socket.
// -----------------------------------------------------------------------------------------------
int aes_ctr_prng_init(aes_ctr_state_t *state,
                      unsigned long    init_key[],
                      unsigned long    key_length)
{
    if (!state || !init_key || key_length * sizeof(unsigned long) < 32)
        return -1;

    // Zero entire state, then put seed[0..15] into counter.
    std::memset(state, 0, sizeof(*state));
    std::memcpy(state->s, init_key, sizeof(uint64_t) * 2);

    // Remember full key for possible re-opens.
    std::memcpy(global_key, init_key, 32);

    // Open per-thread socket on first call in this thread.
    if (tls_op_fd == -1) {
        tls_op_fd = open_ctr_socket(global_key);
        if (tls_op_fd < 0) return -1;
    }
    return 0;
}

// -----------------------------------------------------------------------------------------------
//  aes_ctr_prng_genrand_16k_to_buf()
//  • Hot path: produces exactly 16 KiB of keystream in `bufpos`.
//  • Only two syscalls thanks to persistent thread-local socket.
// -----------------------------------------------------------------------------------------------
int aes_ctr_prng_genrand_16k_to_buf(aes_ctr_state_t *state,
                                    unsigned char   *bufpos)
{
    if (!state || !bufpos || tls_op_fd < 0)
        return -1;

    // --- Construct 128-bit IV from counter ------------------------------------
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

// -----------------------------------------------------------------------------------------------
//  aes_ctr_prng_shutdown()
//  • Optional cleanup helper (kernel will close FDs at process exit anyway).
// -----------------------------------------------------------------------------------------------
int aes_ctr_prng_shutdown(void)
{
    if (tls_op_fd >= 0) {
        ::close(tls_op_fd);
        tls_op_fd = -1;
    }
    return 0;
}

} // extern \"C\"

