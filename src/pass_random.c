/*
 *  pass_random.c: Random (PRNG) pass-related I/O routines.
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
 * nwipe_random_forward_pass
 *
 * Writes a random pattern to the device using the configured PRNG.
 *
 * This version uses:
 *   - an io_blocksize allocated buffer, zero-initialized to avoid leaking
 *     previous memory content in case of PRNG bugs, and
 *   - large write() calls (default 4 MiB per syscall) instead of tiny
 *     st_blksize-sized writes.
 *
 * The PRNG interface (init/read) and the integrity check that verifies that
 * the PRNG produced non-zero data for the first block are kept intact.
 */
int nwipe_random_forward_pass( NWIPE_METHOD_SIGNATURE )
{
    int r;
    int i = 0;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    off64_t current_offset;
    char* b;
    u64 z = c->device_size; /* bytes remaining */
    u64 bs = 0; /* pass bytes skipped */

    int syncRate = nwipe_options.sync;

    /* Select effective I/O block size (e.g. 4 MiB, never smaller than st_blksize). */
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
        /* Compute the per-write sync rate based on io_blocksize and old semantics. */
        syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );
    }

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

    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 1, "random_pass output buffer" );
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
         * something non-zero into the buffer.
         */
        if( z == c->device_size && !nwipe_prng_is_active( b, blocksize ) )
        {
            nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG returned all zeroes", __FUNCTION__ );
            free( b );
            return -1;
        }

        /* Record the offset we're at before the write. */
        current_offset = (off64_t) ( c->device_size - z );

        /* Write the generated random data to the device. */
        r = (int) nwipe_write_with_retry( c, c->device_fd, b, blocksize );

        if( r < 0 )
        {
            c->pass_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block write failed and the user requested to NOT abort the pass.
                 * Treat the whole block as failed, skip it, count the bytes,
                 * and continue with the next block.
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
                c->prng->read( &c->prng_state, b, rev_blocksize );

                r = (int) nwipe_pwrite_with_retry( c, c->device_fd, b, rev_blocksize, rev_offset );

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
                return -1;
            }
        }

        z -= (u64) r;
        c->pass_done += (u64) r;
        c->round_done += (u64) r;

        nwipe_update_bytes_erased( c, z, bs, 0 );

        /* Periodic fdatasync after 'syncRate' writes, if configured. */
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
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

    } /* /remaining bytes */

    free( b );

    /* Final sync at end of pass. */
    r = nwipe_fdatasync( c, __FUNCTION__ );
    if( r == 0 )
        nwipe_update_bytes_erased( c, z, bs, 1 );
    if( r == -1 )
        return -1;

    return 0;

} /* nwipe_random_forward_pass */

/*
 * nwipe_random_reverse_pass
 *
 * Writes a random pattern to the device using the configured PRNG.
 *
 * This version uses:
 *   - an io_blocksize allocated buffer, zero-initialized to avoid leaking
 *     previous memory content in case of PRNG bugs, and
 *   - large write() calls (default 4 MiB per syscall) instead of tiny
 *     st_blksize-sized writes.
 *
 * The PRNG interface (init/read) and the integrity check that verifies that
 * the PRNG produced non-zero data for the first block are kept intact.
 */
