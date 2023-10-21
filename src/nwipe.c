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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "device.h"
#include "logging.h"
#include "gui.h"
#include "temperature.h"
#include "miscellaneous.h"

#include <sys/ioctl.h> /* FIXME: Twice Included */
#include <sys/shm.h>
#include <wait.h>

#include <parted/parted.h>
#include <parted/debug.h>
#include "conf.h"
#include "version.h"
#include "hpa_dco.h"
#include "conf.h"
#include <libconfig.h>

int terminate_signal;
int user_abort;
int global_wipe_status;

/* helper function for sorting */
int devnamecmp( const void* a, const void* b )
{
    // nwipe_log( NWIPE_LOG_DEBUG, "a: %s, b: %s", ( *( nwipe_context_t** ) a)->device_name, ( *( nwipe_context_t** )
    // b)->device_name );

    int ret = strcmp( ( *(nwipe_context_t**) a )->device_name, ( *(nwipe_context_t**) b )->device_name );
    return ( ret );
}

int main( int argc, char** argv )
{
    int nwipe_optind;  // The result of nwipe_options().
    int nwipe_enumerated;  // The number of contexts that have been enumerated.
    int nwipe_error = 0;  // An error counter.
    int nwipe_selected = 0;  // The number of contexts that have been selected.
    int any_threads_still_running;  // used in wipe thread cancellation wait loop
    int thread_timeout_counter;  // timeout thread cancellation after THREAD_CANCELLATION_TIMEOUT seconds
    pthread_t nwipe_gui_thread = 0;  // The thread ID of the GUI thread.
    pthread_t nwipe_temperature_thread = 0;  // The thread ID of the temperature update thread
    pthread_t nwipe_sigint_thread;  // The thread ID of the sigint handler.

    char modprobe_command[] = "modprobe %s";
    char modprobe_command2[] = "/sbin/modprobe %s";
    char modprobe_command3[] = "/usr/sbin/modprobe %s";
    char module_shortform[50];
    char final_cmd_modprobe[sizeof( modprobe_command ) + sizeof( module_shortform )];

    /* The entropy source file handle. */
    int nwipe_entropy;

    /* The generic index variables. */
    int i;
    int j;

    /* The generic result buffer. */
    int r;

    /* Initialise the termintaion signal, 1=terminate nwipe */
    terminate_signal = 0;

    /* Initialise the user abort signal, 1=User aborted with CNTRL-C,SIGTERM, SIGQUIT, SIGINT etc.. */
    user_abort = 0;

    /* nwipes return status value, set prior to exit at the end of nwipe, as no other exit points allowed */
    int return_status = 0;

    /* Initialise, flag indicating whether a wipe has actually started or not 0=no, 1=yes */
    global_wipe_status = 0;

    /* Initialise flags that indicate whether a fatal or non fatal error occurred on ANY drive */
    int fatal_errors_flag = 0;
    int non_fatal_errors_flag = 0;

    /* Two arrays are used, containing pointers to the the typedef for each disk */
    /* The first array (c1) points to all devices, the second points to only     */
    /* the disks selected for wiping.                                            */

    /* The array of pointers to enumerated contexts. */
    /* Initialised and populated in device scan.     */
    nwipe_context_t** c1 = 0;

    int wipe_threads_started = 0;

    /** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE ** NOTE **
     * Important Note: if you want nwipe_log messages to go into the logfile
     * any 'nwipe_log()' commands must appear after the options are parsed here,
     * else they will appear in the console but not in the logfile, that is,
     * assuming you specified a log file on the command line as an nwipe option.
     */

    /*****************************
     * Parse command line options.
     */

    /* Initialise the libconfig code that handles nwipe.conf */
    nwipe_conf_init();

    nwipe_optind = nwipe_options_parse( argc, argv );

    /* Log nwipes version */
    nwipe_log( NWIPE_LOG_INFO, "%s", banner );

    /* Log OS info */
    nwipe_log_OSinfo();

    /* Check that hdparm exists, we use hdparm for some HPA/DCO detection etc, if not
     * exit nwipe. These checks are required if the PATH environment is not setup !
     * Example: Debian sid 'su' as opposed to 'su -'
     */
    if( system( "which hdparm > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/hdparm > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/hdparm > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "hdparm command not found." );
                nwipe_log( NWIPE_LOG_WARNING,
                           "Required by nwipe for HPA/DCO detection & correction and ATA secure erase." );
                nwipe_log( NWIPE_LOG_WARNING, "** Please install hdparm **\n" );
                cleanup();
                exit( 1 );
            }
        }
    }

    /* Check if the given path for PDF reports is a writeable directory */
    if( strcmp( nwipe_options.PDFreportpath, "noPDF" ) != 0 )
    {
        if( access( nwipe_options.PDFreportpath, W_OK ) != 0 )
        {
            nwipe_log( NWIPE_LOG_ERROR, "PDFreportpath %s is not a writeable directory.", nwipe_options.PDFreportpath );
            cleanup();
            exit( 2 );
        }
    }

    if( nwipe_optind == argc )
    {
        /* File names were not given by the user.  Scan for devices. */
        nwipe_enumerated = nwipe_device_scan( &c1 );

        if( terminate_signal == 1 )
        {
            cleanup();
            exit( 1 );
        }

        if( nwipe_enumerated == 0 )
        {
            nwipe_log( NWIPE_LOG_INFO,
                       "Storage devices not found. Nwipe should be run as root or sudo/su, i.e sudo nwipe etc" );
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
            nwipe_log( NWIPE_LOG_ERROR, "Devices not found. Check you're not excluding drives unnecessarily," );
            nwipe_log( NWIPE_LOG_ERROR, "and you are running nwipe as sudo or as root." );
            printf( "Devices not found, check you're not excluding drives unnecessarily \n and you are running nwipe "
                    "as sudo or as root." );
            cleanup();
            exit( 1 );
        }
    }

    /* sort list of devices here */
    qsort( (void*) c1, (size_t) nwipe_enumerated, sizeof( nwipe_context_t* ), devnamecmp );

    if( terminate_signal == 1 )
    {
        cleanup();
        exit( 1 );
    }

    /* Log the System information */
    nwipe_log_sysinfo();

    /* The array of pointers to contexts that will actually be wiped. */
    nwipe_context_t** c2 = (nwipe_context_t**) malloc( nwipe_enumerated * sizeof( nwipe_context_t* ) );
    if( c2 == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "memory allocation for c2 failed" );
        cleanup();
        exit( 1 );
    }

    /* Open the entropy source. */
    nwipe_entropy = open( NWIPE_KNOB_ENTROPY, O_RDONLY );

    /* Check the result. */
    if( nwipe_entropy < 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "open" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to open entropy source %s.", NWIPE_KNOB_ENTROPY );
        cleanup();
        free( c2 );
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

    /* Makesure the drivetemp module is loaded, else drives hwmon entries won't appear in /sys/class/hwmon */
    final_cmd_modprobe[0] = 0;

    /* The kernel module we are going to load */
    strcpy( module_shortform, "drivetemp" );

    /* Determine whether we can access modprobe, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */

    if( system( "which modprobe > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/modprobe > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/sbin/modprobe > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "modprobe command not found. Install kmod package (modprobe)) !" );
                nwipe_log( NWIPE_LOG_WARNING, "Most temperature monitoring may be unavailable as module drivetemp" );
                nwipe_log( NWIPE_LOG_WARNING, "could not be loaded. drivetemp is not available on kernels < v5.5" );
            }
            else
            {
                sprintf( final_cmd_modprobe, modprobe_command3, module_shortform );
            }
        }
        else
        {
            sprintf( final_cmd_modprobe, modprobe_command2, module_shortform );
        }
    }
    else
    {
        sprintf( final_cmd_modprobe, modprobe_command, module_shortform );
    }

    /* load the drivetemp module */
    if( system( final_cmd_modprobe ) != 0 )
    {
        nwipe_log( NWIPE_LOG_WARNING, "hwmon: Unable to load module drivetemp, temperatures may be unavailable." );
        nwipe_log( NWIPE_LOG_WARNING, "hwmon: It's possible the drivetemp software isn't modular but built-in" );
        nwipe_log( NWIPE_LOG_WARNING, "hwmon: to the kernel, as is the case with ShredOS.x86_64 in which case" );
        nwipe_log( NWIPE_LOG_WARNING, "hwmon: the temperatures will actually be available despite this issue." );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE, "hwmon: Module drivetemp loaded, drive temperatures available" );
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

        /* Initialise temperature variables for device */
        nwipe_init_temperature( c1[i] );
        if( nwipe_options.verbose )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "hwmon: Device %s hwmon path = %s", c1[i]->device_name, c1[i]->temp1_path );
        }

        // nwipe_update_temperature( c1[i] );

        /* Log the temperature crtical, highest, lowest and lowest critical temperature
         * limits to nwipes log file using the INFO catagory
         */

        nwipe_log_drives_temperature_limits( c1[i] );
    }

    /* Check for initialization errors. */
    if( nwipe_error )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Initialization error %i\n", nwipe_error );
        cleanup();
        return -1;
    }

    /* Set up the data structures to pass the temperature thread the data it needs */
    nwipe_thread_data_ptr_t nwipe_temperature_thread_data;
    nwipe_temperature_thread_data.c = c1;
    nwipe_temperature_thread_data.nwipe_misc_thread_data = &nwipe_misc_thread_data;

    /* Fork the temperature thread */
    errno = pthread_create(
        &nwipe_temperature_thread, NULL, nwipe_update_temperature_thread, &nwipe_temperature_thread_data );

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
            if( nwipe_options.PDF_preview_details == 1 )
            {
                nwipe_gui_preview_org_customer( SHOWING_PRIOR_TO_DRIVE_SELECTION );
            }

            nwipe_gui_select( nwipe_enumerated, c1 );
        }
    }

    /* Initialise some of the variables in the drive contexts
     */
    for( i = 0; i < nwipe_enumerated; i++ )
    {
        /* Set the PRNG implementation, which must always come after the function nwipe_gui_select ! */
        c1[i]->prng = nwipe_options.prng;
        c1[i]->prng_seed.length = 0;
        c1[i]->prng_seed.s = 0;
        c1[i]->prng_state = 0;

        /* Count the number of selected contexts. */
        if( c1[i]->select == NWIPE_SELECT_TRUE )
        {
            nwipe_selected += 1;
        }

        /* Initialise the wipe result value */
        c1[i]->result = 0;

        /* Initialise the variable that tracks how much of the drive has been erased */
        c1[i]->bytes_erased = 0;
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
    if( user_abort == 0 )
    {
        /* Log the wipe options that have been selected immediately prior to the start of the wipe */
        nwipe_options_log();

        /* The wipe has been initiated */
        global_wipe_status = 1;

        for( i = 0; i < nwipe_selected; i++ )
        {
            /* A result buffer for the BLKGETSIZE64 ioctl. */
            u64 size64;

            /* Initialise the spinner character index */
            c2[i]->spinner_idx = 0;

            /* Initialise the start and end time of the wipe */
            c2[i]->start_time = 0;
            c2[i]->end_time = 0;

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
                nwipe_log( NWIPE_LOG_NOTICE, "%s has serial number %s", c2[i]->device_name, c2[i]->device_serial_no );
            }

            /* Do sector size and block size checking. */
            if( ioctl( c2[i]->device_fd, BLKSSZGET, &c2[i]->device_sector_size ) == 0 )
            {

                if( ioctl( c2[i]->device_fd, BLKBSZGET, &c2[i]->device_block_size ) != 0 )
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
                nwipe_log( NWIPE_LOG_ERROR,
                           "%s, sect/blk/dev %i/%i/%llu",
                           c2[i]->device_name,
                           c2[i]->device_sector_size,
                           c2[i]->device_block_size,
                           c2[i]->device_size );
                nwipe_error++;
                continue;
            }
            else
            {
                nwipe_log( NWIPE_LOG_NOTICE,
                           "%s, sect/blk/dev %i/%i/%llu",
                           c2[i]->device_name,
                           c2[i]->device_sector_size,
                           c2[i]->device_block_size,
                           c2[i]->device_size );
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
            else
            {
                wipe_threads_started = 1;
            }
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

    /* set getch delay to 2/10th second. */
    halfdelay( 10 );

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
        sleep( 1 ); /* DO NOT REMOVE ! Stops the routine hogging CPU cycles */
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
    if( nwipe_options.verbose )
    {
        nwipe_log( NWIPE_LOG_INFO, "Exit in progress" );
    }
    /* Send a REQUEST for the wipe threads to be cancelled */
    for( i = 0; i < nwipe_selected; i++ )
    {

        if( c2[i]->thread )
        {
            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_INFO, "Requesting wipe thread cancellation for %s", c2[i]->device_name );
            }
            pthread_cancel( c2[i]->thread );
        }
    }

    /* Kill the GUI thread */
    if( nwipe_gui_thread )
    {
        if( nwipe_options.verbose )
        {
            nwipe_log( NWIPE_LOG_INFO, "Cancelling the GUI thread." );
        }

        /* We don't want to use pthread_cancel as our GUI thread is aware of the control-c
         *  signal and will exit itself we just join the GUI thread and wait for confirmation
         */
        r = pthread_join( nwipe_gui_thread, NULL );
        if( r != 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING, "main()>pthread_join():Error when waiting for GUI thread to cancel." );
        }
        else
        {
            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_INFO, "GUI compute_stats thread has been cancelled" );
            }
        }
    }

    /* Kill the temperature update thread */
    if( nwipe_temperature_thread )
    {
        if( nwipe_options.verbose )
        {
            nwipe_log( NWIPE_LOG_INFO, "Cancelling the temperature thread." );
        }

        /* We don't want to use pthread_cancel as our temperature thread is aware of the control-c
         *  signal and will exit itself we just join the temperature thread and wait for confirmation
         */
        r = pthread_join( nwipe_temperature_thread, NULL );
        if( r != 0 )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "main()>pthread_join():Error when waiting for temperature thread to cancel." );
        }
        else
        {
            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_INFO, "temperature thread has been cancelled" );
            }
        }
    }

    /* Release the gui. */
    if( !nwipe_options.nogui )
    {
        nwipe_gui_free();
    }

    /* Now join the wipe threads and wait until they have terminated */
    any_threads_still_running = 1;
    thread_timeout_counter = THREAD_CANCELLATION_TIMEOUT;
    while( any_threads_still_running )
    {
        /* quit waiting if we've tried 'thread_timeout_counter' times */
        if( thread_timeout_counter == 0 )
        {
            break;
        }

        any_threads_still_running = 0;
        for( i = 0; i < nwipe_selected; i++ )
        {
            if( c2[i]->thread )
            {
                printf( "\nWaiting for wipe thread to cancel for %s\n", c2[i]->device_name );

                /* Joins the thread and waits for completion before continuing */
                r = pthread_join( c2[i]->thread, NULL );
                if( r != 0 )
                {
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Error joining the wipe thread when waiting for thread to cancel.",
                               c2[i]->device_name );

                    if( r == EDEADLK )
                    {
                        nwipe_log( NWIPE_LOG_ERROR,
                                   "Error joining the wipe thread: EDEADLK: Deadlock detected.",
                                   c2[i]->device_name );
                    }
                    else
                    {
                        if( r == EINVAL )
                        {
                            nwipe_log( NWIPE_LOG_ERROR,
                                       "Error joining the wipe thread: %s EINVAL: thread is not joinable.",
                                       c2[i]->device_name );
                        }
                        else
                        {
                            if( r == ESRCH )
                            {
                                nwipe_log( NWIPE_LOG_ERROR,
                                           "Error joining the wipe thread: %s ESRCH: no matching thread found",
                                           c2[i]->device_name );
                            }
                        }
                    }

                    any_threads_still_running = 1;
                }
                else
                {
                    c2[i]->thread = 0; /* Zero the thread so we know it's been cancelled */

                    if( nwipe_options.verbose )
                    {
                        nwipe_log( NWIPE_LOG_INFO, "Wipe thread for device %s has terminated", c2[i]->device_name );
                    }

                    /* Close the device file descriptor. */
                    close( c2[i]->device_fd );
                }
            }
        }
        thread_timeout_counter--;
        sleep( 1 );
    }
    if( nwipe_options.verbose )
    {
        for( i = 0; i < nwipe_selected; i++ )
        {
            nwipe_log( NWIPE_LOG_DEBUG,
                       "Status: %s, result=%d, pass_errors=%llu, verify_errors=%llu, fsync_errors=%llu",
                       c2[i]->device_name,
                       c2[i]->result,
                       c2[i]->pass_errors,
                       c2[i]->verify_errors,
                       c2[i]->fsyncdata_errors );
        }
    }

    /* if no wipe threads started then zero each selected drive result flag,
     * as we don't need to report fatal/non fatal errors if no wipes were ever started ! */
    if( wipe_threads_started == 0 )
    {
        for( i = 0; i < nwipe_selected; i++ )
        {
            c2[i]->result = 0;
        }
    }
    else
    {
        for( i = 0; i < nwipe_selected; i++ )
        {
            /* Check for errors. */
            if( c2[i]->result != 0 || c2[i]->pass_errors != 0 || c2[i]->verify_errors != 0
                || c2[i]->fsyncdata_errors != 0 )
            {
                /* If the run_method ever returns anything other than zero then makesure there is at least one pass
                 * error This is so that the log summary tables correctly show a failure when one occurs as it only
                 * shows pass, verification and fdatasync errors. */
                if( c2[i]->result != 0 )
                {
                    if( c2[i]->pass_errors == 0 )
                    {
                        c2[i]->pass_errors = 1;
                    }
                }

                nwipe_log( NWIPE_LOG_FATAL,
                           "Nwipe exited with errors on device = %s, see log for specific error\n",
                           c2[i]->device_name );
                nwipe_log( NWIPE_LOG_DEBUG,
                           "Status: %s, result=%d, pass_errors=%llu, verify_errors=%llu, fsync_errors=%llu",
                           c2[i]->device_name,
                           c2[i]->result,
                           c2[i]->pass_errors,
                           c2[i]->verify_errors,
                           c2[i]->fsyncdata_errors );
                non_fatal_errors_flag = 1;
                return_status = 1;
            }
        }
    }

    /* Generate and send the drive status summary to the log */
    nwipe_log_summary( c2, nwipe_selected );

    /* Print a one line status message for the user */
    if( return_status == 0 || return_status == 1 )
    {
        if( user_abort == 1 )
        {
            if( global_wipe_status == 1 )
            {
                nwipe_log( NWIPE_LOG_INFO,
                           "Nwipe was aborted by the user. Check the summary table for the drive status." );
            }
            else
            {
                nwipe_log( NWIPE_LOG_INFO, "Nwipe was aborted by the user prior to the wipe starting." );
            }
        }
        else
        {
            if( fatal_errors_flag == 1 || non_fatal_errors_flag == 1 )
            {
                nwipe_log( NWIPE_LOG_INFO,
                           "Nwipe exited with errors, check the log & summary table for individual drive status." );
            }
            else
            {
                nwipe_log( NWIPE_LOG_INFO, "Nwipe successfully completed. See summary table for details." );
            }
        }
    }

    cleanup();

    check_for_autopoweroff();

    /* Exit. */
    return return_status;
}

