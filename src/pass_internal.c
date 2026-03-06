/*
 *  pass_internal.c: Internal pass-related I/O routines.
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
#include "pass_internal.h"

/*
 * Tunable sizes for the wiping / verification I/O path.
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

#ifndef NWIPE_IO_BLOCKSIZE
#define NWIPE_IO_BLOCKSIZE ( 4 * 1024 * 1024UL ) /* 4 MiB I/O block */
#endif

#if NWIPE_IO_BLOCKSIZE > INT_MAX
#error "NWIPE_IO_BLOCKSIZE must fit in an int"
#endif

/*
 * nwipe_(p)write_with_retry / nwipe_(p)read_with_retry
 *
 * Attempt the I/O up to MAX_IO_ATTEMPTS times, sleeping IO_RETRY_DELAY_S
 * seconds between attempts.  On persistent failure the last return value
 * (negative errno or short count) is returned to the caller unchanged, so
 * existing error-handling logic is unaffected.
 */

#ifndef NWIPE_MAX_IO_ATTEMPTS
#define NWIPE_MAX_IO_ATTEMPTS 3
#endif

#ifndef NWIPE_IO_RETRY_DELAY_S
#define NWIPE_IO_RETRY_DELAY_S 5
#endif

#if NWIPE_MAX_IO_ATTEMPTS < 1
#error "NWIPE_MAX_IO_ATTEMPTS must be at least 1"
#endif

/* Behaves like write(), but retries up to MAX_IO_ATTEMPTS times on error or short write.
 * Returns -1 with errno from lseek() if seeking back after a short write fails. */
ssize_t nwipe_write_with_retry( nwipe_context_t* c, int fd, const void* buf, size_t count )
{
    ssize_t r;
    int attempt;
    int slept_s;

    for( attempt = 0; attempt < NWIPE_MAX_IO_ATTEMPTS; attempt++ )
    {
        r = write( fd, buf, count );

        if( nwipe_options.noretry_io_errors )
            return r; /* retrying is disabled */

        if( r == (ssize_t) count )
        {
            c->retry_status = 0;
            return r; /* full write - success */
        }

        if( r < 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: write() failed on '%s' (attempt %d/%d): %s",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       strerror( errno ) );
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: short write on '%s' (attempt %d/%d): "
                       "wrote %zd of %zu bytes.",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       r,
                       count );
        }

        if( attempt + 1 < NWIPE_MAX_IO_ATTEMPTS )
        {
            c->io_retries += 1;
            c->retry_status = 1;

            nwipe_log( NWIPE_LOG_NOTICE, "%s: retrying in %d seconds ...", __FUNCTION__, NWIPE_IO_RETRY_DELAY_S );

            for( slept_s = 0; slept_s < NWIPE_IO_RETRY_DELAY_S; slept_s++ )
            {
                sleep( 1 );
                pthread_testcancel();
            }

            if( r > 0 )
            {
                if( lseek( fd, -r, SEEK_CUR ) == (off64_t) -1 )
                {
                    nwipe_perror( errno, __FUNCTION__, "lseek" );
                    nwipe_log(
                        NWIPE_LOG_ERROR, "%s: cannot rewind after short write on '%s'.", __FUNCTION__, c->device_name );
                    c->retry_status = 0;
                    return -1; /* fatal, we don't know where we are */
                }
            }
        }
    }

    nwipe_log( NWIPE_LOG_ERROR,
               "%s: giving up write() on '%s' after %d attempts.",
               __FUNCTION__,
               c->device_name,
               NWIPE_MAX_IO_ATTEMPTS );

    c->retry_status = 0;
    return r;
} /* nwipe_write_with_retry */

