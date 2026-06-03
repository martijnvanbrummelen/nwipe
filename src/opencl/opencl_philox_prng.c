#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "../nwipe.h"
#include "../context.h"
#include "../logging.h"
#include "../prng.h"
#include "opencl_philox_prng.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_OPENCL
#ifndef CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#endif
#if defined( HAVE_OPENCL_OPENCL_H )
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#ifndef CL_PLATFORM_NOT_FOUND_KHR
#define CL_PLATFORM_NOT_FOUND_KHR -1001
#endif
#endif

typedef struct nwipe_opencl_philox_state_s
{
#ifdef HAVE_OPENCL
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem output_buffer;
    size_t output_capacity;
#endif
    uint32_t key[2];
    uint32_t nonce[2];
    uint64_t block_counter;
    unsigned char tail[SIZE_OF_OPENCL_PHILOX_PRNG];
    size_t tail_offset;
    size_t tail_length;
} nwipe_opencl_philox_state_t;

#ifdef HAVE_OPENCL
#define NWIPE_OPENCL_DEVICE_BENCH_BYTES ( 8u * 1024u * 1024u )
#define NWIPE_OPENCL_DEVICE_BENCH_ROUNDS 2

typedef struct nwipe_opencl_candidate_s
{
    cl_platform_id platform;
    cl_device_id device;
    cl_device_type type;
    cl_uint compute_units;
    cl_uint clock_mhz;
    double mbps;
    char name[128];
} nwipe_opencl_candidate_t;

static const char* nwipe_opencl_philox_kernel_source =
    "__kernel void philox4x32_10(__global uchar *out, ulong block_base, uint key0, uint key1, uint nonce0, uint "
    "nonce1)\n"
    "{\n"
    "    const uint M0 = 0xD2511F53U;\n"
    "    const uint M1 = 0xCD9E8D57U;\n"
    "    const uint W0 = 0x9E3779B9U;\n"
    "    const uint W1 = 0xBB67AE85U;\n"
    "    size_t gid = get_global_id(0);\n"
    "    ulong block = block_base + (ulong) gid;\n"
    "    uint4 ctr = (uint4)((uint) block, (uint) (block >> 32), nonce0, nonce1);\n"
    "    uint2 key = (uint2)(key0, key1);\n"
    "    for(int round = 0; round < 10; ++round)\n"
    "    {\n"
    "        uint hi0 = mul_hi(ctr.x, M0);\n"
    "        uint lo0 = ctr.x * M0;\n"
    "        uint hi1 = mul_hi(ctr.z, M1);\n"
    "        uint lo1 = ctr.z * M1;\n"
    "        ctr = (uint4)(hi1 ^ ctr.y ^ key.x, lo1, hi0 ^ ctr.w ^ key.y, lo0);\n"
    "        key += (uint2)(W0, W1);\n"
    "    }\n"
    "    size_t base = gid * 16;\n"
    "    uint v0 = ctr.x;\n"
    "    uint v1 = ctr.y;\n"
    "    uint v2 = ctr.z;\n"
    "    uint v3 = ctr.w;\n"
    "    out[base + 0] = (uchar)(v0 & 0xFFU);\n"
    "    out[base + 1] = (uchar)((v0 >> 8) & 0xFFU);\n"
    "    out[base + 2] = (uchar)((v0 >> 16) & 0xFFU);\n"
    "    out[base + 3] = (uchar)((v0 >> 24) & 0xFFU);\n"
    "    out[base + 4] = (uchar)(v1 & 0xFFU);\n"
    "    out[base + 5] = (uchar)((v1 >> 8) & 0xFFU);\n"
    "    out[base + 6] = (uchar)((v1 >> 16) & 0xFFU);\n"
    "    out[base + 7] = (uchar)((v1 >> 24) & 0xFFU);\n"
    "    out[base + 8] = (uchar)(v2 & 0xFFU);\n"
    "    out[base + 9] = (uchar)((v2 >> 8) & 0xFFU);\n"
    "    out[base + 10] = (uchar)((v2 >> 16) & 0xFFU);\n"
    "    out[base + 11] = (uchar)((v2 >> 24) & 0xFFU);\n"
    "    out[base + 12] = (uchar)(v3 & 0xFFU);\n"
    "    out[base + 13] = (uchar)((v3 >> 8) & 0xFFU);\n"
    "    out[base + 14] = (uchar)((v3 >> 16) & 0xFFU);\n"
    "    out[base + 15] = (uchar)((v3 >> 24) & 0xFFU);\n"
    "}\n";

