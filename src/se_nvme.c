/*
 * se_nvme.c: NVMe Secure Erase
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: https://github.com/linux-nvme/nvme-cli (v2.16/GPLv2)
 */
#ifdef HAVE_CONFIG_H
#include <config.h> /* HAVE_LIBNVME */
#endif

#ifdef HAVE_LIBNVME

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* asprintf */
#endif

#define _POSIX_C_SOURCE 200809L

#include "nwipe.h"
#include "context.h"
#include "logging.h"
#include "se_nvme.h"

/* clang-format off */
#define ROUND_UP( N, S ) ( ( ( ( N ) + (S) -1 ) / ( S ) ) * ( S ) )
/* clang-format on */

/* Helper for aligned memory allocation, do not change this. */
static void* nwipe_se_nvme_alloc( size_t len )
{
    void* p;

    len = ROUND_UP( len, 0x1000 );
    if( posix_memalign( &p, (size_t) getpagesize(), len ) )
        return NULL;

    memset( p, 0, len );
    return p;
} /* nwipe_se_nvme_alloc */

/*
 * Initializes given pre-allocated nwipe_se_nvme_topo pointer.
 * Establishes the root node and builds the topology tree from it.
 * nwipe_se_nvme_topo_destroy() should be called when no longer needed.
 */
int nwipe_se_nvme_topo_init( nwipe_se_nvme_topo* topo )
{
    memset( topo, 0, sizeof( *topo ) );

    topo->root = nvme_create_root( NULL, LOG_ERR );
    if( !topo->root )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: nvme_create_root() failed", __FUNCTION__ );
        return -1;
    }

    int err = nvme_scan_topology( topo->root, NULL, NULL );
    if( err != 0 )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "%s: nvme_scan_topology() failed: %s (errno=%d)", __FUNCTION__, strerror( errno ), errno );
        nvme_free_tree( topo->root );
        topo->root = NULL;
        return -1;
    }

    return 0;
} /* nwipe_se_nvme_topo_init */

/*
 * Frees internal allocations of the given nwipe_se_nvme_topo.
 * Do not use the topology anymore after this function was called.
 */
void nwipe_se_nvme_topo_destroy( nwipe_se_nvme_topo* topo )
{
    if( topo->root )
    {
        nvme_free_tree( topo->root );
        topo->root = NULL;
    }
} /* nwipe_se_nvme_topo_destroy */

/*
 * Resolves a NVMe namespace to a NVMe controller.
 * Finds all other NVMe namespaces attached to the NVMe controller.
 * Sets given context's san->ctrl_path and populates san->ns_* variables.
 * Returns -1 on error, 0 on success, and 1 if no NVMe controller was found.
 */
static int nwipe_se_nvme_resolve( const char* device_name, nwipe_se_nvme_ctx* san, nvme_root_t r )
{
    nvme_host_t h;
    nvme_subsystem_t s;
    nvme_ctrl_t c;
    nvme_ns_t ns;

    const char* bname = strrchr( device_name, '/' );
    bname = bname ? bname + 1 : device_name;

    nvme_for_each_host( r, h )
    {
        nvme_for_each_subsystem( h, s )
        {
            nvme_subsystem_for_each_ctrl( s, c )
            {
                nvme_ctrl_for_each_ns( c, ns )
                {
                    if( strcmp( bname, nvme_ns_get_name( ns ) ) )
                        continue;

                    if( asprintf( &san->ctrl_path, "/dev/%s", nvme_ctrl_get_name( c ) ) < 0 )
                    {
                        nwipe_log( NWIPE_LOG_ERROR,
                                   "%s: asprintf() failed: %s (errno=%d)",
                                   __FUNCTION__,
                                   strerror( errno ),
                                   errno );
                        return -1;
                    }

                    int count = 0;
                    nvme_ns_t n;
                    nvme_ctrl_for_each_ns( c, n ) count++;

                    san->ns_names = calloc( (size_t) ( count + 1 ), sizeof( char* ) );
                    if( !san->ns_names )
                    {
                        free( san->ctrl_path );
                        san->ctrl_path = NULL;
                        return -1;
                    }

                    int i = 0;
                    nvme_ctrl_for_each_ns( c, n )
                    {
                        san->ns_names[i] = strdup( nvme_ns_get_name( n ) );
                        if( !san->ns_names[i] )
                        {
                            nwipe_log( NWIPE_LOG_ERROR,
                                       "%s: strdup() failed: %s (errno=%d)",
                                       __FUNCTION__,
                                       strerror( errno ),
                                       errno );
                            for( int j = 0; j < i; j++ )
                                free( san->ns_names[j] );
                            free( san->ns_names );
                            san->ns_names = NULL;
                            free( san->ctrl_path );
                            san->ctrl_path = NULL;
                            return -1;
                        }

                        i++;
                    }
                    san->ns_names[i] = NULL;
                    san->ns_count = count;

                    return 0;
                }
            }
        }
    }

    nwipe_log( NWIPE_LOG_ERROR, "%s: %s: not found in topology tree", __FUNCTION__, device_name );
    return 1;
} /* nwipe_se_nvme_resolve */