int nwipe_random_reverse_pass( NWIPE_METHOD_SIGNATURE )
{
    int r;
    int i = 0;
    size_t blocksize;
    size_t io_blocksize;
    off64_t current_offset;
    char* b;
    u64 z = c->device_size; /* bytes remaining */
    u64 bs = 0; /* pass bytes skipped */

    int syncRate = nwipe_options.sync;

    /* Select effective I/O block size (e.g. 4 MiB, never smaller than st_blksize). */
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
        /* Compute the per-write sync rate based on io_blocksize and old semantics. */
        syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );
    }

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

    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 1, "random_pass output buffer" );
    if( !b )
        return -1;

    /* Seed the PRNG for this pass. */
    c->prng->init( &c->prng_state, &c->prng_seed );

    /* Reset pass byte counter */
    c->pass_done = 0;

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
         * something non-zero into the buffer.
         */
        if( z == c->device_size && !nwipe_prng_is_active( b, blocksize ) )
        {
            nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG returned all zeroes", __FUNCTION__ );
            free( b );
            return -1;
        }

        /* Record the offset we're at before the write (reverse-adjusted). */
        current_offset = (off64_t) ( z - (u64) blocksize );

        /* Write the generated random data to the device. */
        r = (int) nwipe_pwrite_with_retry( c, c->device_fd, b, blocksize, current_offset );

        if( r < 0 )
        {
            c->pass_errors += 1;

            if( nwipe_options.noabort_block_errors )
            {
                /*
                 * Block write failed and the user requested to NOT abort the pass.
                 * Treat the whole block as failed, skip it, count the bytes,
                 * and continue with the next block.
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
             * Try to touch the bad block from the front (forward wipe).
             * If we meet another bad block along the way, abort the wipe.
             */
            off64_t fwd_offset = 0;
            size_t fwd_blocksize = io_blocksize;

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

                c->prng->read( &c->prng_state, b, fwd_blocksize );

                r = (int) nwipe_pwrite_with_retry( c, c->device_fd, b, fwd_blocksize, fwd_offset );

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

        /* Periodic fdatasync after 'syncRate' writes, if configured. */
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
                    return -1;
                }

                i = 0;
            }
        }

        pthread_testcancel();

    } /* /remaining bytes */

    free( b );

    /* Final sync at end of pass. */
    r = nwipe_fdatasync( c, __FUNCTION__ );
    if( r == 0 )
        nwipe_update_bytes_erased( c, z, bs, 1 );
    if( r == -1 )
        return -1;

    return 0;

} /* nwipe_random_reverse_pass */

/*
 * nwipe_random_forward_verify
 *
 * Verifies that a random pass was correctly written to the device.
 * The PRNG is re-seeded with the stored seed, and the same random byte
 * stream is generated again and compared against what is read from disk.
 *
 * This version uses large I/O blocks (e.g. 4 MiB) instead of tiny
 * st_blksize-sized chunks to reduce syscall overhead and speed up verification.
 */
int nwipe_random_forward_verify( NWIPE_METHOD_SIGNATURE )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t offset;
    off64_t current_offset;
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
    nwipe_fdatasync( c, __FUNCTION__ );

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

        /*
         * For the first block only, verify that the PRNG actually wrote
         * something non-zero into the buffer.
         */
        if( z == c->device_size && !nwipe_prng_is_active( d, blocksize ) )
        {
            nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG returned all zeroes", __FUNCTION__ );
            free( b );
            free( d );
            return -1;
        }

        /* Record the offset we're at before the read. */
        current_offset = (off64_t) ( c->device_size - z );

        /* Read data from device. */
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

        /* Compare the bytes we actually read (r) against the generated pattern. */
        if( memcmp( b, d, (size_t) r ) != 0 )
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

} /* nwipe_random_forward_verify */

/*
 * nwipe_random_reverse_verify
 *
 * Verifies that a random pass was correctly written to the device.
 * The PRNG is re-seeded with the stored seed, and the same random byte
 * stream is generated again and compared against what is read from disk.
 *
 * This version uses large I/O blocks (e.g. 4 MiB) instead of tiny
 * st_blksize-sized chunks to reduce syscall overhead and speed up verification.
 */
int nwipe_random_reverse_verify( NWIPE_METHOD_SIGNATURE )
{
    int r;
    size_t blocksize;
    size_t io_blocksize;
    off64_t current_offset;
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

    /* Reset pass byte counter */
    c->pass_done = 0;

    /* Ensure all previous writes are on disk before we verify. */
    nwipe_fdatasync( c, __FUNCTION__ );

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

        /*
         * For the first block only, verify that the PRNG actually wrote
         * something non-zero into the buffer.
         */
        if( z == c->device_size && !nwipe_prng_is_active( d, blocksize ) )
        {
            nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG returned all zeroes", __FUNCTION__ );
            free( b );
            free( d );
            return -1;
        }

        /* Record the offset we're at before the read (reverse-adjusted). */
        current_offset = (off64_t) ( z - (u64) blocksize );

        /* Read data from device. */
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

        /* Compare the bytes we actually read (r) against the generated pattern. */
        if( memcmp( b, d, (size_t) r ) != 0 )
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

} /* nwipe_random_reverse_verify */
