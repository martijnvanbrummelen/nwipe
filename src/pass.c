/*
 *  pass.c: Routines that read and write patterns to block devices.
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

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdlib.h> /* posix_memalign, malloc, free */
#include <string.h> /* memset, memcpy, memcmp */
#include <errno.h>
#include <limits.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "pass.h"
#include "logging.h"
#include "gui.h"

/*
 * Tunable sizes for the wiping / verification I/O path.
 *
 * NWIPE_BUFFER_SIZE:
 *   - Size of the generic scratch buffer used by passes.
 *   - Default is 16 MiB, which is a good compromise between memory usage and
 *     reducing syscall count.
 *
 * NWIPE_IO_BLOCKSIZE:
 *   - Target size of individual read()/write() operations.
 *   - Default is 4 MiB, so each syscall moves a lot of data instead of only
 *     4 KiB, drastically reducing syscall overhead.
 *
 * Notes:
 *   - We do NOT depend on O_DIRECT here; all code works fine with normal,
 *     buffered I/O.
 *   - But all I/O buffers are allocated aligned to the device block size so
 *     that the same code also works with O_DIRECT when the device is opened
 *     with it.
 */
#ifndef NWIPE_BUFFER_SIZE
#define NWIPE_BUFFER_SIZE ( 16 * 1024 * 1024UL ) /* 16 MiB generic buffer */
#endif

#ifndef NWIPE_IO_BLOCKSIZE
#define NWIPE_IO_BLOCKSIZE ( 4 * 1024 * 1024UL ) /* 4 MiB I/O block */
#endif

/*
 * Compute the effective I/O block size for a given device:
 *
 * - Must be at least the device's reported st_blksize (usually 4 KiB).
 * - Starts from NWIPE_IO_BLOCKSIZE (4 MiB by default) and adjusts.
 * - Rounded down to a multiple of st_blksize so it is compatible with
 *   O_DIRECT alignment rules.
 * - Never exceeds the device size.
 */
static size_t nwipe_effective_io_blocksize( const nwipe_context_t* c )
{
    size_t bs = (size_t) c->device_stat.st_blksize;

    if( bs == 0 )
    {
        /* Should not happen for normal block devices; use a sane default. */
        bs = 4096;
    }

    size_t io_bs = (size_t) NWIPE_IO_BLOCKSIZE;

    if( io_bs < bs )
    {
        io_bs = bs;
    }

    /* Round down to a multiple of the device block size. */
    if( io_bs % bs != 0 )
    {
        io_bs -= ( io_bs % bs );
    }

    if( io_bs == 0 )
    {
        io_bs = bs;
    }

    if( (u64) io_bs > c->device_size )
    {
        io_bs = (size_t) c->device_size;
    }

    return io_bs;
}

/*
 * Allocate an I/O buffer aligned to the device block size.
 *
 * This is done with posix_memalign() so that the buffer can safely be used
 * with O_DIRECT if the device was opened with it. The same allocation is also
 * perfectly fine for normal buffered I/O.
 *
 * Parameters:
 *   c      - device context (for block size / logging)
 *   size   - number of bytes to allocate
 *   clear  - if non-zero, the buffer is zeroed after allocation
 *   label  - short description for logging
 */
static void* nwipe_alloc_io_buffer( const nwipe_context_t* c, size_t size, int clear, const char* label )
{
    size_t align = (size_t) c->device_stat.st_blksize;
    if( align < 512 )
    {
        /* O_DIRECT usually requires at least 512-byte alignment. */
        align = 512;
    }

    void* ptr = NULL;
    int rc = posix_memalign( &ptr, align, size );
    if( rc != 0 || ptr == NULL )
    {
        nwipe_log( NWIPE_LOG_FATAL,
                   "%s: posix_memalign failed for %s (size=%zu, align=%zu, rc=%d).",
                   __FUNCTION__,
                   label,
                   size,
                   align,
                   rc );
        return NULL;
    }

    if( clear )
    {
        memset( ptr, 0, size );
    }

    return ptr;
}

/*
 * Compute the per-write sync rate for a given device and I/O block size.
 *
 * Historically, --sync=N meant "fdatasync() every N * st_blksize bytes".
 * Now that we use large I/O blocks, we convert that into "sync every K writes",
 * where each write is of size io_blocksize.
 *
 * For O_DIRECT we return 0 because write() already reports I/O errors directly.
 */
