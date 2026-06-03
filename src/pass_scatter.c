/*
 * pass_scatter.c: Scattered-order device pass (write & verify)
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: Fabian Druschke's original algorithm and implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "pass_internal.h"
#include "splitmix64/splitmix64.h"

#define FNV_OFFSET_BASIS 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

/*
 * Soft target number of scatter segments
 * Controls the region between the hard min/max clamps
 */
#ifndef NWIPE_SCATTER_SEGMENTS
#define NWIPE_SCATTER_SEGMENTS 200000ULL
#endif

/*
 * Soft minimum segment size
 * Prevents small devices from getting too small segments
 * Ensures disk still gets throughput (next to mechanical exercise)
 */
#ifndef NWIPE_SCATTER_SEGMENT_MIN
#define NWIPE_SCATTER_SEGMENT_MIN ( 4ULL * 1024 * 1024 )
#endif

/*
 * Soft maximum segment size
 * Prevents large devices from getting too big segments
 * Ensures disk still gets mechanical exercise (next to throughput)
 */
#ifndef NWIPE_SCATTER_SEGMENT_MAX
#define NWIPE_SCATTER_SEGMENT_MAX ( 16ULL * 1024 * 1024 )
#endif

/*
 * -----------------------------------------------------------------------------
 * Plan-related functions
 * -----------------------------------------------------------------------------
 */

/* Ceiling division of n / d */
static inline u64 ceiling_divide( u64 n, u64 d )
{
    return d ? ( n + d - 1 ) / d : 0;
} /* ceiling_divide */

/* Align value upwards (to next multiple of alignment base) */
static u64 align_up( u64 n, u64 align )
{
    if( align == 0 || n % align == 0 )
        return n;

    return n + align - ( n % align );
} /* align_up */

/* Hash byte_count bytes into the running hash */
static u64 fnv1a_hash_bytes( u64 hash, const void* data, size_t byte_count )
{
    const unsigned char* bytes = (const unsigned char*) data;

    for( size_t idx = 0; idx < byte_count; idx++ )
        hash = ( hash ^ bytes[idx] ) * FNV_PRIME;

    return hash;
} /* fnv1a_hash_bytes */

/* Produce a pass-unique seed from a device context */
static u64 seed_from_context( const nwipe_context_t* c )
{
    u64 hash = FNV_OFFSET_BASIS;

    /* Hash these context properties so every pass has a unique seed */
    hash = fnv1a_hash_bytes( hash, &c->device_size, sizeof( c->device_size ) );
    hash = fnv1a_hash_bytes( hash, &c->round_working, sizeof( c->round_working ) );
    hash = fnv1a_hash_bytes( hash, &c->pass_working, sizeof( c->pass_working ) );
    hash = fnv1a_hash_bytes( hash, &c->round_count, sizeof( c->round_count ) );
    hash = fnv1a_hash_bytes( hash, &c->pass_count, sizeof( c->pass_count ) );

    /* Also hash the PRNG seed into the hash if it's a random pass */
    if( c->prng_seed.s && c->prng_seed.length > 0 )
        hash = fnv1a_hash_bytes( hash, c->prng_seed.s, c->prng_seed.length );

    return hash;
} /* seed_from_context */

/* Produce a pass-unique seed from a device context and pattern */
static u64 seed_from_context_and_pattern( const nwipe_context_t* c, const nwipe_pattern_t* p )
{
    u64 hash = seed_from_context( c );

    hash = fnv1a_hash_bytes( hash, &p->length, sizeof( p->length ) );

    if( p->s && p->length > 0 ) /* Just in case */
        hash = fnv1a_hash_bytes( hash, p->s, (size_t) p->length );

    return hash;
} /* seed_from_context_and_pattern */