/* Behaves like pwrite(), but retries up to MAX_IO_ATTEMPTS times on error or short write. */
ssize_t nwipe_pwrite_with_retry( nwipe_context_t* c, int fd, const void* buf, size_t count, off64_t offset )
{
    ssize_t r;
    int attempt;
    int slept_s;

    for( attempt = 0; attempt < NWIPE_MAX_IO_ATTEMPTS; attempt++ )
    {
        r = pwrite( fd, buf, count, offset );

        if( nwipe_options.noretry_io_errors )
            return r; /* retrying is disabled */

        if( r == (ssize_t) count )
        {
            c->retry_status = 0;
            return r; /* full write - success */
        }

        if( r < 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: pwrite() failed on '%s' (attempt %d/%d): %s",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       strerror( errno ) );
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: short write on '%s' (attempt %d/%d): "
                       "wrote %zd of %zu bytes.",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       r,
                       count );
        }

        if( attempt + 1 < NWIPE_MAX_IO_ATTEMPTS )
        {
            c->io_retries += 1;
            c->retry_status = 1;

            nwipe_log( NWIPE_LOG_NOTICE, "%s: retrying in %d seconds ...", __FUNCTION__, NWIPE_IO_RETRY_DELAY_S );

            for( slept_s = 0; slept_s < NWIPE_IO_RETRY_DELAY_S; slept_s++ )
            {
                sleep( 1 );
                pthread_testcancel();
            }
        }
    }

    nwipe_log( NWIPE_LOG_ERROR,
               "%s: giving up pwrite() on '%s' after %d attempts.",
               __FUNCTION__,
               c->device_name,
               NWIPE_MAX_IO_ATTEMPTS );

    c->retry_status = 0;
    return r;
} /* nwipe_pwrite_with_retry */

/* Behaves like read(), but retries up to MAX_IO_ATTEMPTS times on error or short read.
 * Returns -1 with errno from lseek() if seeking back after a short read fails. */
ssize_t nwipe_read_with_retry( nwipe_context_t* c, int fd, void* buf, size_t count )
{
    ssize_t r;
    int attempt;
    int slept_s;

    for( attempt = 0; attempt < NWIPE_MAX_IO_ATTEMPTS; attempt++ )
    {
        r = read( fd, buf, count );

        if( nwipe_options.noretry_io_errors )
            return r; /* retrying is disabled */

        if( r == (ssize_t) count || r == 0 )
        {
            c->retry_status = 0;
            return r; /* full read or EOF - success */
        }

        if( r < 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: read() failed on '%s' (attempt %d/%d): %s",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       strerror( errno ) );
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: short read on '%s' (attempt %d/%d): "
                       "read %zd of %zu bytes.",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       r,
                       count );
        }

        if( attempt + 1 < NWIPE_MAX_IO_ATTEMPTS )
        {
            c->io_retries += 1;
            c->retry_status = 1;

            nwipe_log( NWIPE_LOG_NOTICE, "%s: retrying in %d seconds ...", __FUNCTION__, NWIPE_IO_RETRY_DELAY_S );

            for( slept_s = 0; slept_s < NWIPE_IO_RETRY_DELAY_S; slept_s++ )
            {
                sleep( 1 );
                pthread_testcancel();
            }

            if( r > 0 )
            {
                if( lseek( fd, -r, SEEK_CUR ) == (off64_t) -1 )
                {
                    nwipe_perror( errno, __FUNCTION__, "lseek" );
                    nwipe_log(
                        NWIPE_LOG_ERROR, "%s: cannot rewind after short read on '%s'.", __FUNCTION__, c->device_name );
                    c->retry_status = 0;
                    return -1; /* fatal, we don't know where we are */
                }
            }
        }
    }

    nwipe_log( NWIPE_LOG_ERROR,
               "%s: giving up read() on '%s' after %d attempts.",
               __FUNCTION__,
               c->device_name,
               NWIPE_MAX_IO_ATTEMPTS );

    c->retry_status = 0;
    return r;
} /* nwipe_read_with_retry */