static uint32_t nwipe_load_le32( const unsigned char* src )
{
    return (uint32_t) src[0] | ( (uint32_t) src[1] << 8 ) | ( (uint32_t) src[2] << 16 ) | ( (uint32_t) src[3] << 24 );
}

static void nwipe_seed_to_key_nonce( nwipe_entropy_t* seed, uint32_t key[2], uint32_t nonce[2] )
{
    unsigned char folded[16];
    size_t i;

    memset( folded, 0, sizeof( folded ) );

    if( seed && seed->s && seed->length > 0 )
    {
        for( i = 0; i < seed->length; ++i )
        {
            folded[i % sizeof( folded )] ^= seed->s[i];
        }
    }

    key[0] = nwipe_load_le32( folded + 0 );
    key[1] = nwipe_load_le32( folded + 4 );
    nonce[0] = nwipe_load_le32( folded + 8 );
    nonce[1] = nwipe_load_le32( folded + 12 );
}

static double nwipe_opencl_monotonic_seconds( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
}

static void nwipe_opencl_device_name( cl_device_id device, char* out, size_t out_size )
{
    if( out_size == 0 )
    {
        return;
    }

    out[0] = '\0';
    if( clGetDeviceInfo( device, CL_DEVICE_NAME, out_size, out, NULL ) != CL_SUCCESS || out[0] == '\0' )
    {
        snprintf( out, out_size, "unknown OpenCL device" );
    }
    out[out_size - 1] = '\0';
}

static void nwipe_opencl_release_state( nwipe_opencl_philox_state_t* state )
{
    if( !state )
    {
        return;
    }

    if( state->output_buffer )
    {
        clReleaseMemObject( state->output_buffer );
        state->output_buffer = NULL;
    }
    if( state->kernel )
    {
        clReleaseKernel( state->kernel );
        state->kernel = NULL;
    }
    if( state->program )
    {
        clReleaseProgram( state->program );
        state->program = NULL;
    }
    if( state->queue )
    {
        clReleaseCommandQueue( state->queue );
        state->queue = NULL;
    }
    if( state->context )
    {
        clReleaseContext( state->context );
        state->context = NULL;
    }

    state->platform = NULL;
    state->device = NULL;
    state->output_capacity = 0;
}