/* Build a scatter plan given a seed, device size and I/O block size */
static int plan_build( u64 seed, u64 device_size, size_t io_block_size, nwipe_scatter_plan_t* plan )
{
    splitmix64_state_t rng;
    u64 segment_size;
    u64 total_segments;
    u64 half_count;
    u64 write_cursor;
    u64 first_half_cursor;
    u64 second_half_cursor;
    u64 pair_index;
    u64 num_pairs;

    memset( plan, 0, sizeof( *plan ) ); /* Zero it */

    /* Find out how big each segment is (given wanted amount of segments) */
    segment_size = ceiling_divide( device_size, NWIPE_SCATTER_SEGMENTS );

    /* Clamp to minimum segment size if segment size is smaller */
    if( segment_size < NWIPE_SCATTER_SEGMENT_MIN )
        segment_size = NWIPE_SCATTER_SEGMENT_MIN;

    /* Clamp to I/O block size if segment size is smaller than it */
    if( segment_size < (u64) io_block_size )
        segment_size = (u64) io_block_size;

    /* Clamp to maximum segment size if segment size is larger than it */
    if( segment_size > NWIPE_SCATTER_SEGMENT_MAX )
        segment_size = NWIPE_SCATTER_SEGMENT_MAX;

    /* Align segment size to I/O block size (to pass direct I/O) */
    segment_size = align_up( segment_size, (u64) io_block_size );

    /* Clamp to device size if segment exceeds it (small devices) */
    if( segment_size > device_size )
        segment_size = device_size;

    /* Just in case, shouldn't happen here */
    if( segment_size == 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Segment size is zero", __FUNCTION__ );
        return -1;
    }

    /* Calculate number of needed segments given each segment size */
    total_segments = ceiling_divide( device_size, segment_size );

    /* Just in case, shouldn't happen here */
    if( total_segments == 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Segment count is zero", __FUNCTION__ );
        return -1;
    }

    plan->segment_size = segment_size;
    plan->segment_count = total_segments;

    /* Allocate an u64 for each segment - this is for the indices */
    plan->visit_order = malloc( (size_t) total_segments * sizeof( *plan->visit_order ) );
    if( !plan->visit_order )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to allocate visiting order", __FUNCTION__ );
        return -1;
    }

    half_count = total_segments / 2;
    first_half_cursor = 0; /* 0 - 50% of device */
    second_half_cursor = half_count; /* 50 - 100% of device */
    write_cursor = 0;

    /* Interleave the segments so we produce long jumps on the device */
    while( first_half_cursor < half_count || second_half_cursor < total_segments )
    {
        if( first_half_cursor < half_count ) /* Pick one from first half */
            plan->visit_order[write_cursor++] = first_half_cursor++;
        if( second_half_cursor < total_segments ) /* Pick one from second half */
            plan->visit_order[write_cursor++] = second_half_cursor++;
    }

    /* Get PRNG set up for shuffling (swapping) the segments using seed */
    if( splitmix64_prng_init( &rng, (const uint8_t*) &seed, sizeof( seed ) ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to init SplitMix64 PRNG", __FUNCTION__ );
        free( plan->visit_order );
        plan->visit_order = NULL;
        return -1;
    }

    /*
     * Shuffle in pairs (to keep the long jumps in place)
     * Each pair is one from first device half, one from second device half
     */
    num_pairs = total_segments / 2;

    if( num_pairs > 1 )
    {
        /* Iterate over all pairs and swap them around */
        for( pair_index = num_pairs - 1; pair_index > 0; pair_index-- )
        {
            u64 random_value;
            u64 swap_target;

            /* Get a random number we can map to a position in the array */
            splitmix64_prng_genrand_to_buf( &rng, (uint8_t*) &random_value, sizeof( random_value ) );

            /* Check if the internal PRNG did not just return zeroes */
            if( pair_index == num_pairs - 1
                && !nwipe_prng_is_active( (const char*) &random_value, sizeof( random_value ) ) )
            {
                nwipe_log( NWIPE_LOG_SANITY, "%s: SplitMix64 produced all zeroes", __FUNCTION__ );
                free( plan->visit_order );
                plan->visit_order = NULL;
                return -1;
            }

            /* Map the random number to a pair index */
            swap_target = random_value % ( pair_index + 1 );

            /* We don't want to swap with ourselves */
            if( swap_target != pair_index )
            {
                u64 current_pair_first = pair_index * 2; /* First element of source pair */
                u64 target_pair_first = swap_target * 2; /* First element of target pair */
                u64 temp;

                /* Swap the first element of each pair. */
                temp = plan->visit_order[current_pair_first];
                plan->visit_order[current_pair_first] = plan->visit_order[target_pair_first];
                plan->visit_order[target_pair_first] = temp;

                /* Swap the second element of each pair (if there is one). */
                if( current_pair_first + 1 < total_segments && target_pair_first + 1 < total_segments )
                {
                    temp = plan->visit_order[current_pair_first + 1];
                    plan->visit_order[current_pair_first + 1] = plan->visit_order[target_pair_first + 1];
                    plan->visit_order[target_pair_first + 1] = temp;
                }
            }
        }
    }

    return 0;
} /* plan_build */

