/*  vi: tabstop=3
 *
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

#include <netinet/in.h>
#include <time.h>
#include <signal.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "device.h"
#include "logging.h"
#include "gui.h"


#include <sys/ioctl.h>  /* FIXME: Twice Included */
#include <sys/shm.h>
#include <wait.h>

#include <parted/parted.h>
#include <parted/debug.h>

int main( int argc, char** argv )
{
        int nwipe_optind;              /* The result of nwipe_options().                    */
        int nwipe_enumerated;          /* The number of contexts that have been enumerated. */
        int nwipe_error = 0;           /* An error counter.                                 */
        int nwipe_selected = 0;        /* The number of contexts that have been selected.   */
        pthread_t nwipe_gui_thread;    /* The thread ID of the GUI thread.                  */
        pthread_t nwipe_sigint_thread; /* The thread ID of the sigint handler.              */

        /* The entropy source file handle. */
        int nwipe_entropy; 

        /* The generic index variables. */
        int i;
        int j;

        /* The generic result buffer. */
        int r;

        /* Two arrays are used, containing pointers to the the typedef for each disk */
        /* The first array (c1) points to all devices, the second points to only     */
        /* the disks selected for wiping.                                            */

        /* The array of pointers to enumerated contexts. */
        /* Initialised and populated in device scan.     */
        nwipe_context_t **c1 = 0;

        /* Parse command line options. */
        nwipe_optind = nwipe_options_parse( argc, argv );

        if( nwipe_optind == argc )
        {
                /* File names were not given by the user.  Scan for devices. */
                
                nwipe_enumerated = nwipe_device_scan( &c1 );

                if( nwipe_enumerated == 0 )
                {
                        nwipe_log( NWIPE_LOG_ERROR, "Storage devices not found." );
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
        }


        /* The array of pointers to contexts that will actually be wiped. */
        nwipe_context_t **c2 = (nwipe_context_t **)malloc(nwipe_enumerated * sizeof(nwipe_context_t *));

        /* Open the entropy source. */
        nwipe_entropy = open( NWIPE_KNOB_ENTROPY, O_RDONLY );

        /* Check the result. */
        if( nwipe_entropy < 0 )
        {
                nwipe_perror( errno, __FUNCTION__, "open" );
                nwipe_log( NWIPE_LOG_FATAL, "Unable to open entropy source %s.", NWIPE_KNOB_ENTROPY );
                return errno;
        }

        nwipe_log( NWIPE_LOG_NOTICE, "Opened entropy source '%s'.", NWIPE_KNOB_ENTROPY );

        /* Block relevant signals in main thread. Any other threads that are     */
        /*        created after this will also block those signals.                */
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGHUP);
        sigaddset(&sigset, SIGTERM);
        sigaddset(&sigset, SIGQUIT);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGUSR1);
        pthread_sigmask(SIG_SETMASK, &sigset, NULL);
  
        /* Create a signal handler thread.  This thread will catch all           */
        /*      signals and decide what to do with them.  This will only         */
        /*      catch nondirected signals.  (I.e., if a thread causes a SIGFPE   */
        /*      then that thread will get that signal.                           */

        /* Pass a pointer to a struct containing all data to the signal handler. */
        nwipe_misc_thread_data_t nwipe_misc_thread_data;
        nwipe_thread_data_ptr_t nwipe_thread_data_ptr;
        
        nwipe_thread_data_ptr.c = c2;
        nwipe_misc_thread_data.nwipe_enumerated = nwipe_enumerated;
        if( !nwipe_options.nogui )
                nwipe_misc_thread_data.gui_thread = &nwipe_gui_thread;
        nwipe_thread_data_ptr.nwipe_misc_thread_data = &nwipe_misc_thread_data;

        pthread_attr_t pthread_attr;
        pthread_attr_init(&pthread_attr);
        pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);

        pthread_create( &nwipe_sigint_thread, &pthread_attr, signal_hand, &nwipe_thread_data_ptr);


        /* A context struct for each device has already been created. */
        /* Now set specific nwipe options */
        for( i = 0; i < nwipe_enumerated; i++ )
        {

                /* Set the entropy source. */
                c1[i]->entropy_fd = nwipe_entropy;

                if( nwipe_options.autonuke )
                {
                        /* When the autonuke option is set, select all disks. */
                        //TODO - partitions
                        //if( c1[i].device_part == 0 ) { c1[i].select = NWIPE_SELECT_TRUE;        }
                        //else                         { c1[i].select = NWIPE_SELECT_TRUE_PARENT; }
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
                c1[i]->prng_seed.s      = 0;
                c1[i]->prng_state       = 0;

        } /* file arguments */

        /* Check for initialization errors. */
        if( nwipe_error ) { return -1; }

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
                        printf("--nogui option must be used with autonuke option\n"); 
                        exit(1);
                }
                else
                {
                        nwipe_gui_select( nwipe_enumerated, c1 );
                }
        }


        /* Count the number of selected contexts. */
        for( i = 0 ; i < nwipe_enumerated ; i++ )
        {
                if( c1[i]->select == NWIPE_SELECT_TRUE )
                {
                        nwipe_selected += 1;
                }
        }

        /* Pass the number selected to the struct for other threads */
        nwipe_misc_thread_data.nwipe_selected = nwipe_selected;

        /* Populate the array of selected contexts. */
        for( i = 0, j = 0 ; i < nwipe_enumerated ; i++ )
        {
                if( c1[i]->select == NWIPE_SELECT_TRUE )
                {
                        /* Copy the context. */
                        c2[j++] = c1[i];
                }
                
        } /* for */

        /* TODO: free c1 and c2 memory. */

        for( i = 0 ; i < nwipe_selected ; i++ )
        {

                /* A result buffer for the BLKGETSIZE64 ioctl. */
                u64 size64;

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
                        nwipe_perror( errno, __FUNCTION__, "fstat");
                        nwipe_log( NWIPE_LOG_ERROR, "Unable to stat file '%s'.", c2[i]->device_name );
                        nwipe_error++;
                        continue;
                }

                /* Check that the file is a block device. */
                if( ! S_ISBLK( c2[i]->device_stat.st_mode ) )
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
                if ( strlen(c2[i]->identity.serial_no) ) {
                        nwipe_log( NWIPE_LOG_INFO, "Device %s has serial number %s", c2[i]->device_name, c2[i]->identity.serial_no);
                }
                

                /* Do sector size and block size checking. */

                if( ioctl( c2[i]->device_fd, BLKSSZGET, &c2[i]->sector_size ) == 0 )
                {
                        nwipe_log( NWIPE_LOG_INFO, "Device '%s' has sector size %i.", c2[i]->device_name,  c2[i]->sector_size );

                        if( ioctl( c2[i]->device_fd, BLKBSZGET, &c2[i]->block_size ) == 0 )
                        {
                                if( c2[i]->block_size != c2[i]->sector_size )
                                {
                                        nwipe_log( NWIPE_LOG_WARNING, "Changing '%s' block size from %i to %i.", c2[i]->device_name, c2[i]->block_size, c2[i]->sector_size );
                                        if( ioctl( c2[i]->device_fd, BLKBSZSET, &c2[i]->sector_size ) == 0 )
                                        {
                                                c2[i]->block_size = c2[i]->sector_size;
                                        }

                                        else
                                        {
                                                nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed BLKBSZSET ioctl.", c2[i]->device_name );
                                        }
                                }
                        }
                        else
                        {
                                nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed BLKBSZGET ioctl.", c2[i]->device_name );
                                c2[i]->block_size  = 0;
                        }
                }

                else
                {
                        nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed BLKSSZGET ioctl.", c2[i]->device_name );
                        c2[i]->sector_size = 0;
                        c2[i]->block_size  = 0;
                }


                /* The st_size field is zero for block devices. */
                /* ioctl( c2[i]->device_fd, BLKGETSIZE64, &c2[i]->device_size ); */

                /* Seek to the end of the device to determine its size. */
                c2[i]->device_size = lseek( c2[i]->device_fd, 0, SEEK_END );

                /* Also ask the driver for the device size. */
                /* if( ioctl( c2[i]->device_fd, BLKGETSIZE64, &size64 ) ) */
                if( ioctl( c2[i]->device_fd, _IOR(0x12,114,size_t), &size64 ) )
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
                        nwipe_log( NWIPE_LOG_ERROR, "Last-odd-block detected on '%s'.", c2[i]->device_name  );
                        nwipe_error++;
                }


                if( c2[i]->device_size == (loff_t)-1 )
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
        
                        if( r == (loff_t)-1 )
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
                        nwipe_log( NWIPE_LOG_INFO, "Device '%s' is size %llu.", c2[i]->device_name,  c2[i]->device_size );
                }


                /* Fork a child process. */
                errno = pthread_create( &c2[i]->thread, NULL, nwipe_options.method, (void*) c2[i]);
                if ( errno ) {
                        nwipe_perror( errno, __FUNCTION__, "pthread_create" );
                        if( !nwipe_options.nogui )
                                nwipe_gui_free();
                        return errno;
                }
                
        } /* new thread */

        
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
                errno = pthread_create( &nwipe_gui_thread, NULL, nwipe_gui_status, &nwipe_gui_data);
        }


        /* Wait for all the wiping threads to finish. */        
        for( i = 0 ; i < nwipe_selected ; i++ )
        {

                if ( c2[i]->thread )
                {
                        pthread_join( c2[i]->thread, NULL);

                        /* Close the device file descriptor. */
                        close( c2[i]->device_fd );
                }
                
        }


        /* Kill the GUI thread */
        /* It may not be running if the program was interrupted */
        if ( nwipe_gui_thread )
        {
                pthread_join( nwipe_gui_thread, NULL );

                nocbreak();
        //        timeout(-1);
                /* Wait for enter key to be pressed unless --nowait
                   was specified. */
                if( !nwipe_options.nowait )
                  getch();
        
                /* Release the gui. */
                nwipe_gui_free();
                
        }
        
        nwipe_log( NWIPE_LOG_NOTICE, "Nwipe exited." );

        for( i = 0 ; i < nwipe_selected ; i++ )
        {
        
                /* Check for fatal errors. */
                if( c2[i]->result < 0 ){ return -1; }
                        
        }

        for( i = 0 ; i < nwipe_selected ; i++ )
        {
        
                /* Check for non-fatal errors. */
                if( c2[i]->result > 0 ){ return 1; }
                
        }

        /* Flush any remaining logs. */
        for (i=0; i < log_current_element; i++)
        {
                printf("%s\n", log_lines[i]);
        }

        /* Success. */
        return 0;

} /* main */