/*
 * Initializes given pre-allocated nwipe_se_nvme_ctx pointer.
 * nwipe_se_nvme_destroy() should be called when no longer needed.
 */
int nwipe_se_nvme_init( const char* device_name, nwipe_se_nvme_ctx* san, nwipe_se_nvme_topo* topo )
{
    memset( san, 0, sizeof( *san ) );
    san->fd = -1;

    /*
     * We resolve through the NVMe topology tree, NEVER by string manipulation:
     *   The numeric suffix on the character device, for example the 0 in
     *   /dev/nvme0, does NOT indicate this device handle is the parent
     *   controller of any namespaces with the same suffix. The namespace
     *   handle's numeral may be coming from the subsystem identifier, which is
     *   independent of the controller's identifier. Do not assume any
     *   particular device relationship based on their names. If you do, you may
     *   irrevocably erase data on an unintended device.
     */
    if( nwipe_se_nvme_resolve( device_name, san, topo->root ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: nwipe_se_nvme_resolve() failed", __FUNCTION__, device_name );
        return -1;
    }

    return 0;
} /* nwipe_se_nvme_init */

/* Opens the NVMe controller device */
int nwipe_se_nvme_open( nwipe_se_nvme_ctx* san )
{
    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    san->fd = open( san->ctrl_path, O_RDONLY );
    if( san->fd < 0 )
    {
        int eno = errno;
        snprintf( san->error_msg, sizeof( san->error_msg ), "%s (errno=%d)", strerror( eno ), eno );
        nwipe_log( NWIPE_LOG_ERROR,
                   "%s: %s: Failed to open NVMe controller: %s (%d)",
                   __FUNCTION__,
                   san->ctrl_path,
                   strerror( eno ),
                   eno );
        return -1;
    }

    return 0;
} /* nwipe_se_nvme_open */

/* Closes the NVMe controller device */
void nwipe_se_nvme_close( nwipe_se_nvme_ctx* san )
{
    if( san->fd >= 0 )
    {
        close( san->fd );
        san->fd = -1;
    }
} /* nwipe_se_nvme_close */

/*
 * Frees internal allocations of the given nwipe_se_nvme_ctx.
 * Do not use the context anymore after this function was called.
 */
void nwipe_se_nvme_destroy( nwipe_se_nvme_ctx* san )
{
    if( san->ns_names )
    {
        for( int i = 0; i < san->ns_count; i++ )
            free( san->ns_names[i] );
        free( san->ns_names );
        san->ns_names = NULL;
        san->ns_count = 0;
    }

    if( san->ctrl_path )
    {
        free( san->ctrl_path );
        san->ctrl_path = NULL;
    }
} /* nwipe_se_nvme_destroy */

/*
 * Probes for NVMe Sanitize capabilities using nvme_identify_ctrl().
 * Sets san->cap_caps_valid to 1 if san_cap_* values are useable.
 * Returns -1 only on an allocation- or command-rejected failure.
 */
int nwipe_se_nvme_sancap( nwipe_se_nvme_ctx* san )
{
    if( san->fd < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->ctrl_path );
        return -1;
    }

    struct nvme_id_ctrl* id_ctrl = nwipe_se_nvme_alloc( sizeof( *id_ctrl ) );
    if( !id_ctrl )
    {
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: nvme_id_ctrl allocation failed", __FUNCTION__, san->ctrl_path );
        return -1;
    }

    int err = nvme_identify_ctrl( san->fd, id_ctrl );
    if( err != 0 )
    {
        if( err < 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_identify_ctrl() failed: %s (errno=%d, err=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       strerror( errno ),
                       errno,
                       err );
        }
        else
        {
            const char* msg = nvme_status_to_string( err, false );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_identify_ctrl() failed: %s (status=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       msg,
                       err );
        }

        san->cap_crypto_erase = 0;
        san->cap_block_erase = 0;
        san->cap_overwrite = 0;
        san->cap_caps_valid = 0;

        free( id_ctrl );
        return -1;
    }

    __u32 sanicap = le32toh( id_ctrl->sanicap );
    san->cap_crypto_erase = !!( sanicap & NVME_CTRL_SANICAP_CES );
    san->cap_block_erase = !!( sanicap & NVME_CTRL_SANICAP_BES );
    san->cap_overwrite = !!( sanicap & NVME_CTRL_SANICAP_OWS );
    san->cap_caps_valid = 1;

    free( id_ctrl );
    return 0;
} /* nwipe_se_nvme_sancap */