/* Releases the allocated memory and zeros the plan struct */
static void plan_free( nwipe_scatter_plan_t* plan )
{
    free( plan->visit_order );
    memset( plan, 0, sizeof( *plan ) );
} /* plan_free */

/* Get the offset and amount of bytes for a given visiting index */
static void get_segment_range( const nwipe_scatter_plan_t* plan,
                               u64 visit_index,
                               u64 device_size,
                               off64_t* offset_out,
                               size_t* length_out )
{
    u64 segment_index = plan->visit_order[visit_index];
    u64 byte_offset = segment_index * plan->segment_size;
    u64 bytes_left = device_size - byte_offset;

    /* Starting offset of the segment */
    *offset_out = (off64_t) byte_offset;

    /* Use full segment size or whatever bytes left if it's the last (smaller) segment. */
    *length_out = (size_t) ( bytes_left < plan->segment_size ? bytes_left : plan->segment_size );

} /* get_segment_range */

/*
 * -----------------------------------------------------------------------------
 * I/O functions
 * -----------------------------------------------------------------------------
 */

/* This is the filling callback we use for PRNG */
static int fill_prng( nwipe_context_t* c, char* buffer, size_t length, off64_t device_offset, void* opaque )
{
    (void) device_offset; /* not needed */
    (void) opaque; /* not needed */

    /* Read PRNG into buffer */
    c->prng->read( &c->prng_state, buffer, length );

    return 0;
} /* fill_prng */

/* This is the filling callback we use for static patterns */
static int fill_pattern( nwipe_context_t* c, char* buffer, size_t length, off64_t device_offset, void* opaque )
{
    (void) c; /* not needed */
    nwipe_scatter_patt_ctx_t* fill_ctx = (nwipe_scatter_patt_ctx_t*) opaque;

    /* Window into the pattern for the requested offset */
    int w = (int) ( device_offset % (off64_t) fill_ctx->pattern_length );

    /* Buffer is guaranteed aligned and large enough for our use */
    memcpy( buffer, &fill_ctx->pattern_buffer[w], length );

    return 0;
} /* fill_pattern */

