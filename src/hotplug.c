/*
 *  hotplug.c: Hotplug device monitoring for nwipe.
 *
 *  This first implementation uses a polling loop over libparted's current
 *  device inventory so the feature does not require any new build-time
 *  dependencies yet. The admission and wipe path reuse the existing device
 *  validation and wipe methods.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#define _POSIX_C_SOURCE 200809L

/* Enable GNU extensions so that O_DIRECT is visible from <fcntl.h>. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>
#include <parted/parted.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include "context.h"
#include "device.h"
#include "hotplug_policy.h"
#include "logging.h"
#include "method.h"
#include "nwipe.h"
#include "options.h"

extern int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount );
extern int terminate_signal;
extern int user_abort;

typedef struct nwipe_hotplug_record_t_
{
    dev_t rdev;
    nwipe_hotplug_record_kind_t kind;
    char path[DEVICE_NAME_MAX_SIZE];
    struct nwipe_hotplug_record_t_* next;
} nwipe_hotplug_record_t;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    nwipe_hotplug_record_t* records;
    int active_jobs;
} nwipe_hotplug_state_t;

typedef struct
{
    nwipe_hotplug_state_t* state;
    nwipe_context_t* c;
    dev_t rdev;
} nwipe_hotplug_job_t;

static int nwipe_hotplug_path_to_rdev( const char* path, dev_t* out_rdev )
{
    struct stat st;

    if( path == NULL || out_rdev == NULL )
    {
        errno = EINVAL;
        return -1;
    }

    if( stat( path, &st ) != 0 )
    {
        return -1;
    }

    if( !S_ISBLK( st.st_mode ) )
    {
        errno = ENOTBLK;
        return -1;
    }

    *out_rdev = st.st_rdev;
    return 0;
}

static nwipe_hotplug_record_t* nwipe_hotplug_find_record( nwipe_hotplug_state_t* state, dev_t rdev )
{
    nwipe_hotplug_record_t* rec;

    for( rec = state->records; rec != NULL; rec = rec->next )
    {
        if( rec->rdev == rdev )
        {
            return rec;
        }
    }

    return NULL;
}

static void
nwipe_hotplug_set_record( nwipe_hotplug_state_t* state, dev_t rdev, nwipe_hotplug_record_kind_t kind, const char* path )
{
    nwipe_hotplug_record_t* rec;

    rec = nwipe_hotplug_find_record( state, rdev );
    if( rec == NULL )
    {
        rec = (nwipe_hotplug_record_t*) calloc( 1, sizeof( *rec ) );
        if( rec == NULL )
        {
            nwipe_log( NWIPE_LOG_ERROR,
                       "hotplug: unable to allocate registry entry for %s",
                       path != NULL ? path : "(unknown)" );
            return;
        }

        rec->rdev = rdev;
        rec->next = state->records;
        state->records = rec;
    }

    rec->kind = kind;
    if( path != NULL )
    {
        strncpy( rec->path, path, sizeof( rec->path ) - 1 );
        rec->path[sizeof( rec->path ) - 1] = '\0';
    }
}

static int nwipe_hotplug_present_contains( const dev_t* present, size_t count, dev_t rdev )
{
    size_t i;

    for( i = 0; i < count; i++ )
    {
        if( present[i] == rdev )
        {
            return 1;
        }
    }

    return 0;
}

static void nwipe_hotplug_prune_records( nwipe_hotplug_state_t* state, const dev_t* present, size_t count )
{
    nwipe_hotplug_record_t** link;

    link = &state->records;
    while( *link != NULL )
    {
        nwipe_hotplug_record_t* rec = *link;
        int removable = ( rec->kind == NWIPE_HOTPLUG_RECORD_PENDING || rec->kind == NWIPE_HOTPLUG_RECORD_DONE
                          || rec->kind == NWIPE_HOTPLUG_RECORD_BLOCKED );

        if( removable && !nwipe_hotplug_present_contains( present, count, rec->rdev ) )
        {
            *link = rec->next;
            free( rec );
            continue;
        }

        link = &rec->next;
    }
}

static int nwipe_hotplug_prepare_context_for_wipe( nwipe_context_t* c )
{
    int fd;
    int r;
    u64 size64;
    int open_flags;

    if( c == NULL )
    {
        return -1;
    }

    if( c->device_busy && !nwipe_options.force )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Device '%s' is IN USE but --force is not set, not wiping it.", c->device_name );
        c->select = NWIPE_SELECT_DISABLED_BUSY;
        return -1;
    }

    c->spinner_idx = 0;
    c->start_time = 0;
    c->end_time = 0;
    c->wipe_status = -1;
    c->io_direction = nwipe_options.io_direction;
    c->prng = nwipe_options.prng;
    c->prng_seed.length = 0;
    c->prng_seed.s = 0;
    c->prng_state = 0;
    c->result = 0;
    c->bytes_erased = 0;

    open_flags = O_RDWR;
#ifdef NWIPE_USE_DIRECT_IO
    if( nwipe_options.io_mode == NWIPE_IO_MODE_DIRECT || nwipe_options.io_mode == NWIPE_IO_MODE_AUTO )
    {
        open_flags |= O_DIRECT;
    }
#endif

    fd = open( c->device_name, open_flags );

#ifdef NWIPE_USE_DIRECT_IO
    if( fd < 0 && ( errno == EINVAL || errno == EOPNOTSUPP ) )
    {
        if( nwipe_options.io_mode == NWIPE_IO_MODE_DIRECT )
        {
            nwipe_perror( errno, __FUNCTION__, "open" );
            nwipe_log(
                NWIPE_LOG_FATAL, "O_DIRECT requested via --directio but not supported on '%s'.", c->device_name );
            c->select = NWIPE_SELECT_DISABLED;
            return -1;
        }

        if( nwipe_options.io_mode == NWIPE_IO_MODE_AUTO )
        {
            nwipe_log(
                NWIPE_LOG_WARNING, "O_DIRECT not supported on '%s', falling back to cached I/O.", c->device_name );
            open_flags &= ~O_DIRECT;
            fd = open( c->device_name, open_flags );
        }
    }
#endif

    if( fd < 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "open" );
        nwipe_log( NWIPE_LOG_WARNING, "Unable to open device '%s'.", c->device_name );
        c->select = NWIPE_SELECT_DISABLED;
        return -1;
    }

    c->device_fd = fd;

    if( fstat( c->device_fd, &c->device_stat ) != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "fstat" );
        nwipe_log( NWIPE_LOG_ERROR, "Unable to stat file '%s'.", c->device_name );
        return -1;
    }

    if( !S_ISBLK( c->device_stat.st_mode ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "'%s' is not a block device.", c->device_name );
        return -1;
    }

    if( strlen( (const char*) c->device_serial_no ) )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "%s has serial number %s", c->device_name, c->device_serial_no );
    }

    c->device_size = lseek( c->device_fd, 0, SEEK_END );
    if( c->device_size == (long long) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_ERROR, "Unable to determine the size of '%s'.", c->device_name );
        return -1;
    }

    if( ioctl( c->device_fd, BLKGETSIZE64, &size64 ) != 0 )
    {
        fprintf( stderr, "Error: BLKGETSIZE64 failed on '%s'.\n", c->device_name );
        nwipe_log( NWIPE_LOG_ERROR, "BLKGETSIZE64 failed on '%s'.", c->device_name );
        return -1;
    }

    c->device_size = size64;

    r = lseek( c->device_fd, 0, SEEK_SET );
    if( r == (off64_t) -1 )
    {
        nwipe_perror( errno, __FUNCTION__, "lseek" );
        nwipe_log( NWIPE_LOG_ERROR, "Unable to reset the '%s' file offset.", c->device_name );
        return -1;
    }

    if( c->device_size == 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "%s, log/phy/dev %i/%i/%llu",
                   c->device_name,
                   c->device_sector_size,
                   c->device_phys_sector_size,
                   c->device_size );
        return -1;
    }

    if( nwipe_update_geometry_for_io( c ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "No sane device geometry for '%s'.", c->device_name );
        return -1;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "%s: Device geometry is as follows...", c->device_name );
    nwipe_log( NWIPE_LOG_NOTICE, "  Logical sector size  = %i", c->device_sector_size );
    nwipe_log( NWIPE_LOG_NOTICE, "  Physical sector size = %i", c->device_phys_sector_size );
    nwipe_log( NWIPE_LOG_NOTICE, "  I/O block size       = %zu", c->device_io_block_size );
    nwipe_log( NWIPE_LOG_NOTICE, "  I/O block alignment  = %zu", c->device_io_block_alignment );
    nwipe_log( NWIPE_LOG_NOTICE, "  I/O buffer alignment = %zu", c->device_io_buffer_alignment );
    nwipe_log( NWIPE_LOG_NOTICE, "  Total device bytes   = %llu", c->device_size );

    return 0;
}

static void nwipe_hotplug_release_context( nwipe_context_t* c )
{
    if( c == NULL )
    {
        return;
    }

    if( c->device_fd >= 0 )
    {
        close( c->device_fd );
        c->device_fd = -1;
    }

    free( c );
}

static void nwipe_hotplug_mark_job_done( nwipe_hotplug_state_t* state,
                                         dev_t rdev,
                                         nwipe_hotplug_record_kind_t kind,
                                         const char* path )
{
    pthread_mutex_lock( &state->mutex );
    nwipe_hotplug_set_record( state, rdev, kind, path );
    if( state->active_jobs > 0 )
    {
        state->active_jobs--;
    }
    pthread_cond_broadcast( &state->cond );
    pthread_mutex_unlock( &state->mutex );
}

static void* nwipe_hotplug_job_thread( void* ptr )
{
    nwipe_hotplug_job_t* job;
    nwipe_context_t* c;
    void* ( *method )( void* );
    int success;
    int r;
    char device_name[DEVICE_NAME_MAX_SIZE];

    job = (nwipe_hotplug_job_t*) ptr;
    c = job->c;
    method = (void* (*) (void*) ) nwipe_options.method;
    strncpy( device_name, c->device_name, sizeof( device_name ) - 1 );
    device_name[sizeof( device_name ) - 1] = '\0';

    nwipe_log( NWIPE_LOG_NOTICE, "hotplug: preparing %s for wipe", c->device_name );

    r = nwipe_hotplug_prepare_context_for_wipe( c );
    if( r != 0 )
    {
        nwipe_log( NWIPE_LOG_WARNING, "hotplug: device %s was not started", device_name );
        nwipe_hotplug_release_context( c );
        nwipe_hotplug_mark_job_done( job->state, job->rdev, NWIPE_HOTPLUG_RECORD_BLOCKED, device_name );
        free( job );
        return NULL;
    }

    nwipe_log( NWIPE_LOG_NOTICE,
               "hotplug: starting wipe on %s using method %s",
               c->device_name,
               nwipe_method_label( nwipe_options.method ) );

    method( c );
    success = ( c->result == 0 );

    if( success )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "hotplug: wipe completed successfully for %s", c->device_name );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "hotplug: wipe failed for %s with result %d", device_name, c->result );
    }

    nwipe_hotplug_release_context( c );
    nwipe_hotplug_mark_job_done(
        job->state, job->rdev, success ? NWIPE_HOTPLUG_RECORD_DONE : NWIPE_HOTPLUG_RECORD_BLOCKED, device_name );
    free( job );
    return NULL;
}

static void nwipe_hotplug_start_job( nwipe_hotplug_state_t* state, nwipe_context_t* c, dev_t rdev )
{
    pthread_t thread;
    nwipe_hotplug_job_t* job;
    char device_name[DEVICE_NAME_MAX_SIZE];

    strncpy( device_name, c->device_name, sizeof( device_name ) - 1 );
    device_name[sizeof( device_name ) - 1] = '\0';

    job = (nwipe_hotplug_job_t*) calloc( 1, sizeof( *job ) );
    if( job == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "hotplug: unable to allocate job wrapper for %s", device_name );
        nwipe_hotplug_release_context( c );
        nwipe_hotplug_mark_job_done( state, rdev, NWIPE_HOTPLUG_RECORD_BLOCKED, device_name );
        return;
    }

    job->state = state;
    job->c = c;
    job->rdev = rdev;

    if( pthread_create( &thread, NULL, nwipe_hotplug_job_thread, job ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "hotplug: unable to start wipe thread for %s", device_name );
        nwipe_hotplug_release_context( c );
        free( job );
        nwipe_hotplug_mark_job_done( state, rdev, NWIPE_HOTPLUG_RECORD_BLOCKED, device_name );
        return;
    }

    pthread_detach( thread );
}

static void* nwipe_hotplug_signal_hand( void* ptr )
{
    sigset_t sigset;
    int sig;

    (void) ptr;

    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );

    while( 1 )
    {
        if( sigwait( &sigset, &sig ) != 0 )
        {
            continue;
        }

        switch( sig )
        {
            case SIGHUP:
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                terminate_signal = 1;
                user_abort = 1;
                return NULL;
            default:
                break;
        }
    }
}

static size_t nwipe_hotplug_snapshot_present( dev_t** out_present )
{
    PedDevice* dev = NULL;
    dev_t* present = NULL;
    size_t count = 0;

    ped_device_probe_all();
    while( ( dev = ped_device_get_next( dev ) ) )
    {
        dev_t rdev;

        if( nwipe_hotplug_path_is_partition_at_root( "/sys/class/block", dev->path ) )
        {
            continue;
        }

        if( nwipe_hotplug_path_to_rdev( dev->path, &rdev ) != 0 )
        {
            continue;
        }

        {
            dev_t* tmp = (dev_t*) realloc( present, ( count + 1 ) * sizeof( *present ) );
            if( tmp == NULL )
            {
                break;
            }
            present = tmp;
        }

        present[count++] = rdev;
    }

    *out_present = present;
    return count;
}

int nwipe_hotplug_run( void )
{
    nwipe_hotplug_state_t state;
    pthread_t signal_thread;
    int signal_thread_started = 0;
    dev_t* present = NULL;
    size_t present_count = 0;

    memset( &state, 0, sizeof( state ) );
    pthread_mutex_init( &state.mutex, NULL );
    pthread_cond_init( &state.cond, NULL );

    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    pthread_sigmask( SIG_SETMASK, &sigset, NULL );

    if( !nwipe_options.nosignals )
    {
        if( pthread_create( &signal_thread, NULL, nwipe_hotplug_signal_hand, NULL ) == 0 )
        {
            pthread_detach( signal_thread );
            signal_thread_started = 1;
        }
    }

    nwipe_log( NWIPE_LOG_NOTICE, "hotplug: monitoring enabled; disks present at startup will be ignored" );
    nwipe_options_log();

    present_count = nwipe_hotplug_snapshot_present( &present );
    if( present_count > 0 )
    {
        size_t i;

        pthread_mutex_lock( &state.mutex );
        for( i = 0; i < present_count; i++ )
        {
            nwipe_hotplug_set_record( &state, present[i], NWIPE_HOTPLUG_RECORD_BASELINE, NULL );
        }
        pthread_mutex_unlock( &state.mutex );

        nwipe_log( NWIPE_LOG_NOTICE, "hotplug: baseline snapshot captured for %zu devices", present_count );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE, "hotplug: baseline snapshot captured with no devices present" );
    }

    while( terminate_signal == 0 )
    {
        PedDevice* dev = NULL;

        free( present );
        present = NULL;
        present_count = nwipe_hotplug_snapshot_present( &present );

        pthread_mutex_lock( &state.mutex );
        nwipe_hotplug_prune_records( &state, present, present_count );
        pthread_mutex_unlock( &state.mutex );

        ped_device_probe_all();
        while( ( dev = ped_device_get_next( dev ) ) )
        {
            dev_t rdev;
            nwipe_context_t** contexts = NULL;
            nwipe_context_t* c;

            if( nwipe_hotplug_path_to_rdev( dev->path, &rdev ) != 0 )
            {
                continue;
            }

            if( nwipe_hotplug_path_is_partition_at_root( "/sys/class/block", dev->path ) )
            {
                pthread_mutex_lock( &state.mutex );
                if( nwipe_hotplug_find_record( &state, rdev ) == NULL )
                {
                    nwipe_hotplug_set_record( &state, rdev, NWIPE_HOTPLUG_RECORD_BLOCKED, dev->path );
                    nwipe_log( NWIPE_LOG_NOTICE, "hotplug: ignoring partition %s", dev->path );
                }
                pthread_mutex_unlock( &state.mutex );
                continue;
            }

            pthread_mutex_lock( &state.mutex );
            nwipe_hotplug_record_t* existing = nwipe_hotplug_find_record( &state, rdev );
            if( existing != NULL )
            {
                if( nwipe_hotplug_should_promote_record( existing->kind, state.active_jobs ) )
                {
                    existing->kind = NWIPE_HOTPLUG_RECORD_ACTIVE;
                }
                else
                {
                    pthread_mutex_unlock( &state.mutex );
                    continue;
                }
            }
            else
            {
                nwipe_hotplug_set_record( &state, rdev, NWIPE_HOTPLUG_RECORD_PENDING, dev->path );
                nwipe_log(
                    NWIPE_LOG_NOTICE, "hotplug: detected new disk %s; waiting one poll before admission", dev->path );
                pthread_mutex_unlock( &state.mutex );
                continue;
            }

            pthread_mutex_unlock( &state.mutex );

            if( check_device( &contexts, dev, 0 ) == 0 )
            {
                pthread_mutex_lock( &state.mutex );
                nwipe_hotplug_set_record( &state, rdev, NWIPE_HOTPLUG_RECORD_BLOCKED, dev->path );
                pthread_mutex_unlock( &state.mutex );
                continue;
            }

            c = contexts[0];
            free( contexts );

            pthread_mutex_lock( &state.mutex );
            state.active_jobs++;
            nwipe_hotplug_set_record( &state, rdev, NWIPE_HOTPLUG_RECORD_ACTIVE, c->device_name );
            pthread_mutex_unlock( &state.mutex );

            nwipe_hotplug_start_job( &state, c, rdev );
        }

        sleep( 1 );
    }

    pthread_mutex_lock( &state.mutex );
    while( state.active_jobs > 0 )
    {
        pthread_cond_wait( &state.cond, &state.mutex );
    }
    pthread_mutex_unlock( &state.mutex );

    pthread_mutex_lock( &state.mutex );
    while( state.records != NULL )
    {
        nwipe_hotplug_record_t* next = state.records->next;
        free( state.records );
        state.records = next;
    }
    pthread_mutex_unlock( &state.mutex );

    free( present );
    pthread_mutex_destroy( &state.mutex );
    pthread_cond_destroy( &state.cond );

    if( signal_thread_started )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "hotplug: signal watcher stopped" );
    }

    return 0;
}