/*
 * Polls the sanitize status using nvme_get_log_sanitize().
 * Updates san->state, san->progress_* and san_est_* variables.
 * Avoid hammering of device with calls in a tight loop, ensure delays.
 * Success returns 0; errors -1, logs and populates san->error_msg buffer.
 */
int nwipe_se_nvme_poll( nwipe_se_nvme_ctx* san )
{
    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    if( san->fd < 0 )
    {
        snprintf( san->error_msg, sizeof( san->error_msg ), "FD is not open" );
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->ctrl_path );
        return -1;
    }

    struct nvme_sanitize_log_page* log = nwipe_se_nvme_alloc( sizeof( *log ) );
    if( !log )
    {
        snprintf( san->error_msg, sizeof( san->error_msg ), "Log page allocation failed" );
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: nvme_sanitize_log_page allocation failed", __FUNCTION__, san->ctrl_path );
        return -1;
    }

    int err = nvme_get_log_sanitize( san->fd, true, log );
    if( err != 0 )
    {
        if( err < 0 )
        {
            int eno = errno;
            snprintf( san->error_msg, sizeof( san->error_msg ), "%s (errno=%d, err=%d)", strerror( eno ), eno, err );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_get_log_sanitize() failed: %s (errno=%d, err=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       strerror( eno ),
                       eno,
                       err );
        }
        else
        {
            const char* msg = nvme_status_to_string( err, false );
            snprintf( san->error_msg, sizeof( san->error_msg ), "%s (status=%d)", msg, err );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_get_log_sanitize() failed: %s (status=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       msg,
                       err );
        }

        free( log );
        return -1;
    }

    __u16 sstat = le16toh( log->sstat );
    __u32 scdw10 = le32toh( log->scdw10 );
    san->sanact = ( enum nvme_sanitize_sanact )( scdw10 & 0x7 );
    san->state_raw = (__u8) ( sstat & NVME_SANITIZE_SSTAT_STATUS_MASK );

    switch( san->state_raw )
    {
        /* Don't fix the typo, it's in the library */
        case NVME_SANITIZE_SSTAT_STATUS_IN_PROGESS:
            san->state = NWIPE_SE_NVME_STATE_IN_PROGRESS;
            break;

        case NVME_SANITIZE_SSTAT_STATUS_COMPLETE_SUCCESS:
        case NVME_SANITIZE_SSTAT_STATUS_ND_COMPLETE_SUCCESS:
            san->state = NWIPE_SE_NVME_STATE_SUCCESS;
            break;

        case NVME_SANITIZE_SSTAT_STATUS_COMPLETED_FAILED:
            san->state = NWIPE_SE_NVME_STATE_FAILURE;
            break;

        default:
            san->state = NWIPE_SE_NVME_STATE_IDLE;
            break;
    }

    san->progress_raw = le16toh( log->sprog );
    san->progress_pct = ( (int) san->progress_raw * 100 ) / UINT16_MAX;
    if( san->progress_pct > 100 )
        san->progress_pct = 100;

    __u32 eto = le32toh( log->eto );
    __u32 etbe = le32toh( log->etbe );
    __u32 etce = le32toh( log->etce );
    san->est_overwrite = ( eto != 0 && eto != 0xFFFFFFFF ) ? eto : 0;
    san->est_block_erase = ( etbe != 0 && etbe != 0xFFFFFFFF ) ? etbe : 0;
    san->est_crypto_erase = ( etce != 0 && etce != 0xFFFFFFFF ) ? etce : 0;

    free( log );
    return 0;
} /* nwipe_se_nvme_poll */