/* Behaves like pread(), but retries up to MAX_IO_ATTEMPTS times on error or short write. */
ssize_t nwipe_pread_with_retry( nwipe_context_t* c, int fd, void* buf, size_t count, off64_t offset )
{
    ssize_t r;
    int attempt;
    int slept_s;

    for( attempt = 0; attempt < NWIPE_MAX_IO_ATTEMPTS; attempt++ )
    {
        r = pread( fd, buf, count, offset );

        if( nwipe_options.noretry_io_errors )
            return r; /* retrying is disabled */

        if( r == (ssize_t) count || r == 0 )
        {
            c->retry_status = 0;
            return r; /* full read or EOF - success */
        }

        if( r < 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: pread() failed on '%s' (attempt %d/%d): %s",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       strerror( errno ) );
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "%s: short read on '%s' (attempt %d/%d): "
                       "read %zd of %zu bytes.",
                       __FUNCTION__,
                       c->device_name,
                       attempt + 1,
                       NWIPE_MAX_IO_ATTEMPTS,
                       r,
                       count );
        }

        if( attempt + 1 < NWIPE_MAX_IO_ATTEMPTS )
        {
            c->io_retries += 1;
            c->retry_status = 1;

            nwipe_log( NWIPE_LOG_NOTICE, "%s: retrying in %d seconds ...", __FUNCTION__, NWIPE_IO_RETRY_DELAY_S );

            for( slept_s = 0; slept_s < NWIPE_IO_RETRY_DELAY_S; slept_s++ )
            {
                sleep( 1 );
                pthread_testcancel();
            }
        }
    }

    nwipe_log( NWIPE_LOG_ERROR,
               "%s: giving up pread() on '%s' after %d attempts.",
               __FUNCTION__,
               c->device_name,
               NWIPE_MAX_IO_ATTEMPTS );

    c->retry_status = 0;
    return r;
} /* nwipe_pread_with_retry */

/*
 * Performs fdatasync(), logs (and increases error count).
 * Returns 0 on success, 1 on soft failure, -1 on fatal failure.
 * Soft failure is when the option noabort_block_errors is enabled.
 * fsyncdata_errors is incremented on any kind of failure (-1 or 1).
 */
int nwipe_fdatasync( nwipe_context_t* c, const char* f )
{
    int r;

    c->sync_status = 1;
    r = fdatasync( c->device_fd );
    c->sync_status = 0;

    if( r != 0 )
    {
        c->fsyncdata_errors += 1;

        nwipe_perror( errno, f, "fdatasync" );
        nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );

        if( nwipe_options.noabort_block_errors )
        {
            /* Sync errors are to be expected with bad blocks, so we must allow them */
            return 1;
        }

        return -1;
    }

    return 0;
} /* nwipe_fdatasync */

/*
 * Compute the effective I/O block size for a given device:
 *
 * - Must be at least the device's reported st_blksize (usually 4 KiB).
 * - Starts from NWIPE_IO_BLOCKSIZE (4 MiB by default) and adjusts.
 * - Rounded down to a multiple of st_blksize so it is compatible with
 *   O_DIRECT alignment rules.
 * - Never exceeds the device size.
 */
size_t nwipe_effective_io_blocksize( const nwipe_context_t* c )
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
} /* nwipe_effective_io_blocksize */

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
void* nwipe_alloc_io_buffer( const nwipe_context_t* c, size_t size, int clear, const char* label )
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
} /* nwipe_alloc_io_buffer */

/*
 * Compute the per-write sync rate for a given device and I/O block size.
 *
 * Historically, --sync=N meant "fdatasync() every N * st_blksize bytes".
 * Now that we use large I/O blocks, we convert that into "sync every K writes",
 * where each write is of size io_blocksize.
 *
 * For O_DIRECT we return 0 because write() already reports I/O errors directly.
 */
int nwipe_compute_sync_rate_for_device( const nwipe_context_t* c, size_t io_blocksize )
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
} /* nwipe_compute_sync_rate_for_device */

/*
 * Updates the erased byte count with the provided bytes remaining (z), bytes
 * skipped (bs) and synced flag. The synced flag should only be set when a
 * sync immediately prior to calling this function was successful (r == 0).
 *
 * The synced flag ensures that in cached I/O mode the count is only ever
 * updated when the bytes have in fact been written to the disk, which is
 * ensured by the sync flag and a prior successful fsyncdata function call.
 */
void nwipe_update_bytes_erased( nwipe_context_t* c, u64 z, u64 bs, int synced )
{
    if( synced || c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        u64 be = c->device_size - z - bs;

        /*
         * High-water calculation of bytes erased across passes:
         * If this pass erased more of the device than any previous pass,
         * use that new highest number, otherwise keep previous bytes erased.
         */
        if( c->bytes_erased < be )
        {
            c->bytes_erased = be;
        }
    }
} /* nwipe_update_bytes_erased */

/* Checks if the PRNG buffer is (1 = not all zeroes, 0 = all zeroes) */
int nwipe_prng_is_active( const char* buf, size_t blocksize )
{
    for( size_t i = 0; i < blocksize; i++ )
    {
        if( buf[i] != 0 )
        {
            return 1;
        }
    }

    return 0;
} /* nwipe_prng_is_active */
