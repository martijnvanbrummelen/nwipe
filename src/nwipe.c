/*
 *  nwipe.c:  Darik's Wipe.
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
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <bits/pthreadtypes.h>
#include <bits/sigthread.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "device.h"
#include "logging.h"
#include "gui.h"

#include <sys/ioctl.h> /* FIXME: Twice Included */
#include <sys/shm.h>
#include <wait.h>

#include <parted/parted.h>
#include <parted/debug.h>

int terminate_signal;

int main( int argc, char** argv )
{
    int nwipe_optind;  // The result of nwipe_options().
    int nwipe_enumerated;  // The number of contexts that have been enumerated.
    int nwipe_error = 0;  // An error counter.
    int nwipe_selected = 0;  // The number of contexts that have been selected.
    pthread_t nwipe_gui_thread = 0;  // The thread ID of the GUI thread.
    pthread_t nwipe_sigint_thread;  // The thread ID of the sigint handler.

    /* The entropy source file handle. */
    int nwipe_entropy;

    /* The generic index variables. */
    int i;
    int j;

    /* The generic result buffer. */
    int r;

    /* Initialise the termintaion signal, 1=terminate nwipe */
    terminate_signal = 0;

    /* nwipes return status value, set prior to exit at the end of nwipe, as no other exit points allowed */
    int return_status = 0;

    /* Two arrays are used, containing pointers to the the typedef for each disk */
    /* The first array (c1) points to all devices, the second points to only     */
    /* the disks selected for wiping.                                            */

    /* The array of pointers to enumerated contexts. */
    /* Initialised and populated in device scan.     */
    nwipe_context_t** c1 = 0;

    /* Parse command line options. */
    nwipe_optind = nwipe_options_parse( argc, argv );
    if( nwipe_optind == argc )
    {
        /* File names were not given by the user.  Scan for devices. */
        nwipe_enumerated = nwipe_device_scan( &c1 );

        if( nwipe_enumerated == 0 )
        {
            nwipe_log( NWIPE_LOG_INFO, "Storage devices not found." );
            cleanup();
            return -1;
        }
        else
        {
            nwipe_log( NWIPE_LOG_INFO, "Automatically enumerated %i devices.", nwipe_enumerated );
        }
    }
    else
    {
        argv += nwipe_optind;
        argc -= nwipe_optind;

        nwipe_enumerated = nwipe_device_get( &c1, argv, argc );
        if( nwipe_enumerated == 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Devices not found. Check you're not excluding drives unnecessarily." );
            printf( "No drives found" );
            cleanup();
            exit( 1 );
        }
    }

    /* Log the System information */
    nwipe_log_sysinfo();

    /* The array of pointers to contexts that will actually be wiped. */
    nwipe_context_t** c2 = (nwipe_context_t**) malloc( nwipe_enumerated * sizeof( nwipe_context_t* ) );

    /* Open the entropy source. */
    nwipe_entropy = open( NWIPE_KNOB_ENTROPY, O_RDONLY );

    /* Check the result. */
    if( nwipe_entropy < 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "open" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to open entropy source %s.", NWIPE_KNOB_ENTROPY );
        cleanup();
        return errno;
    }

    nwipe_log( NWIPE_LOG_NOTICE, "Opened entropy source '%s'.", NWIPE_KNOB_ENTROPY );

    /* Block relevant signals in main thread. Any other threads that are     */
    /*        created after this will also block those signals.              */
    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    sigaddset( &sigset, SIGUSR1 );
    pthread_sigmask( SIG_SETMASK, &sigset, NULL );

    /* Create a signal handler thread.  This thread will catch all           */
    /*      signals and decide what to do with them.  This will only         */
    /*      catch nondirected signals.  (I.e., if a thread causes a SIGFPE   */
    /*      then that thread will get that signal.                           */

    /* Pass a pointer to a struct containing all data to the signal handler. */
    nwipe_misc_thread_data_t nwipe_misc_thread_data;
    nwipe_thread_data_ptr_t nwipe_thread_data_ptr;

    nwipe_thread_data_ptr.c = c2;
    nwipe_misc_thread_data.nwipe_enumerated = nwipe_enumerated;
    nwipe_misc_thread_data.nwipe_selected = 0;
    if( !nwipe_options.nogui )
        nwipe_misc_thread_data.gui_thread = &nwipe_gui_thread;
    nwipe_thread_data_ptr.nwipe_misc_thread_data = &nwipe_misc_thread_data;

    if( !nwipe_options.nosignals )
    {
        pthread_attr_t pthread_attr;
        pthread_attr_init( &pthread_attr );
        pthread_attr_setdetachstate( &pthread_attr, PTHREAD_CREATE_DETACHED );

        pthread_create( &nwipe_sigint_thread, &pthread_attr, signal_hand, &nwipe_thread_data_ptr );
    }

    /* A context struct for each device has already been created. */
    /* Now set specific nwipe options */
    for( i = 0; i < nwipe_enumerated; i++ )
    {

        /* Set the entropy source. */
        c1[i]->entropy_fd = nwipe_entropy;

        if( nwipe_options.autonuke )
        {
            /* When the autonuke option is set, select all disks. */
            // TODO - partitions
            // if( c1[i].device_part == 0 ) { c1[i].select = NWIPE_SELECT_TRUE;        }
            // else                         { c1[i].select = NWIPE_SELECT_TRUE_PARENT; }
            c1[i]->select = NWIPE_SELECT_TRUE;
        }
        else
        {
            /* The user must manually select devices. */
            c1[i]->select = NWIPE_SELECT_FALSE;
        }

        /* Set the PRNG implementation. */
        c1[i]->prng = nwipe_options.prng;
        c1[i]->prng_seed.length = 0;
        c1[i]->prng_seed.s = 0;
        c1[i]->prng_state = 0;
    }

    /* Check for initialization errors. */
    if( nwipe_error )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Initialization eror %i\n", nwipe_error );
        cleanup();
        return -1;
    }

    /* Start the ncurses interface. */
    if( !nwipe_options.nogui )
        nwipe_gui_init();

    if( nwipe_options.autonuke == 1 )
    {
        /* Print the options window. */
        if( !nwipe_options.nogui )
            nwipe_gui_options();
    }
    else
    {
        /* Get device selections from the user. */
        if( nwipe_options.nogui )
        {
            printf( "--nogui option must be used with autonuke option\n" );
            cleanup();
            exit( 1 );
        }
        else
        {
            nwipe_gui_select( nwipe_enumerated, c1 );
        }
    }

    /* Count the number of selected contexts. */
    for( i = 0; i < nwipe_enumerated; i++ )
    {
        if( c1[i]->select == NWIPE_SELECT_TRUE )
        {
            nwipe_selected += 1;
        }
    }

    /* Pass the number selected to the struct for other threads */
    nwipe_misc_thread_data.nwipe_selected = nwipe_selected;

    /* Populate the array of selected contexts. */
    for( i = 0, j = 0; i < nwipe_enumerated; i++ )
    {
        if( c1[i]->select == NWIPE_SELECT_TRUE )
        {
            /* Copy the context. */
            c2[j++] = c1[i];
        }
    }

    /* TODO: free c1 and c2 memory. */

    for( i = 0; i < nwipe_selected; i++ )
    {
        /* A result buffer for the BLKGETSIZE64 ioctl. */
        u64 size64;

        /* Initialise the wipe_status flag, -1 = wipe not yet started */
        c2[i]->wipe_status = -1;

        /* Open the file for reads and writes. */
        c2[i]->device_fd = open( c2[i]->device_name, O_RDWR );

        /* Check the open() result. */
        if( c2[i]->device_fd < 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "open" );
            nwipe_log( NWIPE_LOG_WARNING, "Unable to open device '%s'.", c2[i]->device_name );
            c2[i]->select = NWIPE_SELECT_DISABLED;
            continue;
        }

        /* Stat the file. */
        if( fstat( c2[i]->device_fd, &c2[i]->device_stat ) != 0 )
        {
            nwipe_perror( errno, __FUNCTION__, "fstat" );
            nwipe_log( NWIPE_LOG_ERROR, "Unable to stat file '%s'.", c2[i]->device_name );
            nwipe_error++;
            continue;
        }

        /* Check that the file is a block device. */
        if( !S_ISBLK( c2[i]->device_stat.st_mode ) )
        {
            nwipe_log( NWIPE_LOG_ERROR, "'%s' is not a block device.", c2[i]->device_name );
            nwipe_error++;
            continue;
        }

        /* TODO: Lock the file for exclusive access. */
        /*
        if( flock( c2[i]->device_fd, LOCK_EX | LOCK_NB ) != 0 )
        {
                nwipe_perror( errno, __FUNCTION__, "flock" );
                nwipe_log( NWIPE_LOG_ERROR, "Unable to lock the '%s' file.", c2[i]->device_name );
                nwipe_error++;
                continue;
        }
        */

        /* Print serial number of device if it exists. */
        if( strlen( (const char*) c2[i]->device_serial_no ) )
        {
            nwipe_log( NWIPE_LOG_INFO, "Device %s has serial number %s", c2[i]->device_name, c2[i]->device_serial_no );
        }

        /* Do sector size and block size checking. */
        if( ioctl( c2[i]->device_fd, BLKSSZGET, &c2[i]->device_sector_size ) == 0 )
        {
            nwipe_log(
                NWIPE_LOG_INFO, "Device '%s' has sector size %i.", c2[i]->device_name, c2[i]->device_sector_size );

            if( ioctl( c2[i]->device_fd, BLKBSZGET, &c2[i]->device_block_size ) == 0 )
            {
                nwipe_log(
                    NWIPE_LOG_INFO, "Device '%s' has block size %i.", c2[i]->device_name, c2[i]->device_block_size );
            }
            else
            {
                nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed BLKBSZGET ioctl.", c2[i]->device_name );
                c2[i]->device_block_size = 0;
            }
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed BLKSSZGET ioctl.", c2[i]->device_name );
            c2[i]->device_sector_size = 0;
            c2[i]->device_block_size = 0;
        }

        /* The st_size field is zero for block devices. */
        /* ioctl( c2[i]->device_fd, BLKGETSIZE64, &c2[i]->device_size ); */

        /* Seek to the end of the device to determine its size. */
        c2[i]->device_size = lseek( c2[i]->device_fd, 0, SEEK_END );

        /* Also ask the driver for the device size. */
        /* if( ioctl( c2[i]->device_fd, BLKGETSIZE64, &size64 ) ) */
        if( ioctl( c2[i]->device_fd, _IOR( 0x12, 114, size_t ), &size64 ) )
        {
            /* The ioctl failed. */
            fprintf( stderr, "Error: BLKGETSIZE64 failed  on '%s'.\n", c2[i]->device_name );
            nwipe_log( NWIPE_LOG_ERROR, "BLKGETSIZE64 failed  on '%s'.\n", c2[i]->device_name );
            nwipe_error++;
        }
        c2[i]->device_size = size64;

        /* Check whether the two size values agree. */
        if( c2[i]->device_size != size64 )
        {
            /* This could be caused by the linux last-odd-block problem. */
            fprintf( stderr, "Error: Last-odd-block detected on '%s'.\n", c2[i]->device_name );
            nwipe_log( NWIPE_LOG_ERROR, "Last-odd-block detected on '%s'.", c2[i]->device_name );
            nwipe_error++;
        }

        if( c2[i]->device_size == (long long) -1 )
        {
            /* We cannot determine the size of this device. */
            nwipe_perror( errno, __FUNCTION__, "lseek" );
            nwipe_log( NWIPE_LOG_ERROR, "Unable to determine the size of '%s'.", c2[i]->device_name );
            nwipe_error++;
        }
        else
        {
            /* Reset the file pointer. */
            r = lseek( c2[i]->device_fd, 0, SEEK_SET );

            if( r == (off64_t) -1 )
            {
                nwipe_perror( errno, __FUNCTION__, "lseek" );
                nwipe_log( NWIPE_LOG_ERROR, "Unable to reset the '%s' file offset.", c2[i]->device_name );
                nwipe_error++;
            }
        }

        if( c2[i]->device_size == 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Device '%s' is size %llu.", c2[i]->device_name, c2[i]->device_size );
            nwipe_error++;
            continue;
        }
        else
        {
            nwipe_log( NWIPE_LOG_INFO, "Device '%s' is size %llu.", c2[i]->device_name, c2[i]->device_size );
        }

        /* Fork a child process. */
        errno = pthread_create( &c2[i]->thread, NULL, nwipe_options.method, (void*) c2[i] );
        if( errno )
        {
            nwipe_perror( errno, __FUNCTION__, "pthread_create" );
            if( !nwipe_options.nogui )
                nwipe_gui_free();
            return errno;
        }
    }

    /* Change the terminal mode to non-blocking input. */
    nodelay( stdscr, 0 );

    /* Set getch to delay in order to slow screen updates. */
    halfdelay( NWIPE_KNOB_SLEEP * 10 );

    /* Set up data structs to pass the GUI thread the data it needs. */
    nwipe_thread_data_ptr_t nwipe_gui_data;
    if( !nwipe_options.nogui )
    {
        nwipe_gui_data.c = c2;
        nwipe_gui_data.nwipe_misc_thread_data = &nwipe_misc_thread_data;
        /* Fork the GUI thread. */
        errno = pthread_create( &nwipe_gui_thread, NULL, nwipe_gui_status, &nwipe_gui_data );
    }

    /* Wait for all the wiping threads to finish, but don't wait if we receive the terminate signal */
    i = 0;
    while( i < nwipe_selected && terminate_signal == 0 )
    {
        if( i == nwipe_selected )
        {
            break;
        }

        if( c2[i]->wipe_status != 0 )
        {
            i = 0;
        }
        else
        {
            i++;
            continue;
        }
        sleep( 2 ); /* DO NOT REMOVE ! Stops the routine hogging CPU cycles */
    }

    if( terminate_signal != 1 )
    {
        if( !nwipe_options.nowait && !nwipe_options.autopoweroff )
        {
            do
            {
                sleep( 1 );
            } while( terminate_signal != 1 );
        }
    }
    
    nwipe_log( NWIPE_LOG_INFO, "Exit in progress" );

    /* Send a REQUEST for the wipe threads to be cancelled */
    for( i = 0; i < nwipe_selected; i++ )
    {

        if( c2[i]->thread )
        {
            nwipe_log( NWIPE_LOG_INFO, "Requesting wipe thread cancellation for %s", c2[i]->device_name );
            nwipe_log( NWIPE_LOG_INFO, "Please wait.." );
            pthread_cancel( c2[i]->thread );
        }
    }

    /* Kill the GUI thread */
    if( nwipe_gui_thread )
    {
        nwipe_log( NWIPE_LOG_INFO, "Cancelling the GUI thread." );

        /* We don't want to use pthread_cancel as our GUI thread is aware of the control-c
         *  signal and will exit itself we just join the GUI thread and wait for confirmation
         */
        r = pthread_join( nwipe_gui_thread, NULL );
        if( r != 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING, "main()>pthread_join():Error when waiting for GUI thread to cancel." );
        }
        nwipe_log( NWIPE_LOG_INFO, "GUI compute_stats thread has been cancelled" );
    }

    /* Release the gui. */
    if( !nwipe_options.nogui )
    {
        nwipe_gui_free();
    }

    /* Now join the wipe threads and wait until they have terminated */
    for( i = 0; i < nwipe_selected; i++ )
    {

        if( c2[i]->thread )
        {
            /* Joins the thread and waits for completion before continuing */
            r = pthread_join( c2[i]->thread, NULL );
            if( r != 0 )
            {
                nwipe_log( NWIPE_LOG_WARNING, "main()>pthread_join():Error when waiting for wipe thread to cancel." );
            }
            c2[i]->thread = 0; /* Zero the thread so we know it's been cancelled */
            nwipe_log( NWIPE_LOG_INFO, "Wipe thread for device %s has been cancelled", c2[i]->device_name );
            /* Close the device file descriptor. */
            close( c2[i]->device_fd );
        }
    }

    for( i = 0; i < nwipe_selected; i++ )
    {
        /* Check for non-fatal errors. */
        if( c2[i]->result > 0 )
        {
            nwipe_log( NWIPE_LOG_FATAL, "Nwipe exited with non fatal errors on device = %s\n", c2[i]->device_name );
            return_status = 1;
        }
    }

    for( i = 0; i < nwipe_selected; i++ )
    {
        /* Check for fatal errors. */
        if( c2[i]->result < 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Nwipe exited with fatal errors on device = %s\n", c2[i]->device_name );
            return_status = -1;
        }
    }

    if( return_status == 0 )
    {
        nwipe_log( NWIPE_LOG_INFO, "Nwipe successfully exited." );
    }

    cleanup();
    
    check_for_autopoweroff();

    /* Exit. */
    return return_status;
}