/* Generic I/O writing function taking a filling callback */
static int scatter_write( nwipe_context_t* c,
                          const nwipe_scatter_plan_t* plan,
                          size_t io_blocksize,
                          nwipe_scatter_fill_fn fill,
                          void* fill_context )
{
    int r;
    int i = 0;
    int is_first_block = 1;
    u64 z = c->device_size;
    u64 bs = 0;
    u64 visit_index;
    char* b;
    int syncRate;

    if( c->io_mode == NWIPE_IO_MODE_DIRECT ) /* for direct I/O */
    {
        syncRate = 0;
        nwipe_log( NWIPE_LOG_NOTICE, "Disabled fdatasync for %s, DirectI/O in use.", c->device_name );
    }
    else /* for cached I/O */
    {
        syncRate = nwipe_compute_sync_rate_for_device( c, io_blocksize );
    }

    /* Allocate buffer for writing */
    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 1, "scatter_write" );
    if( !b )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to allocate write buffer", __FUNCTION__ );
        return -1;
    }

    /* Reset pass byte counter */
    c->pass_done = 0;

    /* Iterate over the entire visiting array */
    for( visit_index = 0; visit_index < plan->segment_count; visit_index++ )
    {
        off64_t segment_offset; /* Byte offset where segment starts */
        size_t segment_length; /* Byte length of segment */
        size_t segment_bytes_done = 0; /* Bytes written within segment */

        /* Populate the offset and length with the segment information */
        get_segment_range( plan, visit_index, c->device_size, &segment_offset, &segment_length );

        /* Write the segment in I/O-block-sized chunks. */
        while( segment_bytes_done < segment_length )
        {
            off64_t current_offset = segment_offset + (off64_t) segment_bytes_done;
            size_t blocksize = io_blocksize;

            /* Last block of the segment may be smaller than an I/O block */
            if( blocksize > segment_length - segment_bytes_done )
                blocksize = segment_length - segment_bytes_done;

            /* Ask callback for blocksize bytes of data into our buffer */
            if( fill( c, b, blocksize, current_offset, fill_context ) )
            {
                nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to fill write buffer", __FUNCTION__ );
                free( b );
                return -1;
            }

            /* Check if PRNG is active (if it's the PRNG callback) */
            if( is_first_block )
            {
                if( fill == fill_prng && !nwipe_prng_is_active( b, blocksize ) )
                {
                    nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG produced all zeroes", __FUNCTION__ );
                    free( b );
                    return -1;
                }
                is_first_block = 0;
            }

            /* Write at the calculated offset */
            r = (int) nwipe_pwrite_with_retry( c, c->device_fd, b, blocksize, current_offset );

            if( r < 0 ) /* Write failure */
            {
                c->pass_errors++;

                if( nwipe_options.noabort_block_errors )
                {
                    nwipe_perror( errno, __FUNCTION__, "pwrite" );
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Write error on '%s' at offset %lld, skipping %zu bytes.",
                               c->device_name,
                               (long long) current_offset,
                               blocksize );

                    /* Logically count the skipped bytes */
                    z -= (u64) blocksize;
                    bs += (u64) blocksize;
                    c->round_done += (u64) blocksize;
                    segment_bytes_done += blocksize;

                    pthread_testcancel();
                    continue;
                }

                /* Default error case (abort wipe) */
                nwipe_perror( errno, __FUNCTION__, "pwrite" );
                nwipe_log( NWIPE_LOG_FATAL,
                           "Write failed on '%s' at offset %lld.",
                           c->device_name,
                           (long long) current_offset );
                free( b );
                return -1;
            }

            if( r != (int) blocksize ) /* Write short */
            {
                int s = (int) blocksize - r;

                /* Increase error count since we skipped bytes */
                c->pass_errors++;

                nwipe_log( NWIPE_LOG_ERROR,
                           "Short write on '%s' at offset %lld, %i bytes short.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                /* Logically count the skipped bytes */
                z -= (u64) s;
                bs += (u64) s;
                c->round_done += (u64) s;
                segment_bytes_done += (size_t) s;
            }

            /* Count the actually out written bytes */
            z -= (u64) r;
            c->pass_done += (u64) r;
            c->round_done += (u64) r;
            segment_bytes_done += (size_t) r;

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
                        return -1;
                    }

                    i = 0;
                }
            }

            pthread_testcancel();
        } /* while (in a segment) */

        pthread_testcancel();
    } /* for (each segment) */

    free( b );

    /* Final sync at end of pass. */
    r = nwipe_fdatasync( c, __FUNCTION__ );
    if( r == 0 )
        nwipe_update_bytes_erased( c, z, bs, 1 );
    if( r == -1 )
        return -1;

    return 0;
} /* scatter_write */