static int nwipe_opencl_collect_gpu_devices( nwipe_opencl_candidate_t** out_candidates, size_t* out_count )
{
    cl_int err;
    cl_uint platform_count = 0;
    cl_platform_id* platforms = NULL;
    nwipe_opencl_candidate_t* candidates = NULL;
    size_t candidate_count = 0;
    int rc = -1;

    *out_candidates = NULL;
    *out_count = 0;

    err = clGetPlatformIDs( 0, NULL, &platform_count );
    if( err == CL_PLATFORM_NOT_FOUND_KHR || platform_count == 0 )
    {
        return -1;
    }
    if( err != CL_SUCCESS )
    {
        return -1;
    }

    platforms = (cl_platform_id*) calloc( platform_count, sizeof( cl_platform_id ) );
    if( !platforms )
    {
        return -1;
    }

    err = clGetPlatformIDs( platform_count, platforms, NULL );
    if( err != CL_SUCCESS )
    {
        goto out;
    }

    for( cl_uint i = 0; i < platform_count; ++i )
    {
        const cl_device_type wanted = CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_ACCELERATOR;
        cl_uint device_count = 0;
        cl_device_id* devices = NULL;

        err = clGetDeviceIDs( platforms[i], wanted, 0, NULL, &device_count );
        if( err != CL_SUCCESS || device_count == 0 )
        {
            continue;
        }

        devices = (cl_device_id*) calloc( device_count, sizeof( cl_device_id ) );
        if( !devices )
        {
            goto out;
        }

        err = clGetDeviceIDs( platforms[i], wanted, device_count, devices, NULL );
        if( err != CL_SUCCESS )
        {
            free( devices );
            continue;
        }

        for( cl_uint j = 0; j < device_count; ++j )
        {
            cl_bool available = CL_FALSE;
            cl_bool compiler_available = CL_FALSE;
            nwipe_opencl_candidate_t* grown;

            if( clGetDeviceInfo( devices[j], CL_DEVICE_AVAILABLE, sizeof( available ), &available, NULL )
                    != CL_SUCCESS
                || available != CL_TRUE )
            {
                continue;
            }
            if( clGetDeviceInfo(
                    devices[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof( compiler_available ), &compiler_available, NULL )
                    != CL_SUCCESS
                || compiler_available != CL_TRUE )
            {
                continue;
            }

            grown =
                (nwipe_opencl_candidate_t*) realloc( candidates, ( candidate_count + 1 ) * sizeof( *candidates ) );
            if( !grown )
            {
                free( devices );
                goto out;
            }
            candidates = grown;

            memset( &candidates[candidate_count], 0, sizeof( candidates[candidate_count] ) );
            candidates[candidate_count].platform = platforms[i];
            candidates[candidate_count].device = devices[j];
            clGetDeviceInfo(
                devices[j], CL_DEVICE_TYPE, sizeof( candidates[candidate_count].type ), &candidates[candidate_count].type, NULL );
            clGetDeviceInfo( devices[j],
                             CL_DEVICE_MAX_COMPUTE_UNITS,
                             sizeof( candidates[candidate_count].compute_units ),
                             &candidates[candidate_count].compute_units,
                             NULL );
            clGetDeviceInfo( devices[j],
                             CL_DEVICE_MAX_CLOCK_FREQUENCY,
                             sizeof( candidates[candidate_count].clock_mhz ),
                             &candidates[candidate_count].clock_mhz,
                             NULL );
            nwipe_opencl_device_name( devices[j],
                                      candidates[candidate_count].name,
                                      sizeof( candidates[candidate_count].name ) );
            candidate_count++;
        }

        free( devices );
    }

    if( candidate_count > 0 )
    {
        *out_candidates = candidates;
        *out_count = candidate_count;
        candidates = NULL;
        rc = 0;
    }

out:
    free( candidates );
    free( platforms );
    return rc;
}

static int nwipe_opencl_build_program( nwipe_opencl_philox_state_t* state );

static int nwipe_opencl_create_backend( nwipe_opencl_philox_state_t* state,
                                        cl_platform_id platform,
                                        cl_device_id device )
{
    cl_int err;
    const cl_context_properties context_props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties) platform, 0 };

    state->platform = platform;
    state->device = device;
    state->context = clCreateContext( context_props, 1, &state->device, NULL, NULL, &err );
    if( err != CL_SUCCESS || !state->context )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clCreateContext failed (err=%d)", err );
        nwipe_opencl_release_state( state );
        return -1;
    }

    state->queue = clCreateCommandQueue( state->context, state->device, 0, &err );
    if( err != CL_SUCCESS || !state->queue )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clCreateCommandQueue failed (err=%d)", err );
        nwipe_opencl_release_state( state );
        return -1;
    }

    if( nwipe_opencl_build_program( state ) != 0 )
    {
        nwipe_opencl_release_state( state );
        return -1;
    }

    return 0;
}