void *signal_hand(void *ptr)
{
        int sig;
        // Define signals that this handler should react to
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGHUP);
        sigaddset(&sigset, SIGTERM);
        sigaddset(&sigset, SIGQUIT);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGUSR1);

        int i;        

        /* Set up the structs we will use for the data required. */
        nwipe_thread_data_ptr_t *nwipe_thread_data_ptr;
        nwipe_context_t **c;
        nwipe_misc_thread_data_t *nwipe_misc_thread_data;

        /* Retrieve from the pointer passed to the function. */
        nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t *) ptr;
        c = nwipe_thread_data_ptr->c;
        nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;

        
        while (1) {
                 /* wait for a signal to arrive */

                 sigwait(&sigset,&sig);

                switch ( sig ) {

                        // Log current status. All values are automatically updated by the GUI
                        case SIGUSR1 :
                        {
                                compute_stats(ptr);

                                for( i = 0; i < nwipe_misc_thread_data->nwipe_selected; i++ )
                                {

                                        if ( c[i]->thread )
                                        {
                                                char *status = "";
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
                                                if( c[i]->sync_status ) { status = "[syncing]"; }
                                                nwipe_log( NWIPE_LOG_INFO, "%s: %05.2f%%, round %i of %i, pass %i of %i %s", \
                                                     c[i]->device_name, c[i]->round_percent, c[i]->round_working, c[i]->round_count, c[i]->pass_working, c[i]->pass_count, status );
                                        }
                                        else
                                        {
                                                if( c[i]->result == 0 ) { nwipe_log( NWIPE_LOG_INFO, "%s: Success", c[i]->device_name ); }
                                                else if( c[i]->signal ) { nwipe_log( NWIPE_LOG_INFO, "%s: Failure: signal %i", c[i]->device_name, c[i]->signal ); }
                                                else                    { nwipe_log( NWIPE_LOG_INFO, "%s: Failure: code %i", c[i]->device_name, c[i]->result ); }
                                        }
                                }

                                break;
                        }
                        
                        case SIGHUP  :
                        case SIGINT  :
                        case SIGQUIT :
                        case SIGTERM :
                        {

                                for( i = 0; i < nwipe_misc_thread_data->nwipe_selected; i++ )
                                {

                                        if ( c[i]->thread )
                                        {
                                                pthread_cancel( c[i]->thread );
                                        }
                                }

                                // Kill the GUI thread
                                if( !nwipe_options.nogui )
                                {
                                        if ( nwipe_misc_thread_data->gui_thread )
                                        {
                                                pthread_cancel( *nwipe_misc_thread_data->gui_thread );
                                                *nwipe_misc_thread_data->gui_thread = 0;
                                        }
                                }

                                if( !nwipe_options.nogui )
                                        nwipe_gui_free();

                                /* Flush any remaining logs. */
                                for (i=0; i < log_current_element; i++)
                                {
                                        printf("%s\n", log_lines[i]);
                                }

                                printf("Program interrupted (caught signal %d)\n", sig); 

                                // Cleanup
                                // TODO: All other cleanup required

                                exit(0);

                        } /* end case */

                } /* end switch */
                                                
        } /* end of while */

        return((void *)0);

} /* end of signal_hand */

 
/* eof */