static int nwipe_compute_sync_rate_for_device( const nwipe_context_t* c, size_t io_blocksize )
{
    int syncRate = nwipe_options.sync;

    /* No periodic sync in direct I/O mode. */
    if( nwipe_options.io_mode == NWIPE_IO_MODE_DIRECT )
        return 0;

    if( syncRate <= 0 )
        return 0;

    if( io_blocksize == 0 )
        return 0;

    /* Old semantics: bytes between syncs = sync * st_blksize. */
    unsigned long long bytes_between_sync =
        (unsigned long long) syncRate * (unsigned long long) c->device_stat.st_blksize;

    if( bytes_between_sync == 0 )
        return 0;

    /* Convert to "writes between syncs". */
    unsigned long long tmp = bytes_between_sync / (unsigned long long) io_blocksize;

    if( tmp == 0 )
        return 1; /* at least every write */

    if( tmp > (unsigned long long) INT_MAX )
        return INT_MAX; /* just in case */

    return (int) tmp;
}

/*
 * nwipe_random_verify
 *
 * Verifies that a random pass was correctly written to the device.
 * The PRNG is re-seeded with the stored seed, and the same random byte
 * stream is generated again and compared against what is read from disk.
 *
 * This version uses large I/O blocks (e.g. 4 MiB) instead of tiny
 * st_blksize-sized chunks to reduce syscall overhead and speed up verification.
 */
int nwipe_random_verify( nwipe_context_t* c )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    char* b; /* input buffer from device */
    char* d; /* pattern buffer generated by PRNG */
    u64 z = c->device_size; /* bytes remaining in this pass */

    if( c->prng_seed.s == NULL )
    {
        nwipe_log( NWIPE_LOG_SANITY, "Null seed pointer." );
        return -1;
    }

    if( c->prng_seed.length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "The entropy length member is %i.", c->prng_seed.length );
        return -1;
    }

    io_blocksize = nwipe_effective_io_blocksize( c );

    /* Allocate I/O buffers of the chosen block size (aligned for possible O_DIRECT). */
    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "random_verify input buffer" );
    if( !b )
        return -1;

    d = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "random_verify pattern buffer" );
    if( !d )
    {
        free( b );
        return -1;
    }

    /* Rewind device to the beginning. */
    offset = lseek( c->device_fd, 0, SEEK_SET );
    c->pass_done = 0;

    if( offset == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
        free( b );
        free( d );
        return -1;
    }

    if( offset != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "lseek() returned a bogus offset on '%s'.", c->device_name );
        free( b );
        free( d );
        return -1;
    }

    /* Ensure all previous writes are on disk before we verify. */
    c->sync_status = 1;
    r = fdatasync( c->device_fd );
    c->sync_status = 0;

    if( r != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "fdatasync" );
        nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
        c->fsyncdata_errors++;
    }

    /* Reseed the PRNG so it produces the same stream as during the pass. */
    c->prng->init( &c->prng_state, &c->prng_seed );

    while( z > 0 )
    {
        if( z >= (u64) io_blocksize )
        {
            blocksize = io_blocksize;
        }
        else
        {
            blocksize = (size_t) z;

            /* Seatbelt: device size should normally be a multiple of st_blksize. */
            if( (u64) c->device_stat.st_blksize > z )
            {
                nwipe_log( NWIPE_LOG_WARNING,
                           "%s: The size of '%s' is not a multiple of its block size %i.",
                           __FUNCTION__,
                           c->device_name,
                           c->device_stat.st_blksize );
            }
        }

        /* Generate expected random data into pattern buffer. */
        c->prng->read( &c->prng_state, d, blocksize );

        /* Read data from device. */
        r = (int) read( c->device_fd, b, blocksize );
        if( r < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "read" );
            nwipe_log( NWIPE_LOG_ERROR, "Unable to read from '%s'.", c->device_name );
            free( b );
            free( d );
            return -1;
        }

        if( r != (int) blocksize )
        {
            /*
             * Partial reads are treated as warnings and verification errors.
             * We keep the semantics of the original code: increment error
             * counter and try to skip forward by the missing amount.
             */
            int s = (int) blocksize - r;

            nwipe_log(
                NWIPE_LOG_WARNING, "%s: Partial read from '%s', %i bytes short.", __FUNCTION__, c->device_name, s );

            c->verify_errors += 1;

            offset = lseek( c->device_fd, s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial read.", c->device_name );
                free( b );
                free( d );
                return -1;
            }
        }

        /* Compare the bytes we actually read (r) against the generated pattern. */
        if( r > 0 && memcmp( b, d, (size_t) r ) != 0 )
        {
            c->verify_errors += 1;
        }

        z -= (u64) r;
        c->pass_done += r;
        c->round_done += r;

        pthread_testcancel();

    } /* while bytes remaining */

    free( b );
    free( d );

    return 0;

} /* nwipe_random_verify */

