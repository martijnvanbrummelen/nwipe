/*
 *  pass_static.c: Static (pattern) pass-related I/O routines.
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
 * nwipe_static_forward_pass
 *
 * Writes a static pattern to the device.
 *
 * The pattern is repeated into a large buffer and then written in equally
 * large I/O blocks (e.g. 4 MiB). The "window" offset w keeps track of where
 * in the repeating pattern we are when moving across the device.
 */
int nwipe_static_forward_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    off64_t current_offset;
    char* b;
    char* p;
    char* wbuf;
    const void* wsrc;
    int w = 0; /* window offset into pattern */
    u64 z = c->device_size;
    u64 bs = 0; /* pass bytes skipped */

    int syncRate = nwipe_options.sync;

    io_blocksize = nwipe_effective_io_blocksize( c );

    /* For direct I/O we do not need periodic fdatasync(), I/O errors are detected
     * at write() time. Keep sync for cached I/O only. */
    if( c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        syncRate = 0;
        nwipe_log( NWIPE_LOG_NOTICE, "Disabled fdatasync for %s, DirectI/O in use.", c->device_name );
    }
    else /* for cached I/O only */
    {
        /* Compute per-write sync rate (same semantics as random pass). */
        syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );
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

    /*
     * For static patterns we want enough buffer space to always have a
     * contiguous window of "io_blocksize" bytes available starting at any
     * offset w in [0, pattern->length). Using:
     *
     *   buffer_size >= io_blocksize + pattern->length * 2
     *
     * guarantees that we can wrap around the repeating pattern safely.
     */
    b = (char*) nwipe_alloc_io_buffer(
        c, io_blocksize + (size_t) pattern->length * 2, 0, "static_pass pattern buffer" );
    if( !b )
        return -1;

    for( p = b; p < b + io_blocksize + pattern->length; p += pattern->length )
    {
        memcpy( p, pattern->s, (size_t) pattern->length );
    }

    /*
     * We need an aligned buffer to copy the window-adjusted pattern into,
     * otherwise patterns whose length doesn't divide the I/O block size
     * evenly will fail under IO_DIRECT with EINVAL, as seen with Gutmann.
     */
    wbuf = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "static_pass write buffer" );
    if( !wbuf )
    {
        free( b );
        return -1;
    }

    /* Rewind device. */
    offset = lseek( c->device_fd, 0, SEEK_SET );
    c->pass_done = 0;

    if( offset == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
        free( b );
        free( wbuf );
        return -1;
    }

    if( offset != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: lseek() returned a bogus offset on '%s'.", c->device_name );
        free( b );
        free( wbuf );
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

        /* Record the offset we're at before the write. */
        current_offset = (off64_t) ( c->device_size - z );

        if( pattern->length > 0 )
        {
            /* Adjust the pattern window for the current offset */
            w = (int) ( current_offset % (off64_t) pattern->length );
        }

        /*
         * Copy "blocksize" bytes starting at offset w into the aligned
         * pattern buffer. Because we filled the entire buffer with the
         * pattern (and made it large enough), &b[w] is always valid.
         */
        if( w == 0 )
        {
            wsrc = &b[w]; /* already aligned, skip the copy */
        }
        else
        {
            memcpy( wbuf, &b[w], blocksize );
            wsrc = wbuf;
        }

        r = (int) nwipe_write_with_retry( c, c->device_fd, wsrc, blocksize );

        if( r < 0 )
        {
            c->pass_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block write failed and the user requested to NOT abort the pass.
                 * As in the random pass, treat the whole block as failed, skip it,
                 * count the bytes, and continue with the next block.
                 */
                u64 s = (u64) blocksize;

                /* Log the write error and that we are skipping this block. */
                nwipe_perror( errno, __FUNCTION__, "write" );
                nwipe_log( NWIPE_LOG_ERROR,
                           "Write error on '%s' at offset %lld, skipping %llu bytes.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                /*
                 * Skip forward by one block on the device, so subsequent writes
                 * continue after the failing region.
                 */
                offset = lseek( c->device_fd, (off64_t) s, SEEK_CUR );
                if( offset == (off64_t) -1 )
                {
                    /*
                     * If we cannot move the file offset, we cannot safely continue,
                     * so we must abort the pass even in no-abort mode.
                     */
                    nwipe_perror( errno, __FUNCTION__, "lseek" );
                    nwipe_log(
                        NWIPE_LOG_FATAL, "Unable to bump the '%s' file offset after a write error.", c->device_name );
                    free( b );
                    free( wbuf );
                    return -1;
                }

                /*
                 * This block is logically processed (address space advanced),
                 * but not actually written. Reflect that in the remaining size
                 * and in the per-round progress, but DO NOT increase pass_done.
                 */
                z -= s;
                bs += s;
                c->round_done += s;

                /* Allow thread cancellation points and proceed with the next loop iteration. */
                pthread_testcancel();

                continue;
            }

            /*
             * Default behaviour (no no-abort option):
             * Try to touch the bad block from the back (reverse wipe).
             * If we meet another bad block along the way, abort the wipe.
             */
            off64_t rev_offset = (off64_t) ( c->device_size - (u64) io_blocksize );
            size_t rev_blocksize = io_blocksize;
            int rev_w = 0;

            nwipe_perror( errno, __FUNCTION__, "write" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "Write error on '%s' at offset %lld, starting reverse wipe.",
                       c->device_name,
                       (long long) current_offset );

            /* The bad block itself is unwritable, count it as skipped */
            z -= (u64) blocksize;
            bs += (u64) blocksize;
            c->round_done += (u64) blocksize;

            if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                nwipe_update_bytes_erased( c, z, bs, 1 );

            c->io_direction = NWIPE_IO_DIRECTION_REVERSE;

            if( c->device_size % (u64) io_blocksize != 0 )
            {
                if( c->device_size % (u64) c->device_stat.st_blksize != 0 )
                {
                    nwipe_log( NWIPE_LOG_WARNING,
                               "%s: The size of '%s' is not a multiple of its block size %i.",
                               __FUNCTION__,
                               c->device_name,
                               c->device_stat.st_blksize );
                }

                /* The last block of the device is smaller than our I/O blocksize, adjust it */
                rev_offset = (off64_t) ( c->device_size - ( c->device_size % (u64) io_blocksize ) );
                rev_blocksize = (size_t) ( c->device_size % (u64) io_blocksize );
            }

            while( rev_offset > current_offset )
            {
                if( pattern->length > 0 )
                {
                    /* Adjust the pattern window for the required offset */
                    rev_w = (int) ( rev_offset % (off64_t) pattern->length );
                }

                if( rev_w == 0 )
                {
                    wsrc = &b[rev_w]; /* already aligned, skip the copy */
                }
                else
                {
                    memcpy( wbuf, &b[rev_w], rev_blocksize );
                    wsrc = wbuf;
                }

                r = (int) nwipe_pwrite_with_retry( c, c->device_fd, wsrc, rev_blocksize, rev_offset );

                if( r < 0 )
                {
                    nwipe_perror( errno, __FUNCTION__, "pwrite" );
                    nwipe_log( NWIPE_LOG_FATAL,
                               "Reverse wipe failed on '%s' at offset %lld.",
                               c->device_name,
                               (long long) rev_offset );

                    c->pass_errors += 1;
                    c->io_direction = nwipe_options.io_direction;

                    if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                        nwipe_update_bytes_erased( c, z, bs, 1 );

                    free( b );
                    free( wbuf );
                    return -1;
                }

                if( r != (int) rev_blocksize )
                {
                    /*
                     * Short write (rare with block devices)
                     * Count what was written, skip the short bytes.
                     */
                    int s = (int) rev_blocksize - r;

                    nwipe_log( NWIPE_LOG_ERROR,
                               "Partial write on '%s' at offset %lld, %i bytes short.",
                               c->device_name,
                               (long long) rev_offset,
                               s );

                    /* Increase the error count since we skipped bytes */
                    c->pass_errors += 1;

                    /* We need to count the skipped bytes logically, otherwise erasing
                     * will try to write them past the device size at the erase end */
                    z -= (u64) s;
                    bs += (u64) s;
                    c->round_done += (u64) s;
                }

                z -= (u64) r;
                c->pass_done += (u64) r;
                c->round_done += (u64) r;

                nwipe_update_bytes_erased( c, z, bs, 0 );

                rev_offset -= (off64_t) rev_blocksize;
                rev_blocksize = io_blocksize; /* must be a full I/O block now */

                pthread_testcancel();
            }

            nwipe_log( NWIPE_LOG_NOTICE,
                       "Reverse wipe on '%s' reached initial bad block at offset %lld.",
                       c->device_name,
                       (long long) current_offset );

            c->io_direction = nwipe_options.io_direction;

            if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                nwipe_update_bytes_erased( c, z, bs, 1 );

            /* The disk is otherwise stable, we return with non-fatal errors
             * so other passes can continue; overall result will be failure. */
            free( b );
            free( wbuf );
            return 0;
        }

        if( r != (int) blocksize )
        {
            /*
             * Short write (rare with block devices)
             * Count what was written, skip the short bytes.
             */
            int s = (int) blocksize - r;

            nwipe_log( NWIPE_LOG_ERROR,
                       "Partial write on '%s' at offset %lld, %i bytes short.",
                       c->device_name,
                       (long long) current_offset,
                       s );

            /* Increase the error count since we skipped bytes */
            c->pass_errors += 1;

            /* We need to count the skipped bytes logically, otherwise erasing
             * will try to write them past the device size at the erase end */
            z -= (u64) s;
            bs += (u64) s;
            c->round_done += (u64) s;

            /* Seek forward the skipped bytes so we stay in-sync for next write */
            offset = lseek( c->device_fd, (off64_t) s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_FATAL, "Unable to bump the '%s' file offset after a partial write.", c->device_name );
                free( b );
                free( wbuf );
                return -1;
            }
        }

        z -= (u64) r;
        c->pass_done += (u64) r;
        c->round_done += (u64) r;

        nwipe_update_bytes_erased( c, z, bs, 0 );

        /* Periodic sync if requested. */
        if( syncRate > 0 )
        {
            i++;

            if( i >= syncRate )
            {
                r = nwipe_fdatasync( c, __FUNCTION__ );

                if( r == 0 )
                {
                    nwipe_update_bytes_erased( c, z, bs, 1 );
                }

                if( r == -1 )
                {
                    free( b );
                    free( wbuf );
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

    } /* /remaining bytes */

    free( b );
    free( wbuf );

    /* Final sync at end of pass. */
    r = nwipe_fdatasync( c, __FUNCTION__ );
    if( r == 0 )
        nwipe_update_bytes_erased( c, z, bs, 1 );
    if( r == -1 )
        return -1;

    return 0;

} /* nwipe_static_forward_pass */

/*
 * nwipe_static_reverse_pass
 *
 * Writes a static pattern to the device.
 *
 * The pattern is repeated into a large buffer and then written in equally
 * large I/O blocks (e.g. 4 MiB). The "window" offset w keeps track of where
 * in the repeating pattern we are when moving across the device.
 */
int nwipe_static_reverse_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t current_offset;
    char* b;
    char* p;
    char* wbuf;
    const void* wsrc;
    int w = 0; /* window offset into pattern */
    u64 z = c->device_size;
    u64 bs = 0; /* pass bytes skipped */

    int syncRate = nwipe_options.sync;

    io_blocksize = nwipe_effective_io_blocksize( c );

    /* For direct I/O we do not need periodic fdatasync(), I/O errors are detected
     * at write() time. Keep sync for cached I/O only. */
    if( c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        syncRate = 0;
        nwipe_log( NWIPE_LOG_NOTICE, "Disabled fdatasync for %s, DirectI/O in use.", c->device_name );
    }
    else /* for cached I/O only */
    {
        /* Compute per-write sync rate (same semantics as random pass). */
        syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );
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

    /*
     * For static patterns we want enough buffer space to always have a
     * contiguous window of "io_blocksize" bytes available starting at any
     * offset w in [0, pattern->length). Using:
     *
     *   buffer_size >= io_blocksize + pattern->length * 2
     *
     * guarantees that we can wrap around the repeating pattern safely.
     */
    b = (char*) nwipe_alloc_io_buffer(
        c, io_blocksize + (size_t) pattern->length * 2, 0, "static_pass pattern buffer" );
    if( !b )
        return -1;

    for( p = b; p < b + io_blocksize + pattern->length; p += pattern->length )
    {
        memcpy( p, pattern->s, (size_t) pattern->length );
    }

    /*
     * We need an aligned buffer to copy the window-adjusted pattern into,
     * otherwise patterns whose length doesn't divide the I/O block size
     * evenly will fail under IO_DIRECT with EINVAL, as seen with Gutmann.
     */
    wbuf = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "static_pass write buffer" );
    if( !wbuf )
    {
        free( b );
        return -1;
    }

    /* Reset pass byte counter */
    c->pass_done = 0;

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

        /* Record the offset we're at before the write (reverse-adjusted). */
        current_offset = (off64_t) ( z - (u64) blocksize );

        if( pattern->length > 0 )
        {
            /* Adjust the pattern window for the current offset */
            w = (int) ( current_offset % (off64_t) pattern->length );
        }

        /*
         * Copy "blocksize" bytes starting at offset w into the aligned
         * pattern buffer. Because we filled the entire buffer with the
         * pattern (and made it large enough), &b[w] is always valid.
         */
        if( w == 0 )
        {
            wsrc = &b[w]; /* already aligned, skip the copy */
        }
        else
        {
            memcpy( wbuf, &b[w], blocksize );
            wsrc = wbuf;
        }

        r = (int) nwipe_pwrite_with_retry( c, c->device_fd, wsrc, blocksize, current_offset );

        if( r < 0 )
        {
            c->pass_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block write failed and the user requested to NOT abort the pass.
                 * As in the random pass, treat the whole block as failed, skip it,
                 * count the bytes, and continue with the next block.
                 */
                u64 s = (u64) blocksize;

                /* Log the write error and that we are skipping this block. */
                nwipe_perror( errno, __FUNCTION__, "pwrite" );
                nwipe_log( NWIPE_LOG_ERROR,
                           "Write error on '%s' at offset %lld, skipping %llu bytes.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                /*
                 * This block is logically processed (address space advanced),
                 * but not actually written. Reflect that in the remaining size
                 * and in the per-round progress, but DO NOT increase pass_done.
                 */
                z -= s;
                bs += s;
                c->round_done += s;

                /* Allow thread cancellation points and proceed with the next loop iteration. */
                pthread_testcancel();

                continue;
            }

            /*
             * Default behaviour (no no-abort option):
             * Try to touch the bad block from the back (reverse wipe).
             * If we meet another bad block along the way, abort the wipe.
             */
            off64_t fwd_offset = 0;
            size_t fwd_blocksize = io_blocksize;
            int fwd_w = 0;

            nwipe_perror( errno, __FUNCTION__, "pwrite" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "Write error on '%s' at offset %lld, starting forward wipe.",
                       c->device_name,
                       (long long) current_offset );

            /* The bad block itself is unwritable, count it as skipped */
            z -= (u64) blocksize;
            bs += (u64) blocksize;
            c->round_done += (u64) blocksize;

            if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                nwipe_update_bytes_erased( c, z, bs, 1 );

            c->io_direction = NWIPE_IO_DIRECTION_FORWARD;

            while( fwd_offset < current_offset )
            {
                /* If the last block failed, it may be smaller than an I/O block */
                if( ( fwd_offset + (off64_t) fwd_blocksize ) > current_offset )
                    fwd_blocksize = (size_t) ( current_offset - fwd_offset );

                if( pattern->length > 0 )
                {
                    /* Adjust the pattern window for the required offset */
                    fwd_w = (int) ( fwd_offset % (off64_t) pattern->length );
                }

                if( fwd_w == 0 )
                {
                    wsrc = &b[fwd_w]; /* already aligned, skip the copy */
                }
                else
                {
                    memcpy( wbuf, &b[fwd_w], fwd_blocksize );
                    wsrc = wbuf;
                }

                r = (int) nwipe_pwrite_with_retry( c, c->device_fd, wsrc, fwd_blocksize, fwd_offset );

                if( r < 0 )
                {
                    nwipe_perror( errno, __FUNCTION__, "pwrite" );
                    nwipe_log( NWIPE_LOG_FATAL,
                               "Forward wipe failed on '%s' at offset %lld.",
                               c->device_name,
                               (long long) fwd_offset );

                    c->pass_errors += 1;
                    c->io_direction = nwipe_options.io_direction;

                    if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                        nwipe_update_bytes_erased( c, z, bs, 1 );

                    free( b );
                    free( wbuf );
                    return -1;
                }

                if( r != (int) fwd_blocksize )
                {
                    /*
                     * Short write (rare with block devices)
                     * Count what was written, skip the short bytes.
                     */
                    int s = (int) fwd_blocksize - r;

                    nwipe_log( NWIPE_LOG_ERROR,
                               "Partial write on '%s' at offset %lld, %i bytes short.",
                               c->device_name,
                               (long long) fwd_offset,
                               s );

                    /* Increase the error count since we skipped bytes */
                    c->pass_errors += 1;

                    /* We need to count the skipped bytes logically, otherwise erasing
                     * will try to write them past the device size at the erase end */
                    z -= (u64) s;
                    bs += (u64) s;
                    c->round_done += (u64) s;
                }

                z -= (u64) r;
                c->pass_done += (u64) r;
                c->round_done += (u64) r;

                nwipe_update_bytes_erased( c, z, bs, 0 );

                fwd_offset += (off64_t) fwd_blocksize;

                pthread_testcancel();
            }

            nwipe_log( NWIPE_LOG_NOTICE,
                       "Forward wipe on '%s' reached initial bad block at offset %lld.",
                       c->device_name,
                       (long long) current_offset );

            c->io_direction = nwipe_options.io_direction;

            if( nwipe_fdatasync( c, __FUNCTION__ ) == 0 ) /* Best effort */
                nwipe_update_bytes_erased( c, z, bs, 1 );

            /* The disk is otherwise stable, we return with non-fatal errors
             * so other passes can continue; overall result will be failure. */
            free( b );
            free( wbuf );
            return 0;
        }

        if( r != (int) blocksize )
        {
            /*
             * Short write (rare with block devices)
             * Count what was written, skip the short bytes.
             */
            int s = (int) blocksize - r;

            nwipe_log( NWIPE_LOG_ERROR,
                       "Partial write on '%s' at offset %lld, %i bytes short.",
                       c->device_name,
                       (long long) current_offset,
                       s );

            /* Increase the error count since we skipped bytes */
            c->pass_errors += 1;

            /* We need to count the skipped bytes logically, otherwise erasing
             * will try to write them past the device size at the erase end */
            z -= (u64) s;
            bs += (u64) s;
            c->round_done += (u64) s;
        }

        z -= (u64) r;
        c->pass_done += (u64) r;
        c->round_done += (u64) r;

        nwipe_update_bytes_erased( c, z, bs, 0 );

        /* Periodic sync if requested. */
        if( syncRate > 0 )
        {
            i++;

            if( i >= syncRate )
            {
                r = nwipe_fdatasync( c, __FUNCTION__ );

                if( r == 0 )
                {
                    nwipe_update_bytes_erased( c, z, bs, 1 );
                }

                if( r == -1 )
                {
                    free( b );
                    free( wbuf );
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

    } /* /remaining bytes */

    free( b );
    free( wbuf );

    /* Final sync at end of pass. */
    r = nwipe_fdatasync( c, __FUNCTION__ );
    if( r == 0 )
        nwipe_update_bytes_erased( c, z, bs, 1 );
    if( r == -1 )
        return -1;

    return 0;

} /* nwipe_static_reverse_pass */

/*
 * nwipe_static_forward_verify
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
int nwipe_static_forward_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    off64_t current_offset;
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
    d = (char*) nwipe_alloc_io_buffer(
        c, io_blocksize + (size_t) pattern->length * 2, 0, "static_verify pattern buffer" );
    if( !d )
    {
        free( b );
        return -1;
    }

    for( q = d; q < d + io_blocksize + pattern->length; q += pattern->length )
    {
        memcpy( q, pattern->s, (size_t) pattern->length );
    }

    /* Ensure all writes are flushed before verification. */
    nwipe_fdatasync( c, __FUNCTION__ );

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

        /* Record the offset we're at before the read. */
        current_offset = (off64_t) ( c->device_size - z );

        if( pattern->length > 0 )
        {
            /* Adjust the pattern window for the current offset */
            w = (int) ( current_offset % (off64_t) pattern->length );
        }

        /* Read from the device */
        r = (int) nwipe_read_with_retry( c, c->device_fd, b, blocksize );

        if( r < 0 )
        {
            c->verify_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block read failed and the user requested to NOT abort the pass.
                 * Treat the whole block as failed, skip it, count the bytes,
                 * and continue with the next block.
                 */
                u64 s = (u64) blocksize;

                /* Log the read error and that we are skipping this block. */
                nwipe_perror( errno, __FUNCTION__, "read" );
                nwipe_log( NWIPE_LOG_ERROR,
                           "Read error on '%s' at offset %lld, skipping %llu bytes.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                /*
                 * Skip forward by one block on the device, so subsequent reads
                 * continue after the failing region.
                 */
                offset = lseek( c->device_fd, (off64_t) s, SEEK_CUR );
                if( offset == (off64_t) -1 )
                {
                    /*
                     * If we cannot move the file offset, we cannot safely continue,
                     * so we must abort the pass even in no-abort mode.
                     */
                    nwipe_perror( errno, __FUNCTION__, "lseek" );
                    nwipe_log(
                        NWIPE_LOG_FATAL, "Unable to bump the '%s' file offset after a read error.", c->device_name );
                    free( b );
                    free( d );
                    return -1;
                }

                /*
                 * This block is logically processed (address space advanced),
                 * but not actually written. Reflect that in the remaining size
                 * and in the per-round progress, but DO NOT increase pass_done.
                 */
                z -= s;
                c->round_done += s;

                /* Allow thread cancellation points and proceed with the next loop iteration. */
                pthread_testcancel();

                continue;
            }

            /*
             * Default behaviour (no no-abort option):
             * Abort the verification because of the read error.
             */
            nwipe_perror( errno, __FUNCTION__, "read" );
            nwipe_log(
                NWIPE_LOG_FATAL, "Read error on '%s' at offset %lld.", c->device_name, (long long) current_offset );
            free( b );
            free( d );
            return -1;
        }

        if( r != (int) blocksize )
        {
            /*
             * Short read (rare with block devices)
             * Compare what was read, skip the short bytes.
             */
            int s = (int) blocksize - r;

            nwipe_log( NWIPE_LOG_ERROR,
                       "Partial read on '%s' at offset %lld, %i bytes short.",
                       c->device_name,
                       (long long) current_offset,
                       s );

            /* Increase the error count since we skipped bytes */
            c->verify_errors += 1;

            /* We need to count the skipped bytes logically, otherwise verify
             * will try to read them past the device size at the verify end */
            z -= (u64) s;
            c->round_done += (u64) s;

            /* Seek forward the skipped bytes so we stay in-sync for next read */
            offset = lseek( c->device_fd, (off64_t) s, SEEK_CUR );
            if( offset == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log(
                    NWIPE_LOG_FATAL, "Unable to bump the '%s' file offset after a partial read.", c->device_name );
                free( b );
                free( d );
                return -1;
            }
        }

        /* Compare the bytes we did read */
        if( memcmp( b, &d[w], (size_t) r ) != 0 )
        {
            c->verify_errors += 1;
        }

        z -= (u64) r;
        c->pass_done += (u64) r;
        c->round_done += (u64) r;

        pthread_testcancel();

    } /* while bytes remaining */

    free( b );
    free( d );

    return 0;

} /* nwipe_static_forward_verify */

