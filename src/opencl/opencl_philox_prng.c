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
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifdef HAVE_OPENCL
#ifndef CL_TARGET_OPENCL_VERSION
/* The backend only needs OpenCL 1.2.  Targeting it avoids accidentally
 * depending on newer loader symbols when building with OpenCL 2.x/3.x headers. */
#define CL_TARGET_OPENCL_VERSION 120
#endif
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
    size_t max_chunk_bytes;
    int log_errors;
#endif
    uint32_t key[2];
    uint32_t nonce[2];
    uint64_t block_counter;
    unsigned char tail[SIZE_OF_OPENCL_PHILOX_PRNG];
    size_t tail_offset;
    size_t tail_length;
} nwipe_opencl_philox_state_t;

#ifdef HAVE_OPENCL
#define NWIPE_OPENCL_DEVICE_BENCH_BYTES ( 4u * 1024u * 1024u )
#define NWIPE_OPENCL_DEVICE_BENCH_ROUNDS 2
#define NWIPE_OPENCL_MAX_CHUNK_BYTES ( 64u * 1024u * 1024u )
#define NWIPE_OPENCL_SELECTOR_SIZE 256u
#define NWIPE_OPENCL_STATUS_SIZE 1024u

typedef struct nwipe_opencl_candidate_s
{
    cl_platform_id platform;
    cl_device_id device;
    cl_device_type type;
    cl_uint compute_units;
    cl_uint clock_mhz;
    double mbps;
    size_t index;
    char name[128];
    char platform_name[128];
    char vendor[128];
    char version[128];
} nwipe_opencl_candidate_t;

static pthread_once_t nwipe_opencl_selection_once = PTHREAD_ONCE_INIT;
static cl_platform_id nwipe_opencl_selected_platform = NULL;
static cl_device_id nwipe_opencl_selected_device = NULL;
static char nwipe_opencl_selector[NWIPE_OPENCL_SELECTOR_SIZE];
static char nwipe_opencl_status[NWIPE_OPENCL_STATUS_SIZE] = "OpenCL device discovery has not run yet.";
static int nwipe_opencl_selection_started = 0;

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
    if( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 )
    {
        return 0.0;
    }
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
}

static void nwipe_opencl_set_status( const char* format, ... )
{
    va_list args;

    va_start( args, format );
    vsnprintf( nwipe_opencl_status, sizeof( nwipe_opencl_status ), format, args );
    va_end( args );
    nwipe_opencl_status[sizeof( nwipe_opencl_status ) - 1] = '\0';
}

static const char* nwipe_opencl_error_name( cl_int err )
{
    switch( err )
    {
        case CL_SUCCESS:
            return "CL_SUCCESS";
        case CL_DEVICE_NOT_FOUND:
            return "CL_DEVICE_NOT_FOUND";
        case CL_DEVICE_NOT_AVAILABLE:
            return "CL_DEVICE_NOT_AVAILABLE";
        case CL_COMPILER_NOT_AVAILABLE:
            return "CL_COMPILER_NOT_AVAILABLE";
        case CL_MEM_OBJECT_ALLOCATION_FAILURE:
            return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        case CL_OUT_OF_RESOURCES:
            return "CL_OUT_OF_RESOURCES";
        case CL_OUT_OF_HOST_MEMORY:
            return "CL_OUT_OF_HOST_MEMORY";
        case CL_BUILD_PROGRAM_FAILURE:
            return "CL_BUILD_PROGRAM_FAILURE";
        case CL_INVALID_VALUE:
            return "CL_INVALID_VALUE";
        case CL_INVALID_PLATFORM:
            return "CL_INVALID_PLATFORM";
        case CL_INVALID_DEVICE:
            return "CL_INVALID_DEVICE";
        case CL_INVALID_CONTEXT:
            return "CL_INVALID_CONTEXT";
        case CL_INVALID_COMMAND_QUEUE:
            return "CL_INVALID_COMMAND_QUEUE";
        case CL_INVALID_MEM_OBJECT:
            return "CL_INVALID_MEM_OBJECT";
        case CL_INVALID_PROGRAM:
            return "CL_INVALID_PROGRAM";
        case CL_INVALID_PROGRAM_EXECUTABLE:
            return "CL_INVALID_PROGRAM_EXECUTABLE";
        case CL_INVALID_KERNEL_NAME:
            return "CL_INVALID_KERNEL_NAME";
        case CL_INVALID_KERNEL:
            return "CL_INVALID_KERNEL";
        case CL_INVALID_ARG_INDEX:
            return "CL_INVALID_ARG_INDEX";
        case CL_INVALID_ARG_VALUE:
            return "CL_INVALID_ARG_VALUE";
        case CL_INVALID_ARG_SIZE:
            return "CL_INVALID_ARG_SIZE";
        case CL_INVALID_WORK_DIMENSION:
            return "CL_INVALID_WORK_DIMENSION";
        case CL_INVALID_WORK_GROUP_SIZE:
            return "CL_INVALID_WORK_GROUP_SIZE";
        case CL_INVALID_WORK_ITEM_SIZE:
            return "CL_INVALID_WORK_ITEM_SIZE";
        case CL_PLATFORM_NOT_FOUND_KHR:
            return "CL_PLATFORM_NOT_FOUND_KHR";
        default:
            return "unknown OpenCL error";
    }
}