/*
 * nwipe_random_pass
 *
 * Writes a random pattern to the device using the configured PRNG.
 *
 * This version uses:
 *   - a generic buffer (default 16 MiB), zero-initialized to avoid leaking
 *     previous memory content in case of PRNG bugs, and
 *   - large write() calls (default 4 MiB per syscall) instead of tiny
 *     st_blksize-sized writes.
 *
 * The PRNG interface (init/read) and the integrity check that verifies that
 * the PRNG produced non-zero data for the first block are kept intact.
 */
int nwipe_random_pass( NWIPE_METHOD_SIGNATURE )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    size_t bufsize;
    off64_t offset;
    char* b;
    u64 z = c->device_size; /* bytes remaining */

    int syncRate = nwipe_options.sync;

    /* For direct I/O we do not need periodic fdatasync(), I/O errors are detected
     * at write() time. Keep sync for cached I/O only. */
    if( c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        syncRate = 0;
    }

    /* Select effective I/O block size (e.g. 4 MiB, never smaller than st_blksize). */
    io_blocksize = nwipe_effective_io_blocksize( c );

    /* Compute the per-write sync rate based on io_blocksize and old semantics. */
    syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );

    int i = 0;
    int idx;

    if( c->prng_seed.s == NULL )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: Null seed pointer." );
        return -1;
    }

    if( c->prng_seed.length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: The entropy length member is %i.", c->prng_seed.length );
        return -1;
    }

    /*
     * Allocate a generic 16 MiB buffer (by default) that is used as the
     * scratch area for random data. We will only fill and write "blocksize"
     * bytes per iteration, which is at most io_blocksize.
     */
    bufsize = (size_t) NWIPE_BUFFER_SIZE;
    if( bufsize < io_blocksize )
        bufsize = io_blocksize;

    b = (char*) nwipe_alloc_io_buffer( c, bufsize, 1, "random_pass output buffer" );
    if( !b )
        return -1;

    /* Seed the PRNG for this pass. */
    c->prng->init( &c->prng_state, &c->prng_seed );

    /* Rewind device. */
    offset = lseek( c->device_fd, 0, SEEK_SET );
    c->pass_done = 0;

    if( offset == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
        free( b );
        return -1;
    }

    if( offset != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: lseek() returned a bogus offset on '%s'.", c->device_name );
        free( b );
        return -1;
    }

    while( z > 0 )
    {
        /*
         * Use large writes of size "io_blocksize" as long as enough data is
         * left. The final iteration may use a smaller block if the device size
         * is not an exact multiple.
         */
        if( z >= (u64) io_blocksize )
        {
            blocksize = io_blocksize;
        }
        else
        {
            blocksize = (size_t) z;

            if( (u64) c->device_stat.st_blksize > z )
            {
                nwipe_log( NWIPE_LOG_WARNING,
                           "%s: The size of '%s' is not a multiple of its block size %i.",
                           __FUNCTION__,
                           c->device_name,
                           c->device_stat.st_blksize );
            }
        }

        /* Ask the PRNG to fill "blocksize" bytes into the output buffer. */
        c->prng->read( &c->prng_state, b, blocksize );

        /*
         * For the first block only, verify that the PRNG actually wrote
         * something non-zero into the buffer. This preserves the original
         * sanity check but works even if the I/O block size is larger than
         * st_blksize.
         */
        if( z == c->device_size )
        {
            size_t check_len = (size_t) c->device_stat.st_blksize;
            if( check_len > blocksize )
                check_len = blocksize;

            idx = (int) check_len - 1;

            while( idx >= 0 )
            {
                if( b[idx] != 0 )
                {
                    nwipe_log( NWIPE_LOG_NOTICE, "prng stream is active" );
                    break;
                }
                idx--;
            }
            if( idx < 0 )
            {
                nwipe_log( NWIPE_LOG_FATAL, "ERROR, prng wrote nothing to the buffer" );
                if( c->bytes_erased < ( c->device_size - z ) )
                {
                    c->bytes_erased = c->device_size - z;
                }
                free( b );
                return -1;
            }
        }

        /* Write the generated random data to the device. */
        r = (int) write( c->device_fd, b, blocksize );

        if( r < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "write" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to write to '%s'.", c->device_name );
            if( c->bytes_erased < ( c->device_size - z ) )
            {
                c->bytes_erased = c->device_size - z;
            }
            free( b );
            return -1;
        }

        if( r != (int) blocksize )
        {
            /*
             * Partial writes are rare on block devices, but they can happen.
             * We keep the original behavior: count the missing bytes as
             * errors and try to skip forward by that amount.
             */
            int s = (int) blocksize - r;

            c->pass_errors += s;

            nwipe_log( NWIPE_LOG_WARNING, "Partial write on '%s', %i bytes short.", c->device_name, s );

            offset = lseek( c->device_fd, s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial write.", c->device_name );
                if( c->bytes_erased < ( c->device_size - z ) )
                {
                    c->bytes_erased = c->device_size - z;
                }
                free( b );
                return -1;
            }
        }

        z -= (u64) r;
        c->pass_done += r;
        c->round_done += r;

        /* Periodic fdatasync after 'syncRate' writes, if configured. */
        if( syncRate > 0 )
        {
            i++;

            if( i >= syncRate )
            {
                c->sync_status = 1;
                r = fdatasync( c->device_fd );
                c->sync_status = 0;

                if( r != 0 )
                {
                    nwipe_perror( errno, __FUNCTION__, "fdatasync" );
                    nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
                    nwipe_log( NWIPE_LOG_WARNING, "Wrote %llu bytes on '%s'.", c->pass_done, c->device_name );
                    c->fsyncdata_errors++;
                    free( b );
                    if( c->bytes_erased < ( c->device_size - z ) )
                    {
                        c->bytes_erased = c->device_size - z;
                    }
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

        /* Track how much of the device has been successfully erased so far. */
        if( c->bytes_erased < ( c->device_size - z ) )
        {
            c->bytes_erased = c->device_size - z;
        }

    } /* /remaining bytes */

    free( b );

    /* Final sync at end of pass. */
    c->sync_status = 1;
    r = fdatasync( c->device_fd );
    c->sync_status = 0;

    if( r != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "fdatasync" );
        nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
        c->fsyncdata_errors++;

        /*
         * Keep the original semantics: adjust bytes_erased based on the last
         * known good block and fail the pass.
         */
        if( c->bytes_erased < ( c->device_size - z - blocksize ) )
        {
            c->bytes_erased = c->device_size - z - blocksize;
        }
        return -1;
    }

    return 0;

} /* nwipe_random_pass */

/*
 * nwipe_static_verify
 *
 * Verifies that a static pattern pass was correctly written to the device.
 *
 * We pre-build a pattern buffer that repeats the user-chosen pattern and
 * then, for each block we read from the device, compare it to the appropriate
 * "window" into that pattern buffer.
 *
 * This version uses large I/O blocks (e.g. 4 MiB) instead of tiny
 * st_blksize-sized chunks.
 */
int nwipe_static_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    char* b; /* read buffer */
    char* d; /* pre-built pattern buffer */
    char* q;
    int w = 0; /* window offset into pattern buffer */
    u64 z = c->device_size;

    if( pattern == NULL )
    {
        nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: Null entropy pointer." );
        return -1;
    }

    if( pattern->length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: The pattern length member is %i.", pattern->length );
        return -1;
    }

    io_blocksize = nwipe_effective_io_blocksize( c );

    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "static_verify input buffer" );
    if( !b )
        return -1;

    /*
     * Pattern buffer length:
     *   io_blocksize + pattern->length * 2
     * to ensure we can always take a contiguous window of size <= io_blocksize
     * starting at any offset w within [0, pattern->length).
     */
    d = (char*) nwipe_alloc_io_buffer( c, io_blocksize + pattern->length * 2, 0, "static_verify pattern buffer" );
    if( !d )
    {
        free( b );
        return -1;
    }

    for( q = d; q < d + io_blocksize + pattern->length; q += pattern->length )
    {
        memcpy( q, pattern->s, pattern->length );
    }

    /* Ensure all writes are flushed before verification. */
    c->sync_status = 1;
    r = fdatasync( c->device_fd );
    c->sync_status = 0;

    if( r != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "fdatasync" );
        nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
        c->fsyncdata_errors++;
    }

    /* Rewind. */
    offset = lseek( c->device_fd, 0, SEEK_SET );
    c->pass_done = 0;

    if( offset == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
        free( b );
        free( d );
        return -1;
    }

    if( offset != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: lseek() returned a bogus offset on '%s'.", c->device_name );
        free( b );
        free( d );
        return -1;
    }

    while( z > 0 )
    {
        if( z >= (u64) io_blocksize )
        {
            blocksize = io_blocksize;
        }
        else
        {
            blocksize = (size_t) z;

            if( (u64) c->device_stat.st_blksize > z )
            {
                nwipe_log( NWIPE_LOG_WARNING,
                           "%s: The size of '%s' is not a multiple of its block size %i.",
                           __FUNCTION__,
                           c->device_name,
                           c->device_stat.st_blksize );
            }
        }

        r = (int) read( c->device_fd, b, blocksize );
        if( r < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "read" );
            nwipe_log( NWIPE_LOG_ERROR, "Unable to read from '%s'.", c->device_name );
            free( b );
            free( d );
            return -1;
        }

        if( r == (int) blocksize )
        {
            /* Compare every byte in the buffer against the current pattern window. */
            if( memcmp( b, &d[w], (size_t) r ) != 0 )
            {
                c->verify_errors += 1;
            }
        }
        else
        {
            int s = (int) blocksize - r;

            c->verify_errors += 1;

            nwipe_log( NWIPE_LOG_WARNING, "Partial read on '%s', %i bytes short.", c->device_name, s );

            offset = lseek( c->device_fd, s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial read.", c->device_name );
                free( b );
                free( d );
                return -1;
            }
        }

        /*
         * Advance the pattern window by r bytes, modulo pattern->length.
         * This keeps the pattern alignment in sync with the device offset.
         */
        if( pattern->length > 0 && r > 0 )
        {
            size_t adv = (size_t) r % (size_t) pattern->length;
            w = (int) ( ( w + (int) adv ) % pattern->length );
        }

        z -= (u64) r;
        c->pass_done += r;
        c->round_done += r;

        pthread_testcancel();

    } /* while bytes remaining */

    free( b );
    free( d );

    return 0;

} /* nwipe_static_verify */