/* Generic I/O verification function taking a filling callback */
static int scatter_verify( nwipe_context_t* c,
                           const nwipe_scatter_plan_t* plan,
                           size_t io_blocksize,
                           nwipe_scatter_fill_fn fill,
                           void* fill_context )
{
    int r;
    int is_first_block = 1;
    u64 z = c->device_size;
    u64 visit_index;
    char* b;
    char* d;

    /* Allocate read buffer (what we read from disk) */
    b = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "scatter_vfy_rd" );
    if( !b )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to allocate read buffer", __FUNCTION__ );
        return -1;
    }

    /* Allocate pattern buffer (what we want to compare against) */
    d = (char*) nwipe_alloc_io_buffer( c, io_blocksize, 0, "scatter_vfy_pat" );
    if( !d )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to allocate pattern buffer", __FUNCTION__ );
        free( b );
        return -1;
    }

    /* Reset pass byte counter */
    c->pass_done = 0;

    /* Flush any pending writes to disk before starting */
    nwipe_fdatasync( c, __FUNCTION__ );

    /* Iterate over the entire visiting array */
    for( visit_index = 0; visit_index < plan->segment_count; visit_index++ )
    {
        off64_t segment_offset; /* Byte offset where segment starts */
        size_t segment_length; /* Byte length of segment */
        size_t segment_bytes_done = 0; /* Bytes verified within segment */

        /* Populate the offset and length with the segment information */
        get_segment_range( plan, visit_index, c->device_size, &segment_offset, &segment_length );

        /* Read the segment in I/O-sized blocks */
        while( segment_bytes_done < segment_length )
        {
            off64_t current_offset = segment_offset + (off64_t) segment_bytes_done;
            size_t blocksize = io_blocksize;

            /* Last block of the segment may be smaller than an I/O block */
            if( blocksize > segment_length - segment_bytes_done )
                blocksize = segment_length - segment_bytes_done;

            /* Generate the expected data using our callback function */
            if( fill( c, d, blocksize, current_offset, fill_context ) )
            {
                nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to fill pattern buffer", __FUNCTION__ );
                free( b );
                free( d );
                return -1;
            }

            /* Check if PRNG is active (if it's the PRNG callback) */
            if( is_first_block )
            {
                if( fill == fill_prng && !nwipe_prng_is_active( d, blocksize ) )
                {
                    nwipe_log( NWIPE_LOG_SANITY, "%s: PRNG produced all zeroes", __FUNCTION__ );
                    free( b );
                    free( d );
                    return -1;
                }
                is_first_block = 0;
            }

            /* Read at the calculated offset */
            r = (int) nwipe_pread_with_retry( c, c->device_fd, b, blocksize, current_offset );

            if( r < 0 ) /* Read failure */
            {
                c->verify_errors++;

                if( nwipe_options.noabort_block_errors )
                {
                    nwipe_perror( errno, __FUNCTION__, "pread" );
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Read error on '%s' at offset %lld, skipping %zu bytes.",
                               c->device_name,
                               (long long) current_offset,
                               blocksize );

                    /* Logically count the skipped bytes */
                    z -= (u64) blocksize;
                    c->round_done += (u64) blocksize;
                    segment_bytes_done += blocksize;

                    pthread_testcancel();
                    continue;
                }

                /* Default error case (abort verification) */
                nwipe_perror( errno, __FUNCTION__, "pread" );
                nwipe_log(
                    NWIPE_LOG_FATAL, "Read error on '%s' at offset %lld.", c->device_name, (long long) current_offset );
                free( b );
                free( d );
                return -1;
            }

            if( r != (int) blocksize ) /* Read short */
            {
                int s = (int) blocksize - r;

                /* Increase verify errors since we skipped bytes */
                c->verify_errors++;

                nwipe_log( NWIPE_LOG_ERROR,
                           "Short read on '%s' at offset %lld, %i bytes short.",
                           c->device_name,
                           (long long) current_offset,
                           s );

                z -= (u64) s;
                c->round_done += (u64) s;
                segment_bytes_done += (size_t) s;
            }

            /* Compare the bytes we did actually read */
            if( memcmp( b, d, (size_t) r ) != 0 )
            {
                c->verify_errors++;

                /* Abort verification unless noabort_block_errors is enabled */
                if( !nwipe_options.noabort_block_errors )
                {
                    nwipe_log( NWIPE_LOG_FATAL,
                               "Verification mismatch on '%s' at offset %lld",
                               c->device_name,
                               (long long) current_offset );
                    free( b );
                    free( d );
                    return -1;
                }
            }

            /* Count the actually read bytes */
            z -= (u64) r;
            c->pass_done += (u64) r;
            c->round_done += (u64) r;
            segment_bytes_done += (size_t) r;

            pthread_testcancel();
        } /* while (in a segment) */

        pthread_testcancel();
    } /* for (each segment) */

    free( b );
    free( d );

    return 0;
} /* scatter_verify */

/*
 * -----------------------------------------------------------------------------
 * Helper functions
 * -----------------------------------------------------------------------------
 */

