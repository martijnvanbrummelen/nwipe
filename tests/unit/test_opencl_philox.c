#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "context.h"
#include "logging.h"
#include "opencl/opencl_philox_prng.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define OPENCL_TEST_THREADS 4

static const unsigned char expected_first_block[SIZE_OF_OPENCL_PHILOX_PRNG] =
    { 0xd5, 0xe8, 0x27, 0x66, 0x8d, 0xc5, 0x69, 0xe1, 0x4c, 0xac, 0x57, 0xbc, 0xd8, 0xdb, 0x00, 0x9b };

typedef struct
{
    int available;
    int failed;
} opencl_thread_result_t;

/* Keep the backend test independent from nwipe's application-wide logger. */
void nwipe_log( nwipe_log_t level, const char* format, ... )
{
    (void) level;
    (void) format;
}

static void* test_opencl_from_thread( void* opaque )
{
    opencl_thread_result_t* result = (opencl_thread_result_t*) opaque;
    unsigned char seed_bytes[16] = { 0 };
    unsigned char output[SIZE_OF_OPENCL_PHILOX_PRNG];
    nwipe_entropy_t seed = { .length = sizeof( seed_bytes ), .s = seed_bytes };
    void* state = NULL;

    result->available = nwipe_opencl_philox_prng_available();
    if( result->available
        && ( nwipe_opencl_philox_prng_init( &state, &seed ) != 0
             || nwipe_opencl_philox_prng_read( &state, output, sizeof( output ) ) != 0
             || memcmp( output, expected_first_block, sizeof( output ) ) != 0 ) )
    {
        result->failed = 1;
    }
    nwipe_opencl_philox_prng_free( &state );
    return NULL;
}

int main( void )
{
    pthread_t threads[OPENCL_TEST_THREADS];
    opencl_thread_result_t thread_results[OPENCL_TEST_THREADS] = { 0 };
    unsigned char seed_bytes[16] = { 0 };
    unsigned char split_output[64];
    unsigned char whole_output[64];
    nwipe_entropy_t seed = { .length = sizeof( seed_bytes ), .s = seed_bytes };
    void* state = NULL;
    int available_threads = 0;

    for( int i = 0; i < OPENCL_TEST_THREADS; ++i )
    {
        if( pthread_create( &threads[i], NULL, test_opencl_from_thread, &thread_results[i] ) != 0 )
        {
            fprintf( stderr, "Unable to create OpenCL test thread.\n" );
            return 1;
        }
    }
    for( int i = 0; i < OPENCL_TEST_THREADS; ++i )
    {
        pthread_join( threads[i], NULL );
        available_threads += thread_results[i].available;
        if( thread_results[i].failed )
        {
            fprintf( stderr, "Concurrent OpenCL Philox initialization or generation failed.\n" );
            return 1;
        }
    }

    if( available_threads == 0 )
    {
        fprintf( stderr, "SKIP: %s\n", nwipe_opencl_philox_prng_status() );
        return 77;
    }
    if( available_threads != OPENCL_TEST_THREADS )
    {
        fprintf( stderr, "OpenCL availability was inconsistent across threads.\n" );
        return 1;
    }

    if( nwipe_opencl_philox_prng_init( &state, &seed ) != 0
        || nwipe_opencl_philox_prng_read( &state, split_output, 7 ) != 0
        || nwipe_opencl_philox_prng_read( &state, split_output + 7, sizeof( split_output ) - 7 ) != 0 )
    {
        fprintf( stderr, "OpenCL Philox initialization or split read failed.\n" );
        nwipe_opencl_philox_prng_free( &state );
        return 1;
    }

    if( memcmp( split_output, expected_first_block, sizeof( expected_first_block ) ) != 0 )
    {
        fprintf( stderr, "OpenCL Philox did not produce the known zero-seed vector.\n" );
        nwipe_opencl_philox_prng_free( &state );
        return 1;
    }

    if( nwipe_opencl_philox_prng_init( &state, &seed ) != 0
        || nwipe_opencl_philox_prng_read( &state, whole_output, sizeof( whole_output ) ) != 0
        || memcmp( split_output, whole_output, sizeof( whole_output ) ) != 0 )
    {
        fprintf( stderr, "OpenCL Philox output changed across read boundaries or reinitialization.\n" );
        nwipe_opencl_philox_prng_free( &state );
        return 1;
    }

    nwipe_opencl_philox_prng_free( &state );
    return 0;
}