/*
 * Run the san->planned_sanact sanitize operation.
 * Sends command to device and returns 0 on success.
 * Operation itself runs on the device (as non-blocking).
 * Errors return -1, logs and populates san->error_msg buffer.
 */
int nwipe_se_nvme_sanitize( nwipe_se_nvme_ctx* san )
{
    memset( san->error_msg, 0, sizeof( san->error_msg ) ); /* Used in GUI */

    if( san->fd < 0 )
    {
        snprintf( san->error_msg, sizeof( san->error_msg ), "FD is not open" );
        nwipe_log( NWIPE_LOG_ERROR, "%s: %s: FD is not open", __FUNCTION__, san->ctrl_path );
        return -1;
    }

    if( san->planned_sanact == NVME_SANITIZE_SANACT_EXIT_FAILURE
        || san->planned_sanact == NVME_SANITIZE_SANACT_EXIT_MEDIA_VERIF )
    {
        if( san->ause )
        {
            snprintf( san->error_msg, sizeof( san->error_msg ), "AUSE not allowed with sanact" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: AUSE not allowed with sanact=%d",
                       __FUNCTION__,
                       san->ctrl_path,
                       san->planned_sanact );
            return -1;
        }
        if( san->nodas )
        {
            snprintf( san->error_msg, sizeof( san->error_msg ), "NODAS not allowed with sanact" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: NODAS not allowed with sanact=%d",
                       __FUNCTION__,
                       san->ctrl_path,
                       san->planned_sanact );
            return -1;
        }
    }

    if( san->planned_sanact != NVME_SANITIZE_SANACT_START_OVERWRITE )
    {
        if( san->owpass || san->oipbp || san->ovrpat )
        {
            snprintf( san->error_msg, sizeof( san->error_msg ), "Overwrite fields not allowed with sanact" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: Overwrite fields set but sanact=%d is not overwrite",
                       __FUNCTION__,
                       san->ctrl_path,
                       san->planned_sanact );
            return -1;
        }
    }
    else
    {
        if( san->owpass > 15 )
        {
            snprintf( san->error_msg, sizeof( san->error_msg ), "Overwrite passes out of range" );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: Overwrite owpass=%d out of range [0-15]",
                       __FUNCTION__,
                       san->ctrl_path,
                       san->owpass );
            return -1;
        }
    }

    switch( san->planned_sanact )
    {
        case NVME_SANITIZE_SANACT_START_BLOCK_ERASE:
        case NVME_SANITIZE_SANACT_START_CRYPTO_ERASE:
        case NVME_SANITIZE_SANACT_START_OVERWRITE:
            san->destructive_sanact = 1;
            break;
        default:
            san->destructive_sanact = 0;
            break;
    }

    nwipe_log( NWIPE_LOG_INFO, "%s: issuing SANITIZE sanact=%d", san->ctrl_path, san->planned_sanact );

    struct nvme_sanitize_nvm_args args;
    memset( &args, 0, sizeof( args ) );

    args.args_size = sizeof( args );
    args.fd = san->fd;
    args.timeout = NVME_DEFAULT_IOCTL_TIMEOUT;
    args.sanact = san->planned_sanact;
    args.ovrpat = san->ovrpat;
    args.ause = san->ause;
    args.owpass = san->owpass;
    args.oipbp = san->oipbp;
    args.nodas = san->nodas;
    args.emvs = san->emvs;
    args.result = NULL;

    int err = nvme_sanitize_nvm( &args );
    if( err != 0 )
    {
        if( err < 0 )
        {
            int eno = errno;
            snprintf( san->error_msg, sizeof( san->error_msg ), "%s (errno=%d, err=%d)", strerror( eno ), eno, err );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_sanitize_nvm() failed: %s (errno=%d, err=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       strerror( eno ),
                       eno,
                       err );
        }
        else
        {
            const char* msg = nvme_status_to_string( err, false );
            snprintf( san->error_msg, sizeof( san->error_msg ), "%s (status=%d)", msg, err );
            nwipe_log( NWIPE_LOG_ERROR,
                       "%s: %s: nvme_sanitize_nvm() failed: %s (status=%d)",
                       __FUNCTION__,
                       san->ctrl_path,
                       msg,
                       err );
        }

        return -1;
    }

    nwipe_log( NWIPE_LOG_INFO, "%s: SANITIZE command accepted (sanact=%d)", san->ctrl_path, san->planned_sanact );

    return 0;
} /* nwipe_se_nvme_sanitize */

#endif /* HAVE_LIBNVME */
