#ifndef PASS_INTERNAL_H_
#define PASS_INTERNAL_H_

#include <stdint.h>
#include <stdlib.h> /* posix_memalign, malloc, free */
#include <string.h> /* memset, memcpy, memcmp */
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "logging.h"
#include "gui.h"

ssize_t nwipe_write_with_retry( nwipe_context_t* c, int fd, const void* buf, size_t count );
ssize_t nwipe_pwrite_with_retry( nwipe_context_t* c, int fd, const void* buf, size_t count, off64_t offset );
ssize_t nwipe_read_with_retry( nwipe_context_t* c, int fd, void* buf, size_t count );

int nwipe_fdatasync( nwipe_context_t* c, const char* f );

size_t nwipe_effective_io_blocksize( const nwipe_context_t* c );
void* nwipe_alloc_io_buffer( const nwipe_context_t* c, size_t size, int clear, const char* label );
int nwipe_compute_sync_rate_for_device( const nwipe_context_t* c, size_t io_blocksize );
void nwipe_update_bytes_erased( nwipe_context_t* c, u64 z, u64 bs, int synced );

int nwipe_static_forward_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );
int nwipe_static_forward_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );
int nwipe_random_forward_pass( NWIPE_METHOD_SIGNATURE );
int nwipe_random_forward_verify( NWIPE_METHOD_SIGNATURE );

#endif /* PASS_INTERNAL_H_ */