static int nwipe_opencl_build_program( nwipe_opencl_philox_state_t* state )
{
    cl_int err;
    const char* src = nwipe_opencl_philox_kernel_source;
    size_t src_len = strlen( src );
    char build_log[2048];
    size_t build_log_len = 0;

    state->program = clCreateProgramWithSource( state->context, 1, &src, &src_len, &err );
    if( err != CL_SUCCESS || !state->program )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clCreateProgramWithSource failed (err=%d)", err );
        return -1;
    }

    err = clBuildProgram( state->program, 1, &state->device, NULL, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        if( clGetProgramBuildInfo( state->program,
                                   state->device,
                                   CL_PROGRAM_BUILD_LOG,
                                   sizeof( build_log ) - 1,
                                   build_log,
                                   &build_log_len )
            == CL_SUCCESS )
        {
            build_log[build_log_len] = '\0';
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox build failed: %s", build_log );
        }
        else
        {
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clBuildProgram failed (err=%d)", err );
        }
        return -1;
    }

    state->kernel = clCreateKernel( state->program, "philox4x32_10", &err );
    if( err != CL_SUCCESS || !state->kernel )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clCreateKernel failed (err=%d)", err );
        return -1;
    }

    return 0;
}

static int nwipe_opencl_ensure_output_buffer( nwipe_opencl_philox_state_t* state, size_t bytes )
{
    cl_int err;

    if( state->output_capacity >= bytes && state->output_buffer )
    {
        return 0;
    }

    if( state->output_buffer )
    {
        clReleaseMemObject( state->output_buffer );
        state->output_buffer = NULL;
        state->output_capacity = 0;
    }

    state->output_buffer = clCreateBuffer( state->context, CL_MEM_WRITE_ONLY, bytes, NULL, &err );
    if( err != CL_SUCCESS || !state->output_buffer )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clCreateBuffer failed for %zu bytes (err=%d)", bytes, err );
        return -1;
    }

    state->output_capacity = bytes;
    return 0;
}

static int nwipe_opencl_generate_blocks( nwipe_opencl_philox_state_t* state, unsigned char* dst, size_t blocks )
{
    cl_int err;
    cl_ulong base_block;
    size_t global_size = blocks;
    size_t bytes = blocks * SIZE_OF_OPENCL_PHILOX_PRNG;

    if( blocks == 0 )
    {
        return 0;
    }

    if( nwipe_opencl_ensure_output_buffer( state, bytes ) != 0 )
    {
        return -1;
    }

    base_block = (cl_ulong) state->block_counter;

    err = clSetKernelArg( state->kernel, 0, sizeof( state->output_buffer ), &state->output_buffer );
    err |= clSetKernelArg( state->kernel, 1, sizeof( base_block ), &base_block );
    err |= clSetKernelArg( state->kernel, 2, sizeof( state->key[0] ), &state->key[0] );
    err |= clSetKernelArg( state->kernel, 3, sizeof( state->key[1] ), &state->key[1] );
    err |= clSetKernelArg( state->kernel, 4, sizeof( state->nonce[0] ), &state->nonce[0] );
    err |= clSetKernelArg( state->kernel, 5, sizeof( state->nonce[1] ), &state->nonce[1] );
    if( err != CL_SUCCESS )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clSetKernelArg failed (err=%d)", err );
        return -1;
    }

    err = clEnqueueNDRangeKernel( state->queue, state->kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clEnqueueNDRangeKernel failed (err=%d)", err );
        return -1;
    }

    err = clEnqueueReadBuffer( state->queue, state->output_buffer, CL_TRUE, 0, bytes, dst, 0, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: clEnqueueReadBuffer failed (err=%d)", err );
        return -1;
    }

    state->block_counter += blocks;
    return 0;
}

static unsigned long long nwipe_opencl_device_score( const nwipe_opencl_candidate_t* candidate )
{
    unsigned long long score = 1;

    if( candidate->compute_units > 0 )
    {
        score *= candidate->compute_units;
    }
    if( candidate->clock_mhz > 0 )
    {
        score *= candidate->clock_mhz;
    }
    if( candidate->type & CL_DEVICE_TYPE_GPU )
    {
        score *= 2;
    }

    return score;
}