void* signal_hand( void* ptr )
{
    int sig;

    // Define signals that this handler should react to
    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    sigaddset( &sigset, SIGUSR1 );

    int i;

    /* Set up the structs we will use for the data required. */
    nwipe_thread_data_ptr_t* nwipe_thread_data_ptr;
    nwipe_context_t** c;
    nwipe_misc_thread_data_t* nwipe_misc_thread_data;

    /* Retrieve from the pointer passed to the function. */
    nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t*) ptr;
    c = nwipe_thread_data_ptr->c;
    nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;

    while( 1 )
    {
        /* wait for a signal to arrive */
        sigwait( &sigset, &sig );

        switch( sig )
        {

            // Log current status. All values are automatically updated by the GUI
            case SIGUSR1:
            {
                compute_stats( ptr );

                for( i = 0; i < nwipe_misc_thread_data->nwipe_selected; i++ )
                {

                    if( c[i]->thread )
                    {
                        char* status = "";
                        switch( c[i]->pass_type )
                        {
                            case NWIPE_PASS_FINAL_BLANK:
                                status = "[blanking]";
                                break;

                            case NWIPE_PASS_FINAL_OPS2:
                                status = "[OPS-II final]";
                                break;

                            case NWIPE_PASS_WRITE:
                                status = "[writing]";
                                break;

                            case NWIPE_PASS_VERIFY:
                                status = "[verifying]";
                                break;

                            case NWIPE_PASS_NONE:
                                break;
                        }
                        if( c[i]->sync_status )
                        {
                            status = "[syncing]";
                        }
                        nwipe_log( NWIPE_LOG_INFO,
                                   "%s: %05.2f%%, round %i of %i, pass %i of %i %s",
                                   c[i]->device_name,
                                   c[i]->round_percent,
                                   c[i]->round_working,
                                   c[i]->round_count,
                                   c[i]->pass_working,
                                   c[i]->pass_count,
                                   status );
                    }
                    else
                    {
                        if( c[i]->result == 0 )
                        {
                            nwipe_log( NWIPE_LOG_INFO, "%s: Success", c[i]->device_name );
                        }
                        else if( c[i]->signal )
                        {
                            nwipe_log(
                                NWIPE_LOG_INFO, "%s: >>> FAILURE! <<<: signal %i", c[i]->device_name, c[i]->signal );
                        }
                        else
                        {
                            nwipe_log(
                                NWIPE_LOG_INFO, "%s: >>> FAILURE! <<<: code %i", c[i]->device_name, c[i]->result );
                        }
                    }
                }

                break;
            }

            case SIGHUP:
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
            {
                /* Set termination flag for main() which will do housekeeping prior to exit */
                terminate_signal = 1;

                /* Return control to the main thread, returning the signal received */
                return ( (void*) 0 );

                break;
            }
        }
    }

    return ( 0 );
}

int cleanup()
{
    int i;
    extern int log_elements_displayed;
    extern int log_elements_allocated;
    extern char** log_lines;

    /* Print the logs held in memory. */
    for( i = log_elements_displayed; i < log_elements_allocated; i++ )
    {
        printf( "%s\n", log_lines[i] );
    }
    fflush( stdout );

    /* Deallocate memory used by logging */
    if( log_elements_allocated != 0 )
    {
        for( i = 0; i < log_elements_allocated; i++ )
        {
            free( log_lines[i] );
        }
        log_elements_allocated = 0;  // zeroed just in case cleanup is called twice.
        free( log_lines );
    }

    /* TODO: All other cleanup required */

    return 0;
}
void check_for_autopoweroff( void )
{
    char cmd[]="shutdown -P +1 \"System going down in one minute\"";
    FILE* fp;
    int r;  // A result buffer.
    
    /* User request auto power down ? */
    if( nwipe_options.autopoweroff == 1 )
    {
        fp = popen( cmd, "r" );
        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_INFO, "Failed to autopoweroff to %s", cmd );
            return;
        }
        r = pclose( fp );
    }
}
