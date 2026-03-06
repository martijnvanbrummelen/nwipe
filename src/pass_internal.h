/*
 *  pass_internal.h: Internal pass-related I/O routines.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  Modifications to original dwipe Copyright Andy Beverley <andy@andybev.com>
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

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
ssize_t nwipe_pread_with_retry( nwipe_context_t* c, int fd, void* buf, size_t count, off64_t offset );
int nwipe_fdatasync( nwipe_context_t* c, const char* f );

size_t nwipe_effective_io_blocksize( const nwipe_context_t* c );
void* nwipe_alloc_io_buffer( const nwipe_context_t* c, size_t size, int clear, const char* label );
int nwipe_compute_sync_rate_for_device( const nwipe_context_t* c, size_t io_blocksize );
void nwipe_update_bytes_erased( nwipe_context_t* c, u64 z, u64 bs, int synced );
int nwipe_prng_is_active( const char* buf, size_t blocksize );

int nwipe_static_forward_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );
int nwipe_static_reverse_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );
int nwipe_static_forward_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );
int nwipe_static_reverse_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern );

int nwipe_random_forward_pass( NWIPE_METHOD_SIGNATURE );
int nwipe_random_reverse_pass( NWIPE_METHOD_SIGNATURE );
int nwipe_random_forward_verify( NWIPE_METHOD_SIGNATURE );
int nwipe_random_reverse_verify( NWIPE_METHOD_SIGNATURE );

#endif /* PASS_INTERNAL_H_ */