/*
 * nwipe_static_reverse_verify
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
int nwipe_static_reverse_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t current_offset;
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
    d = (char*) nwipe_alloc_io_buffer(
        c, io_blocksize + (size_t) pattern->length * 2, 0, "static_verify pattern buffer" );
    if( !d )
    {
        free( b );
        return -1;
    }

    for( q = d; q < d + io_blocksize + pattern->length; q += pattern->length )
    {
        memcpy( q, pattern->s, (size_t) pattern->length );
    }

    /* Ensure all writes are flushed before verification. */
    nwipe_fdatasync( c, __FUNCTION__ );

    /* Reset pass byte counter */
    c->pass_done = 0;

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

        /* Record the offset we're at before the read (reverse-adjusted). */
        current_offset = (off64_t) ( z - (u64) blocksize );

        if( pattern->length > 0 )
        {
            /* Adjust the pattern window for the current offset */
            w = (int) ( current_offset % (off64_t) pattern->length );
        }

        /* Read from the device */
        r = (int) nwipe_pread_with_retry( c, c->device_fd, b, blocksize, current_offset );

        if( r < 0 )
        {
            c->verify_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block read failed and the user requested to NOT abort the pass.
                 * Treat the whole block as failed, skip it, count the bytes,
                 * and continue with the next block.
                 */
                u64 s = (u64) blocksize;

                /* Log the read error and that we are skipping this block. */
                nwipe_perror( errno, __FUNCTION__, "pread" );
                nwipe_log( NWIPE_LOG_ERROR,
                           "Read error on '%s' at offset %lld, skipping %llu bytes.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                /*
                 * This block is logically processed (address space advanced),
                 * but not actually written. Reflect that in the remaining size
                 * and in the per-round progress, but DO NOT increase pass_done.
                 */
                z -= s;
                c->round_done += s;

                /* Allow thread cancellation points and proceed with the next loop iteration. */
                pthread_testcancel();

                continue;
            }

            /*
             * Default behaviour (no no-abort option):
             * Abort the verification because of the read error.
             */
            nwipe_perror( errno, __FUNCTION__, "pread" );
            nwipe_log(
                NWIPE_LOG_FATAL, "Read error on '%s' at offset %lld.", c->device_name, (long long) current_offset );
            free( b );
            free( d );
            return -1;
        }

        if( r != (int) blocksize )
        {
            /*
             * Short read (rare with block devices)
             * Compare what was read, skip the short bytes.
             */
            int s = (int) blocksize - r;

            nwipe_log( NWIPE_LOG_ERROR,
                       "Partial read on '%s' at offset %lld, %i bytes short.",
                       c->device_name,
                       (long long) current_offset,
                       s );

            /* Increase the error count since we skipped bytes */
            c->verify_errors += 1;

            /* We need to count the skipped bytes logically, otherwise verify
             * will try to read them past the device size at the verify end */
            z -= (u64) s;
            c->round_done += (u64) s;
        }

        /* Compare the bytes we did read */
        if( memcmp( b, &d[w], (size_t) r ) != 0 )
        {
            c->verify_errors += 1;
        }

        z -= (u64) r;
        c->pass_done += (u64) r;
        c->round_done += (u64) r;

        pthread_testcancel();

    } /* while bytes remaining */

    free( b );
    free( d );

    return 0;

} /* nwipe_static_reverse_verify */