/* Self-tests the scatter seeding mechanism (0 = success, -1 = failure) */
static int self_test_scatter_seed()
{
    nwipe_context_t ctx1, ctx2;
    nwipe_pattern_t patt1;
    u64 hash_base, hash_alt;

    memset( &ctx1, 0, sizeof( ctx1 ) ); /* Zero it */
    ctx1.device_size = 123456789ULL;
    ctx1.round_working = 1;
    ctx1.pass_working = 1;

    hash_base = seed_from_context( &ctx1 );

    if( hash_base != seed_from_context( &ctx1 ) )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Scatter seed is non-deterministic.", __FUNCTION__ );
        return -1;
    }

    ctx2 = ctx1;
    ctx2.pass_working = 2;

    if( hash_base == seed_from_context( &ctx2 ) )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Scatter seed is same after pass changed.", __FUNCTION__ );
        return -1;
    }

    memset( &patt1, 0, sizeof( patt1 ) ); /* Zero it */
    patt1.s = (char*) "\x92\x49\x24"; /* Borrowed from Gutmann... ;-) */
    patt1.length = 3;

    hash_base = seed_from_context_and_pattern( &ctx1, &patt1 );

    if( hash_base != seed_from_context_and_pattern( &ctx1, &patt1 ) )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Scatter seed is non-deterministic (with pattern).", __FUNCTION__ );
        return -1;
    }

    ctx2 = ctx1;
    ctx2.device_size = 123456780ULL;

    hash_alt = seed_from_context_and_pattern( &ctx2, &patt1 );

    if( hash_base == hash_alt )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Scatter seed is same after device size changed.", __FUNCTION__ );
        return -1;
    }

    return 0;
} /* self_test_scatter_seed */

/* Logs the plan parameters so the user knows what values it's using. */
static void log_plan( const nwipe_context_t* c, const nwipe_scatter_plan_t* plan )
{
    nwipe_log( NWIPE_LOG_NOTICE,
               "Scatter plan for '%s': segment=%llu, count=%llu.",
               c->device_name,
               (unsigned long long) plan->segment_size,
               (unsigned long long) plan->segment_count );
} /* log_plan */

/*
 * Pre-build a given repeating pattern buffer.
 * Internally allocates the fill_ctx->pattern_buffer.
 * Caller must make sure to free() it when done with it.
 */
static int init_pattern_fill_ctx( const nwipe_context_t* c,
                                  const nwipe_pattern_t* pattern,
                                  size_t io_block_size,
                                  nwipe_scatter_patt_ctx_t* fill_ctx )
{
    char* stamp_cursor;

    fill_ctx->pattern_length = pattern->length;

    /*
     * Allocate enough memory for the pattern buffer
     * Each window into the buffer must offer at least one io_block_size
     */
    fill_ctx->pattern_buffer =
        (char*) nwipe_alloc_io_buffer( c, io_block_size + (size_t) pattern->length * 2, 0, "scatter_pat" );
    if( !fill_ctx->pattern_buffer )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to allocate pattern buffer", __FUNCTION__ );
        return -1;
    }

    /* Stamp pattern into the entire buffer so any window into it is valid */
    for( stamp_cursor = fill_ctx->pattern_buffer;
         stamp_cursor < fill_ctx->pattern_buffer + io_block_size + pattern->length;
         stamp_cursor += pattern->length )
    {
        memcpy( stamp_cursor, pattern->s, (size_t) pattern->length );
    }

    return 0;
} /* init_pattern_fill_ctx */

/*
 * -----------------------------------------------------------------------------
 * Public functions
 * -----------------------------------------------------------------------------
 */