static void nwipe_opencl_platform_string( cl_platform_id platform,
                                          cl_platform_info param,
                                          char* out,
                                          size_t out_size,
                                          const char* fallback )
{
    size_t required = 0;
    char* value = NULL;

    if( out_size == 0 )
    {
        return;
    }

    out[0] = '\0';
    if( clGetPlatformInfo( platform, param, 0, NULL, &required ) == CL_SUCCESS && required > 0
        && required < ( 1024u * 1024u ) )
    {
        value = (char*) malloc( required + 1 );
        if( value && clGetPlatformInfo( platform, param, required, value, NULL ) == CL_SUCCESS )
        {
            value[required] = '\0';
            snprintf( out, out_size, "%s", value );
        }
    }
    free( value );
    if( out[0] == '\0' )
    {
        snprintf( out, out_size, "%s", fallback );
    }
    out[out_size - 1] = '\0';
}

static void nwipe_opencl_device_string( cl_device_id device,
                                        cl_device_info param,
                                        char* out,
                                        size_t out_size,
                                        const char* fallback )
{
    size_t required = 0;
    char* value = NULL;

    if( out_size == 0 )
    {
        return;
    }

    out[0] = '\0';
    if( clGetDeviceInfo( device, param, 0, NULL, &required ) == CL_SUCCESS && required > 0
        && required < ( 1024u * 1024u ) )
    {
        value = (char*) malloc( required + 1 );
        if( value && clGetDeviceInfo( device, param, required, value, NULL ) == CL_SUCCESS )
        {
            value[required] = '\0';
            snprintf( out, out_size, "%s", value );
        }
    }
    free( value );
    if( out[0] == '\0' )
    {
        snprintf( out, out_size, "%s", fallback );
    }
    out[out_size - 1] = '\0';
}

static int nwipe_opencl_contains_case_insensitive( const char* haystack, const char* needle )
{
    size_t needle_len;

    if( !needle || needle[0] == '\0' )
    {
        return 1;
    }
    if( !haystack )
    {
        return 0;
    }

    needle_len = strlen( needle );
    for( ; *haystack; ++haystack )
    {
        if( strncasecmp( haystack, needle, needle_len ) == 0 )
        {
            return 1;
        }
    }
    return 0;
}

static int nwipe_opencl_candidate_matches( const nwipe_opencl_candidate_t* candidate, const char* selector )
{
    char* end = NULL;
    unsigned long requested_index;

    if( !selector || selector[0] == '\0' )
    {
        return 1;
    }

    requested_index = strtoul( selector, &end, 10 );
    if( end != selector && *end == '\0' )
    {
        return requested_index <= SIZE_MAX && candidate->index == (size_t) requested_index;
    }

    return nwipe_opencl_contains_case_insensitive( candidate->name, selector )
        || nwipe_opencl_contains_case_insensitive( candidate->platform_name, selector )
        || nwipe_opencl_contains_case_insensitive( candidate->vendor, selector );
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
    state->max_chunk_bytes = 0;
}