static double nwipe_opencl_benchmark_candidate( const nwipe_opencl_candidate_t* candidate )
{
    nwipe_opencl_philox_state_t state;
    unsigned char* buffer;
    const size_t bytes = NWIPE_OPENCL_DEVICE_BENCH_BYTES;
    const size_t blocks = bytes / SIZE_OF_OPENCL_PHILOX_PRNG;
    double best_mbps = 0.0;

    memset( &state, 0, sizeof( state ) );
    state.key[0] = 0x243F6A88U;
    state.key[1] = 0x85A308D3U;
    state.nonce[0] = 0x13198A2EU;
    state.nonce[1] = 0x03707344U;

    buffer = (unsigned char*) malloc( bytes );
    if( !buffer )
    {
        return 0.0;
    }

    if( nwipe_opencl_create_backend( &state, candidate->platform, candidate->device ) != 0 )
    {
        free( buffer );
        return 0.0;
    }

    if( nwipe_opencl_generate_blocks( &state, buffer, blocks ) != 0 )
    {
        nwipe_opencl_release_state( &state );
        free( buffer );
        return 0.0;
    }

    for( int i = 0; i < NWIPE_OPENCL_DEVICE_BENCH_ROUNDS; ++i )
    {
        const double t0 = nwipe_opencl_monotonic_seconds();
        const int rc = nwipe_opencl_generate_blocks( &state, buffer, blocks );
        const double seconds = nwipe_opencl_monotonic_seconds() - t0;

        if( rc != 0 )
        {
            best_mbps = 0.0;
            break;
        }
        if( seconds > 0.0 )
        {
            const double mbps = ( (double) bytes / ( 1024.0 * 1024.0 ) ) / seconds;
            if( mbps > best_mbps )
            {
                best_mbps = mbps;
            }
        }
    }

    nwipe_opencl_release_state( &state );
    free( buffer );
    return best_mbps;
}

static int nwipe_opencl_pick_best_device( cl_platform_id* out_platform, cl_device_id* out_device )
{
    static int cached = 0;
    static cl_platform_id cached_platform = NULL;
    static cl_device_id cached_device = NULL;

    nwipe_opencl_candidate_t* candidates = NULL;
    size_t candidate_count = 0;
    size_t best = 0;
    double best_mbps = 0.0;

    if( cached )
    {
        *out_platform = cached_platform;
        *out_device = cached_device;
        return 0;
    }

    if( nwipe_opencl_collect_gpu_devices( &candidates, &candidate_count ) != 0 || candidate_count == 0 )
    {
        return -1;
    }

    if( candidate_count > 1 )
    {
        unsigned long long best_score = 0;
        for( size_t i = 0; i < candidate_count; ++i )
        {
            candidates[i].mbps = nwipe_opencl_benchmark_candidate( &candidates[i] );
            if( candidates[i].mbps > best_mbps )
            {
                best = i;
                best_mbps = candidates[i].mbps;
            }

            if( best_mbps == 0.0 )
            {
                const unsigned long long score = nwipe_opencl_device_score( &candidates[i] );
                if( score > best_score )
                {
                    best = i;
                    best_score = score;
                }
            }
        }
    }

    cached_platform = candidates[best].platform;
    cached_device = candidates[best].device;
    cached = 1;

    *out_platform = cached_platform;
    *out_device = cached_device;

    if( candidate_count > 1 && best_mbps > 0.0 )
    {
        nwipe_log( NWIPE_LOG_NOTICE,
                   "OpenCL Philox selected device '%s' from %zu GPU/accelerator candidates (%.1f MB/s mini-bench).",
                   candidates[best].name,
                   candidate_count,
                   best_mbps );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE,
                   "OpenCL Philox selected device '%s' from %zu GPU/accelerator candidate(s).",
                   candidates[best].name,
                   candidate_count );
    }

    free( candidates );
    return 0;
}
#endif