/*
 * nwipe_static_pass
 *
 * Writes a static pattern to the device.
 *
 * The pattern is repeated into a large buffer and then written in equally
 * large I/O blocks (e.g. 4 MiB). The "window" offset w keeps track of where
 * in the repeating pattern we are when moving across the device.
 */
int nwipe_static_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    size_t bufsize;
    off64_t offset;
    char* b;
    char* p;
    int w = 0; /* window offset into pattern */
    u64 z = c->device_size;

    int syncRate = nwipe_options.sync;
    /* For direct I/O we do not need periodic fdatasync(), I/O errors are detected
     * at write() time. Keep sync for cached I/O only. */
    if( c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        syncRate = 0;
    }

    int i = 0;

    if( pattern == NULL )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: Null pattern pointer." );
        return -1;
    }

    if( pattern->length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: The pattern length member is %i.", pattern->length );
        return -1;
    }

    io_blocksize = nwipe_effective_io_blocksize( c );

    /* Compute per-write sync rate (same semantics as random pass). */
    syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );

    /*
     * For static patterns we want enough buffer space to always have a
     * contiguous window of "io_blocksize" bytes available starting at any
     * offset w in [0, pattern->length). Using:
     *
     *   buffer_size >= io_blocksize + pattern->length * 2
     *
     * guarantees that we can wrap around the repeating pattern safely.
     * We also honour NWIPE_BUFFER_SIZE if it is larger, to keep the "generic
     * 16 MiB buffer" behavior.
     */
    bufsize = io_blocksize + pattern->length * 2;
    if( bufsize < (size_t) NWIPE_BUFFER_SIZE )
        bufsize = (size_t) NWIPE_BUFFER_SIZE;

    b = (char*) nwipe_alloc_io_buffer( c, bufsize, 0, "static_pass pattern buffer" );
    if( !b )
        return -1;

    for( p = b; p < b + bufsize; p += pattern->length )
    {
        memcpy( p, pattern->s, pattern->length );
    }

    /* Rewind device. */
    offset = lseek( c->device_fd, 0, SEEK_SET );
    c->pass_done = 0;

    if( offset == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
        free( b );
        return -1;
    }

    if( offset != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: lseek() returned a bogus offset on '%s'.", c->device_name );
        free( b );
        return -1;
    }

    while( z > 0 )
    {
        if( z >= (u64) io_blocksize )
        {
            blocksize = io_blocksize;
        }
        else
        {
            blocksize = (size_t) z;

            if( (u64) c->device_stat.st_blksize > z )
            {
                nwipe_log( NWIPE_LOG_WARNING,
                           "%s: The size of '%s' is not a multiple of its block size %i.",
                           __FUNCTION__,
                           c->device_name,
                           c->device_stat.st_blksize );
            }
        }

        /*
         * Write "blocksize" bytes starting at offset w in the repeating
         * pattern buffer. Because we filled the entire buffer with the
         * pattern (and made it large enough), &b[w] is always valid.
         */
        r = (int) write( c->device_fd, &b[w], blocksize );
        if( r < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "write" );
            nwipe_log( NWIPE_LOG_FATAL, "Unable to write to '%s'.", c->device_name );
            if( c->bytes_erased < ( c->device_size - z ) )
            {
                c->bytes_erased = c->device_size - z;
            }
            free( b );
            return -1;
        }

        if( r != (int) blocksize )
        {
            int s = (int) blocksize - r;

            c->pass_errors += s;

            nwipe_log( NWIPE_LOG_WARNING, "Partial write on '%s', %i bytes short.", c->device_name, s );

            offset = lseek( c->device_fd, s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial write.", c->device_name );
                if( c->bytes_erased < ( c->device_size - z ) )
                {
                    c->bytes_erased = c->device_size - z;
                }
                free( b );
                return -1;
            }
        }

        /*
         * Advance the pattern window by r bytes (not blocksize; we use the
         * actual number of bytes written) modulo pattern length.
         */
        if( pattern->length > 0 && r > 0 )
        {
            size_t adv = (size_t) r % (size_t) pattern->length;
            w = (int) ( ( w + (int) adv ) % pattern->length );
        }

        z -= (u64) r;
        c->pass_done += r;
        c->round_done += r;

        /* Periodic sync if requested. */
        if( syncRate > 0 )
        {
            i++;

            if( i >= syncRate )
            {
                c->sync_status = 1;
                r = fdatasync( c->device_fd );
                c->sync_status = 0;

                if( r != 0 )
                {
                    nwipe_perror( errno, __FUNCTION__, "fdatasync" );
                    nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
                    nwipe_log( NWIPE_LOG_WARNING, "Wrote %llu bytes on '%s'.", c->pass_done, c->device_name );
                    c->fsyncdata_errors++;
                    free( b );
                    if( c->bytes_erased < ( c->device_size - z ) )
                    {
                        c->bytes_erased = c->device_size - z;
                    }
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

        if( c->bytes_erased < ( c->device_size - z ) )
        {
            c->bytes_erased = c->device_size - z;
        }

    } /* /remaining bytes */

    /* Final sync at end of pass. */
    c->sync_status = 1;
    r = fdatasync( c->device_fd );
    c->sync_status = 0;

    if( r != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "fdatasync" );
        nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
        c->fsyncdata_errors++;
        if( c->bytes_erased < ( c->device_size - z - blocksize ) )
        {
            c->bytes_erased = c->device_size - z - blocksize;
        }
        free( b );
        return -1;
    }

    free( b );

    return 0;

} /* nwipe_static_pass */