void* signal_hand( void* ptr )
{
    int sig;
    int hours;
    int minutes;
    int seconds;

    hours = 0;
    minutes = 0;
    seconds = 0;

    // Define signals that this handler should react to
    sigset_t sigset;
    sigemptyset( &sigset );
    sigaddset( &sigset, SIGHUP );
    sigaddset( &sigset, SIGTERM );
    sigaddset( &sigset, SIGQUIT );
    sigaddset( &sigset, SIGINT );
    sigaddset( &sigset, SIGUSR1 );

    int i;
    char eta[9];

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

                        convert_seconds_to_hours_minutes_seconds( c[i]->eta, &hours, &minutes, &seconds );

                        nwipe_log( NWIPE_LOG_INFO,
                                   "%s: %05.2f%%, round %i of %i, pass %i of %i, eta %02i:%02i:%02i, %s",
                                   c[i]->device_name,
                                   c[i]->round_percent,
                                   c[i]->round_working,
                                   c[i]->round_count,
                                   c[i]->pass_working,
                                   c[i]->pass_count,
                                   hours,
                                   minutes,
                                   seconds,
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

            case SIGHUP:
            case SIGINT:
            case SIGQUIT:
            case SIGTERM:
                /* Set termination flag for main() which will do housekeeping prior to exit */
                terminate_signal = 1;

                /* Set the user abort flag */
                user_abort = 1;

                /* Return control to the main thread, returning the signal received */
                return ( (void*) 0 );

                break;
        }
    }

    return ( 0 );
}

int cleanup()
{
    int i;
    extern int log_elements_displayed;  // initialised and found in logging.c
    extern int log_elements_allocated;  // initialised and found in logging.c
    extern char** log_lines;
    extern config_t nwipe_cfg;

    /* Print the logs held in memory to the console */
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

    /* Deallocate libconfig resources */
    config_destroy( &nwipe_cfg );

    /* TODO: Any other cleanup required ? */

    return 0;
}
void check_for_autopoweroff( void )
{
    char cmd[] = "shutdown -Ph +1 \"System going down in one minute\"";
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