static int nwipe_opencl_collect_gpu_devices( nwipe_opencl_candidate_t** out_candidates, size_t* out_count )
{
    cl_int err;
    cl_uint platform_count = 0;
    cl_platform_id* platforms = NULL;
    nwipe_opencl_candidate_t* candidates = NULL;
    size_t candidate_count = 0;
    size_t reported_devices = 0;
    size_t unavailable_devices = 0;
    cl_int first_device_query_error = CL_SUCCESS;
    int rc = -1;

    *out_candidates = NULL;
    *out_count = 0;

    err = clGetPlatformIDs( 0, NULL, &platform_count );
    if( err == CL_PLATFORM_NOT_FOUND_KHR || platform_count == 0 )
    {
        nwipe_opencl_set_status(
            "The OpenCL loader is present, but no vendor platform was found (no working ICD/runtime installed)." );
        return -1;
    }
    if( err != CL_SUCCESS )
    {
        nwipe_opencl_set_status( "clGetPlatformIDs failed: %s (%d). Check the OpenCL ICD loader and vendor runtime.",
                                 nwipe_opencl_error_name( err ),
                                 err );
        return -1;
    }

    platforms = (cl_platform_id*) calloc( platform_count, sizeof( cl_platform_id ) );
    if( !platforms )
    {
        nwipe_opencl_set_status( "Unable to allocate memory while enumerating OpenCL platforms." );
        return -1;
    }

    err = clGetPlatformIDs( platform_count, platforms, NULL );
    if( err != CL_SUCCESS )
    {
        nwipe_opencl_set_status(
            "Unable to read %u OpenCL platform(s): %s (%d).", platform_count, nwipe_opencl_error_name( err ), err );
        goto out;
    }

    for( cl_uint i = 0; i < platform_count; ++i )
    {
        cl_uint device_count = 0;
        cl_device_id* devices = NULL;

        /* Query all types first and filter using CL_DEVICE_TYPE below.  Several
         * vendor ICDs mishandle a combined GPU|ACCELERATOR query. */
        err = clGetDeviceIDs( platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &device_count );
        if( err != CL_SUCCESS || device_count == 0 )
        {
            if( err != CL_SUCCESS && err != CL_DEVICE_NOT_FOUND && first_device_query_error == CL_SUCCESS )
            {
                first_device_query_error = err;
            }
            continue;
        }

        devices = (cl_device_id*) calloc( device_count, sizeof( cl_device_id ) );
        if( !devices )
        {
            nwipe_opencl_set_status( "Unable to allocate memory while enumerating OpenCL devices." );
            goto out;
        }

        err = clGetDeviceIDs( platforms[i], CL_DEVICE_TYPE_ALL, device_count, devices, NULL );
        if( err != CL_SUCCESS )
        {
            free( devices );
            continue;
        }

        for( cl_uint j = 0; j < device_count; ++j )
        {
            cl_bool available = CL_FALSE;
            cl_bool compiler_available = CL_FALSE;
            cl_device_type type = 0;
            nwipe_opencl_candidate_t* grown;

            reported_devices++;

            if( clGetDeviceInfo( devices[j], CL_DEVICE_TYPE, sizeof( type ), &type, NULL ) != CL_SUCCESS
                || !( type & ( CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_ACCELERATOR ) ) )
            {
                continue;
            }

            if( clGetDeviceInfo( devices[j], CL_DEVICE_AVAILABLE, sizeof( available ), &available, NULL ) != CL_SUCCESS
                || available != CL_TRUE )
            {
                unavailable_devices++;
                continue;
            }
            if( clGetDeviceInfo(
                    devices[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof( compiler_available ), &compiler_available, NULL )
                    != CL_SUCCESS
                || compiler_available != CL_TRUE )
            {
                unavailable_devices++;
                continue;
            }

            grown = (nwipe_opencl_candidate_t*) realloc( candidates, ( candidate_count + 1 ) * sizeof( *candidates ) );
            if( !grown )
            {
                nwipe_opencl_set_status( "Unable to allocate memory while collecting OpenCL devices." );
                free( devices );
                goto out;
            }
            candidates = grown;

            memset( &candidates[candidate_count], 0, sizeof( candidates[candidate_count] ) );
            candidates[candidate_count].platform = platforms[i];
            candidates[candidate_count].device = devices[j];
            candidates[candidate_count].type = type;
            candidates[candidate_count].index = candidate_count;
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
            nwipe_opencl_device_string( devices[j],
                                        CL_DEVICE_NAME,
                                        candidates[candidate_count].name,
                                        sizeof( candidates[candidate_count].name ),
                                        "unknown OpenCL device" );
            nwipe_opencl_platform_string( platforms[i],
                                          CL_PLATFORM_NAME,
                                          candidates[candidate_count].platform_name,
                                          sizeof( candidates[candidate_count].platform_name ),
                                          "unknown OpenCL platform" );
            nwipe_opencl_device_string( devices[j],
                                        CL_DEVICE_VENDOR,
                                        candidates[candidate_count].vendor,
                                        sizeof( candidates[candidate_count].vendor ),
                                        "unknown vendor" );
            nwipe_opencl_device_string( devices[j],
                                        CL_DEVICE_VERSION,
                                        candidates[candidate_count].version,
                                        sizeof( candidates[candidate_count].version ),
                                        "unknown OpenCL version" );
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
    else
    {
        if( reported_devices == 0 )
        {
            if( first_device_query_error != CL_SUCCESS )
            {
                nwipe_opencl_set_status(
                    "%u OpenCL platform(s) loaded, but device enumeration failed: %s (%d). Check the vendor "
                    "runtime/driver combination and GPU device permissions.",
                    platform_count,
                    nwipe_opencl_error_name( first_device_query_error ),
                    first_device_query_error );
            }
            else
            {
                nwipe_opencl_set_status(
                    "%u OpenCL platform(s) loaded, but the ICD exposed no devices. Check that the GPU driver and "
                    "matching vendor runtime are installed and that nwipe can access /dev/dri/render* (and /dev/kfd "
                    "for ROCm).",
                    platform_count );
            }
        }
        else
        {
            nwipe_opencl_set_status(
                "%u OpenCL platform(s) reported %zu device(s), but no available GPU/accelerator with a source "
                "compiler was found%s.",
                platform_count,
                reported_devices,
                unavailable_devices ? " (some matching devices were unavailable or had no compiler)" : "" );
        }
    }

out:
    free( candidates );
    free( platforms );
    return rc;
}

static int
nwipe_opencl_build_program( nwipe_opencl_philox_state_t* state, char* reason, size_t reason_size, int log_errors );

static int nwipe_opencl_create_backend( nwipe_opencl_philox_state_t* state,
                                        cl_platform_id platform,
                                        cl_device_id device,
                                        char* reason,
                                        size_t reason_size,
                                        int log_errors )
{
    cl_int err;
    cl_ulong max_alloc = 0;
    const cl_context_properties context_props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties) platform, 0 };

    if( reason && reason_size > 0 )
    {
        reason[0] = '\0';
    }

    state->platform = platform;
    state->device = device;
    state->log_errors = log_errors;
    state->context = clCreateContext( context_props, 1, &state->device, NULL, NULL, &err );
    if( err != CL_SUCCESS || !state->context )
    {
        if( reason && reason_size > 0 )
            snprintf( reason, reason_size, "clCreateContext failed: %s (%d)", nwipe_opencl_error_name( err ), err );
        if( log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: %s", reason ? reason : "clCreateContext failed" );
        nwipe_opencl_release_state( state );
        return -1;
    }

    state->queue = clCreateCommandQueue( state->context, state->device, 0, &err );
    if( err != CL_SUCCESS || !state->queue )
    {
        if( reason && reason_size > 0 )
            snprintf(
                reason, reason_size, "clCreateCommandQueue failed: %s (%d)", nwipe_opencl_error_name( err ), err );
        if( log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: %s", reason ? reason : "clCreateCommandQueue failed" );
        nwipe_opencl_release_state( state );
        return -1;
    }

    if( nwipe_opencl_build_program( state, reason, reason_size, log_errors ) != 0 )
    {
        nwipe_opencl_release_state( state );
        return -1;
    }

    if( clGetDeviceInfo( device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof( max_alloc ), &max_alloc, NULL ) != CL_SUCCESS
        || max_alloc < SIZE_OF_OPENCL_PHILOX_PRNG )
    {
        max_alloc = NWIPE_OPENCL_MAX_CHUNK_BYTES;
    }
    if( max_alloc > NWIPE_OPENCL_MAX_CHUNK_BYTES )
    {
        max_alloc = NWIPE_OPENCL_MAX_CHUNK_BYTES;
    }
    state->max_chunk_bytes = (size_t) max_alloc;
    state->max_chunk_bytes -= state->max_chunk_bytes % SIZE_OF_OPENCL_PHILOX_PRNG;

    return 0;
}

static int
nwipe_opencl_build_program( nwipe_opencl_philox_state_t* state, char* reason, size_t reason_size, int log_errors )
{
    cl_int err;
    const char* src = nwipe_opencl_philox_kernel_source;
    size_t src_len = strlen( src );

    state->program = clCreateProgramWithSource( state->context, 1, &src, &src_len, &err );
    if( err != CL_SUCCESS || !state->program )
    {
        if( reason && reason_size > 0 )
            snprintf(
                reason, reason_size, "clCreateProgramWithSource failed: %s (%d)", nwipe_opencl_error_name( err ), err );
        if( log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: %s", reason ? reason : "clCreateProgramWithSource failed" );
        return -1;
    }

    err = clBuildProgram( state->program, 1, &state->device, NULL, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        size_t build_log_size = 0;
        char* build_log = NULL;

        if( clGetProgramBuildInfo( state->program, state->device, CL_PROGRAM_BUILD_LOG, 0, NULL, &build_log_size )
                == CL_SUCCESS
            && build_log_size > 0 && build_log_size < ( 1024u * 1024u ) )
        {
            build_log = (char*) malloc( build_log_size + 1 );
            if( build_log )
            {
                build_log[0] = '\0';
                if( clGetProgramBuildInfo(
                        state->program, state->device, CL_PROGRAM_BUILD_LOG, build_log_size, build_log, NULL )
                    == CL_SUCCESS )
                {
                    build_log[build_log_size] = '\0';
                }
            }
        }

        if( reason && reason_size > 0 )
        {
            snprintf( reason,
                      reason_size,
                      "clBuildProgram failed: %s (%d)%s%s",
                      nwipe_opencl_error_name( err ),
                      err,
                      build_log && build_log[0] ? "; compiler log: " : "",
                      build_log && build_log[0] ? build_log : "" );
        }
        if( log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: %s", reason ? reason : "clBuildProgram failed" );
        free( build_log );
        return -1;
    }

    state->kernel = clCreateKernel( state->program, "philox4x32_10", &err );
    if( err != CL_SUCCESS || !state->kernel )
    {
        if( reason && reason_size > 0 )
            snprintf( reason, reason_size, "clCreateKernel failed: %s (%d)", nwipe_opencl_error_name( err ), err );
        if( log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: %s", reason ? reason : "clCreateKernel failed" );
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
        if( state->log_errors )
            nwipe_log( NWIPE_LOG_ERROR,
                       "OpenCL Philox: clCreateBuffer failed for %zu bytes: %s (%d)",
                       bytes,
                       nwipe_opencl_error_name( err ),
                       err );
        return -1;
    }

    state->output_capacity = bytes;
    return 0;
}

static int nwipe_opencl_generate_chunk( nwipe_opencl_philox_state_t* state, unsigned char* dst, size_t blocks )
{
    cl_int err;
    cl_ulong base_block;
    size_t global_size = blocks;
    size_t bytes;

    if( blocks == 0 )
    {
        return 0;
    }

    if( !dst || blocks > SIZE_MAX / SIZE_OF_OPENCL_PHILOX_PRNG )
    {
        if( state->log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: invalid or oversized output request." );
        return -1;
    }
    bytes = blocks * SIZE_OF_OPENCL_PHILOX_PRNG;

    if( nwipe_opencl_ensure_output_buffer( state, bytes ) != 0 )
    {
        return -1;
    }

    base_block = (cl_ulong) state->block_counter;

    err = clSetKernelArg( state->kernel, 0, sizeof( state->output_buffer ), &state->output_buffer );
    if( err == CL_SUCCESS )
        err = clSetKernelArg( state->kernel, 1, sizeof( base_block ), &base_block );
    if( err == CL_SUCCESS )
        err = clSetKernelArg( state->kernel, 2, sizeof( state->key[0] ), &state->key[0] );
    if( err == CL_SUCCESS )
        err = clSetKernelArg( state->kernel, 3, sizeof( state->key[1] ), &state->key[1] );
    if( err == CL_SUCCESS )
        err = clSetKernelArg( state->kernel, 4, sizeof( state->nonce[0] ), &state->nonce[0] );
    if( err == CL_SUCCESS )
        err = clSetKernelArg( state->kernel, 5, sizeof( state->nonce[1] ), &state->nonce[1] );
    if( err != CL_SUCCESS )
    {
        if( state->log_errors )
            nwipe_log(
                NWIPE_LOG_ERROR, "OpenCL Philox: clSetKernelArg failed: %s (%d)", nwipe_opencl_error_name( err ), err );
        return -1;
    }

    err = clEnqueueNDRangeKernel( state->queue, state->kernel, 1, NULL, &global_size, NULL, 0, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        if( state->log_errors )
            nwipe_log( NWIPE_LOG_ERROR,
                       "OpenCL Philox: clEnqueueNDRangeKernel failed: %s (%d)",
                       nwipe_opencl_error_name( err ),
                       err );
        return -1;
    }

    err = clEnqueueReadBuffer( state->queue, state->output_buffer, CL_TRUE, 0, bytes, dst, 0, NULL, NULL );
    if( err != CL_SUCCESS )
    {
        if( state->log_errors )
            nwipe_log( NWIPE_LOG_ERROR,
                       "OpenCL Philox: clEnqueueReadBuffer failed: %s (%d)",
                       nwipe_opencl_error_name( err ),
                       err );
        return -1;
    }

    state->block_counter += blocks;
    return 0;
}

static int nwipe_opencl_generate_blocks( nwipe_opencl_philox_state_t* state, unsigned char* dst, size_t blocks )
{
    size_t max_blocks;

    if( blocks == 0 )
    {
        return 0;
    }
    if( !state || !dst || blocks > UINT64_MAX - state->block_counter )
    {
        if( state && state->log_errors )
            nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox: output request would overflow the Philox counter." );
        return -1;
    }

    max_blocks = state->max_chunk_bytes / SIZE_OF_OPENCL_PHILOX_PRNG;
    if( max_blocks == 0 )
    {
        max_blocks = NWIPE_OPENCL_MAX_CHUNK_BYTES / SIZE_OF_OPENCL_PHILOX_PRNG;
    }

    while( blocks > 0 )
    {
        const size_t chunk_blocks = blocks < max_blocks ? blocks : max_blocks;
        if( nwipe_opencl_generate_chunk( state, dst, chunk_blocks ) != 0 )
        {
            return -1;
        }
        dst += chunk_blocks * SIZE_OF_OPENCL_PHILOX_PRNG;
        blocks -= chunk_blocks;
    }
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

static double nwipe_opencl_benchmark_candidate( const nwipe_opencl_candidate_t* candidate,
                                                char* reason,
                                                size_t reason_size,
                                                int* usable )
{
    nwipe_opencl_philox_state_t state;
    unsigned char* buffer;
    static const unsigned char zero_vector[SIZE_OF_OPENCL_PHILOX_PRNG] = {
        0xd5, 0xe8, 0x27, 0x66, 0x8d, 0xc5, 0x69, 0xe1, 0x4c, 0xac, 0x57, 0xbc, 0xd8, 0xdb, 0x00, 0x9b };
    const size_t bytes = NWIPE_OPENCL_DEVICE_BENCH_BYTES;
    const size_t blocks = bytes / SIZE_OF_OPENCL_PHILOX_PRNG;
    double best_mbps = 0.0;

    memset( &state, 0, sizeof( state ) );
    if( reason && reason_size > 0 )
    {
        reason[0] = '\0';
    }
    if( usable )
    {
        *usable = 0;
    }

    buffer = (unsigned char*) malloc( bytes );
    if( !buffer )
    {
        if( reason && reason_size > 0 )
            snprintf( reason, reason_size, "unable to allocate the %zu-byte probe buffer", bytes );
        return 0.0;
    }

    if( nwipe_opencl_create_backend( &state, candidate->platform, candidate->device, reason, reason_size, 0 ) != 0 )
    {
        free( buffer );
        return 0.0;
    }

    /* A successful enumeration and kernel build are insufficient: run a known
     * Philox4x32-10 vector to catch broken compiler/runtime combinations. */
    if( nwipe_opencl_generate_blocks( &state, buffer, 1 ) != 0
        || memcmp( buffer, zero_vector, sizeof( zero_vector ) ) != 0 )
    {
        if( reason && reason_size > 0 )
            snprintf( reason, reason_size, "Philox kernel self-test returned an incorrect result" );
        nwipe_opencl_release_state( &state );
        free( buffer );
        return 0.0;
    }

    state.key[0] = 0x243F6A88U;
    state.key[1] = 0x85A308D3U;
    state.nonce[0] = 0x13198A2EU;
    state.nonce[1] = 0x03707344U;
    state.block_counter = 0;

    if( nwipe_opencl_generate_blocks( &state, buffer, blocks ) != 0 )
    {
        if( reason && reason_size > 0 )
            snprintf( reason, reason_size, "OpenCL warm-up execution failed" );
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
            if( reason && reason_size > 0 )
                snprintf( reason, reason_size, "OpenCL throughput probe failed" );
            nwipe_opencl_release_state( &state );
            free( buffer );
            return 0.0;
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
    if( usable )
    {
        *usable = 1;
    }
    return best_mbps;
}

static void nwipe_opencl_select_device_once( void )
{
    nwipe_opencl_candidate_t* candidates = NULL;
    size_t candidate_count = 0;
    size_t best = 0;
    size_t matched = 0;
    size_t usable_count = 0;
    double best_mbps = 0.0;
    unsigned long long best_score = 0;
    char first_failure[512] = "";
    const char* selector;

    nwipe_opencl_selection_started = 1;

    if( nwipe_opencl_collect_gpu_devices( &candidates, &candidate_count ) != 0 || candidate_count == 0 )
    {
        return;
    }

    selector = nwipe_opencl_selector[0] ? nwipe_opencl_selector : getenv( "NWIPE_OPENCL_DEVICE" );

    for( size_t i = 0; i < candidate_count; ++i )
    {
        char reason[512] = "";
        int usable = 0;
        unsigned long long score;

        if( !nwipe_opencl_candidate_matches( &candidates[i], selector ) )
        {
            continue;
        }
        matched++;

        candidates[i].mbps = nwipe_opencl_benchmark_candidate( &candidates[i], reason, sizeof( reason ), &usable );
        if( !usable )
        {
            if( first_failure[0] == '\0' )
            {
                snprintf( first_failure,
                          sizeof( first_failure ),
                          "device [%zu] '%s': %s",
                          candidates[i].index,
                          candidates[i].name,
                          reason[0] ? reason : "unknown runtime failure" );
            }
            continue;
        }

        usable_count++;
        score = nwipe_opencl_device_score( &candidates[i] );
        if( usable_count == 1 || candidates[i].mbps > best_mbps
            || ( candidates[i].mbps == best_mbps && score > best_score ) )
        {
            best = i;
            best_mbps = candidates[i].mbps;
            best_score = score;
        }
    }

    if( matched == 0 )
    {
        nwipe_opencl_set_status(
            "OpenCL selector '%s' did not match any of the %zu available GPU/accelerator device(s). Use a zero-based "
            "device index, or a case-insensitive device/platform/vendor substring.",
            selector ? selector : "",
            candidate_count );
        free( candidates );
        return;
    }

    if( usable_count == 0 )
    {
        nwipe_opencl_set_status(
            "%zu OpenCL GPU/accelerator candidate(s) matched%s, but all failed context, compiler, execution, or "
            "Philox correctness checks. First failure: %s",
            matched,
            selector && selector[0] ? " the requested selector" : "",
            first_failure[0] ? first_failure : "unknown runtime failure" );
        free( candidates );
        return;
    }

    nwipe_opencl_selected_platform = candidates[best].platform;
    nwipe_opencl_selected_device = candidates[best].device;
    nwipe_opencl_set_status( "Using OpenCL device [%zu] '%s' on platform '%s' (%s, %s; %.1f MB/s probe).",
                             candidates[best].index,
                             candidates[best].name,
                             candidates[best].platform_name,
                             candidates[best].vendor,
                             candidates[best].version,
                             candidates[best].mbps );

    free( candidates );
}

static int nwipe_opencl_pick_best_device( cl_platform_id* out_platform, cl_device_id* out_device )
{
    if( pthread_once( &nwipe_opencl_selection_once, nwipe_opencl_select_device_once ) != 0
        || !nwipe_opencl_selected_platform || !nwipe_opencl_selected_device )
    {
        return -1;
    }

    *out_platform = nwipe_opencl_selected_platform;
    *out_device = nwipe_opencl_selected_device;
    return 0;
}
#endif

int nwipe_opencl_philox_set_device_selector( const char* selector )
{
#ifdef HAVE_OPENCL
    size_t length = selector ? strlen( selector ) : 0;

    if( nwipe_opencl_selection_started )
    {
        return -1;
    }
    if( length >= sizeof( nwipe_opencl_selector ) )
    {
        return -1;
    }

    if( length > 0 )
    {
        memcpy( nwipe_opencl_selector, selector, length + 1 );
    }
    else
    {
        nwipe_opencl_selector[0] = '\0';
    }
    return 0;
#else
    (void) selector;
    return 0;
#endif
}

const char* nwipe_opencl_philox_prng_status( void )
{
#ifdef HAVE_OPENCL
    return nwipe_opencl_status;
#else
    return "This nwipe binary was built without OpenCL support. Install OpenCL development headers/loader and "
           "rebuild, or configure with --enable-opencl to require it.";
#endif
}

int nwipe_opencl_philox_prng_available( void )
{
#ifdef HAVE_OPENCL
    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    return nwipe_opencl_pick_best_device( &platform, &device ) == 0;
#else
    return 0;
#endif
}

int nwipe_opencl_philox_prng_init( NWIPE_PRNG_INIT_SIGNATURE )
{
#ifdef HAVE_OPENCL
    nwipe_opencl_philox_state_t* philox_state;

    if( !state )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG initialization received a null state pointer." );
        return -1;
    }
    philox_state = *state;

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
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox unavailable: %s", nwipe_opencl_philox_prng_status() );
        return -1;
    }

    {
        char reason[512];
        const int rc = nwipe_opencl_create_backend(
            philox_state, philox_state->platform, philox_state->device, reason, sizeof( reason ), 1 );
        if( rc == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "OpenCL Philox initialized. %s", nwipe_opencl_status );
        }
        return rc;
    }
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
    nwipe_opencl_philox_state_t* philox_state = state ? (nwipe_opencl_philox_state_t*) *state : NULL;
    unsigned char* out = (unsigned char*) buffer;
    size_t remaining = count;

    if( philox_state == NULL || !philox_state->context || !philox_state->queue || !philox_state->kernel )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG read requested without initialized state." );
        return -1;
    }
    if( count > 0 && !buffer )
    {
        nwipe_log( NWIPE_LOG_ERROR, "OpenCL Philox PRNG read received a null output buffer." );
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