int nwipe_random_scatter_pass( NWIPE_METHOD_SIGNATURE )
{
    nwipe_scatter_plan_t plan;
    size_t io_block_size = c->device_io_block_size;
    int r;

    if( !c->prng_seed.s || c->prng_seed.length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad PRNG seed", __FUNCTION__ );
        return -1;
    }

    /* Self test the seeding mechanism */
    if( self_test_scatter_seed() != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad scatter seeding", __FUNCTION__ );
        return -1;
    }

    /* Build the scattering plan */
    if( plan_build( seed_from_context( c ), c->device_size, io_block_size, &plan ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build scatter plan", __FUNCTION__ );
        return -1;
    }

    log_plan( c, &plan );

    /* Seed the wipe PRNG */
    c->prng->init( &c->prng_state, &c->prng_seed );

    /* Write using the PRNG filling callback. */
    r = scatter_write( c, &plan, io_block_size, fill_prng, NULL );

    plan_free( &plan );

    return r;
} /* nwipe_random_scatter_pass */

int nwipe_random_scatter_verify( NWIPE_METHOD_SIGNATURE )
{
    nwipe_scatter_plan_t plan;
    size_t io_block_size = c->device_io_block_size;
    int r;

    if( !c->prng_seed.s || c->prng_seed.length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad PRNG seed", __FUNCTION__ );
        return -1;
    }

    /* Self test the seeding mechanism */
    if( self_test_scatter_seed() != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad scatter seeding", __FUNCTION__ );
        return -1;
    }

    /* Build the scattering plan (same one as write pass) */
    if( plan_build( seed_from_context( c ), c->device_size, io_block_size, &plan ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build scatter plan", __FUNCTION__ );
        return -1;
    }

    log_plan( c, &plan );

    /* Re-seed the PRNG to starting state (same one as write pass) */
    c->prng->init( &c->prng_state, &c->prng_seed );

    /* Read and compare using the PRNG filling callback. */
    r = scatter_verify( c, &plan, io_block_size, fill_prng, NULL );

    plan_free( &plan );

    return r;
} /* nwipe_random_scatter_verify */

int nwipe_static_scatter_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    nwipe_scatter_plan_t plan;
    nwipe_scatter_patt_ctx_t fill_ctx;
    size_t io_block_size = c->device_io_block_size;
    int r;

    if( !pattern || pattern->length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad static pattern", __FUNCTION__ );
        return -1;
    }

    /* Self test the seeding mechanism */
    if( self_test_scatter_seed() != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad scatter seeding", __FUNCTION__ );
        return -1;
    }

    /* Build the scattering plan */
    if( plan_build( seed_from_context_and_pattern( c, pattern ), c->device_size, io_block_size, &plan ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build scatter plan", __FUNCTION__ );
        return -1;
    }

    log_plan( c, &plan );

    /* Build the repeating pattern buffer */
    if( init_pattern_fill_ctx( c, pattern, io_block_size, &fill_ctx ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build pattern buffer", __FUNCTION__ );
        plan_free( &plan );
        return -1;
    }

    /* Write using the static filling callback. */
    r = scatter_write( c, &plan, io_block_size, fill_pattern, &fill_ctx );

    free( fill_ctx.pattern_buffer );
    plan_free( &plan );

    return r;
} /* nwipe_static_scatter_pass */

int nwipe_static_scatter_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
    nwipe_scatter_plan_t plan;
    nwipe_scatter_patt_ctx_t fill_ctx;
    size_t io_block_size = c->device_io_block_size;
    int r;

    if( !pattern || pattern->length <= 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad static pattern", __FUNCTION__ );
        return -1;
    }

    /* Self test the seeding mechanism */
    if( self_test_scatter_seed() != 0 )
    {
        nwipe_log( NWIPE_LOG_SANITY, "%s: Bad scatter seeding", __FUNCTION__ );
        return -1;
    }

    /* Build the scattering plan (same one as write pass) */
    if( plan_build( seed_from_context_and_pattern( c, pattern ), c->device_size, io_block_size, &plan ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build scatter plan", __FUNCTION__ );
        return -1;
    }

    log_plan( c, &plan );

    /* Build the repeating pattern buffer (same one as write pass) */
    if( init_pattern_fill_ctx( c, pattern, io_block_size, &fill_ctx ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "%s: Failed to build pattern buffer", __FUNCTION__ );
        plan_free( &plan );
        return -1;
    }

    /* Read and compare using the static filling callback. */
    r = scatter_verify( c, &plan, io_block_size, fill_pattern, &fill_ctx );

    free( fill_ctx.pattern_buffer );
    plan_free( &plan );

    return r;
} /* nwipe_static_scatter_verify */