int nwipe_opencl_philox_prng_available( void )
{
#ifdef HAVE_OPENCL
    static int cached = -1;
    nwipe_opencl_candidate_t* candidates = NULL;
    size_t candidate_count = 0;

    if( cached != -1 )
    {
        return cached;
    }

    cached = ( nwipe_opencl_collect_gpu_devices( &candidates, &candidate_count ) == 0 && candidate_count > 0 ) ? 1 : 0;
    free( candidates );
    return cached;
#else
    return 0;
#endif
}

int nwipe_opencl_philox_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
#ifdef HAVE_OPENCL
    nwipe_opencl_philox_state_t* philox_state = *state;

    nwipe_log( NWIPE_LOG_NOTICE, "Initialising OpenCL Philox4x32 PRNG" );

    if( *state == NULL )
    {
        *state = calloc( 1, sizeof( nwipe_opencl_philox_state_t ) );
        philox_state = (nwipe_opencl_philox_state_t*) *state;
    }

    if( philox_state == NULL )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate OpenCL Philox PRNG state." );
        return -1;
    }

    nwipe_seed_to_key_nonce( seed, philox_state->key, philox_state->nonce );
    philox_state->block_counter = 0;
    philox_state->tail_offset = 0;
    philox_state->tail_length = 0;

    if( philox_state->context )
    {
        return 0;
    }

    if( nwipe_opencl_pick_best_device( &philox_state->platform, &philox_state->device ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "OpenCL Philox: no usable GPU/accelerator device found. CPU-only OpenCL runtimes are ignored." );
        return -1;
    }

    return nwipe_opencl_create_backend( philox_state, philox_state->platform, philox_state->device );
#else
    (void) state;
    (void) seed;
    nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG requested but this build has no OpenCL support." );
    return -1;
#endif
}

int nwipe_opencl_philox_prng_read( NWIPE_PRNG_READ_SIGNATURE )
{
#ifdef HAVE_OPENCL
    nwipe_opencl_philox_state_t* philox_state = (nwipe_opencl_philox_state_t*) *state;
    unsigned char* out = (unsigned char*) buffer;
    size_t remaining = count;

    if( philox_state == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG read requested without initialized state." );
        return -1;
    }

    while( remaining > 0 && philox_state->tail_offset < philox_state->tail_length )
    {
        size_t available = philox_state->tail_length - philox_state->tail_offset;
        size_t chunk = ( remaining < available ) ? remaining : available;

        memcpy( out, philox_state->tail + philox_state->tail_offset, chunk );
        philox_state->tail_offset += chunk;
        out += chunk;
        remaining -= chunk;

        if( philox_state->tail_offset == philox_state->tail_length )
        {
            philox_state->tail_offset = 0;
            philox_state->tail_length = 0;
        }
    }

    if( remaining >= SIZE_OF_OPENCL_PHILOX_PRNG )
    {
        size_t full_blocks = remaining / SIZE_OF_OPENCL_PHILOX_PRNG;
        size_t full_bytes = full_blocks * SIZE_OF_OPENCL_PHILOX_PRNG;

        if( nwipe_opencl_generate_blocks( philox_state, out, full_blocks ) != 0 )
        {
            return -1;
        }

        out += full_bytes;
        remaining -= full_bytes;
    }

    if( remaining > 0 )
    {
        if( nwipe_opencl_generate_blocks( philox_state, philox_state->tail, 1 ) != 0 )
        {
            return -1;
        }

        memcpy( out, philox_state->tail, remaining );
        philox_state->tail_offset = remaining;
        philox_state->tail_length = SIZE_OF_OPENCL_PHILOX_PRNG;
    }

    return 0;
#else
    (void) state;
    (void) buffer;
    (void) count;
    nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG read requested but this build has no OpenCL support." );
    return -1;
#endif
}

void nwipe_opencl_philox_prng_free( void** state )
{
    if( state == NULL || *state == NULL )
    {
        return;
    }

#ifdef HAVE_OPENCL
    nwipe_opencl_release_state( (nwipe_opencl_philox_state_t*) *state );
#endif
    free( *state );
    *state = NULL;
}
