/*
 *  logging.c:  Logging facilities for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdarg.h"
#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "logging.h"
#include "create_pdf.h"
#include "miscellaneous.h"

/* Global array to hold log values to print when logging to STDOUT */
char** log_lines;
int log_current_element = 0;
int log_elements_allocated = 0;
int log_elements_displayed = 0;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

void nwipe_log( nwipe_log_t level, const char* format, ... )
{
    /**
     *  Writes a message to the program log file.
     *
     */

    extern int terminate_signal;
    extern int user_abort;

    char** result;
    char* malloc_result;
    char message_buffer[MAX_LOG_LINE_CHARS * sizeof( char )];
    int chars_written;

    int message_buffer_length;
    int r; /* result buffer */

    /* A time buffer. */
    time_t t;

    /* A pointer to the system time struct. */
    struct tm* p;
    r = pthread_mutex_lock( &mutex1 );
    if( r != 0 )
    {
        fprintf( stderr, "nwipe_log: pthread_mutex_lock failed. Code %i \n", r );
        return;
    }

    /* Get the current time. */
    t = time( NULL );
    p = localtime( &t );

    /* Position of writing to current log string */
    int line_current_pos = 0;

    /* initialise characters written */
    chars_written = 0;

    /* Only log messages with the debug label if the command line --verbose
     * options has been specified, otherwise just return
     */
    if( level == NWIPE_LOG_DEBUG && nwipe_options.verbose == 0 )
    {
        r = pthread_mutex_unlock( &mutex1 );
        if( r != 0 )
        {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
        }
        return;
    }

    /* Print the date. The rc script uses the same format. */
    if( level != NWIPE_LOG_NOTIMESTAMP )
    {
        chars_written = snprintf( message_buffer,
                                  MAX_LOG_LINE_CHARS,
                                  "[%i/%02i/%02i %02i:%02i:%02i] ",
                                  1900 + p->tm_year,
                                  1 + p->tm_mon,
                                  p->tm_mday,
                                  p->tm_hour,
                                  p->tm_min,
                                  p->tm_sec );
    }

    /*
     * Has the end of the buffer been reached ?, snprintf returns the number of characters that would have been
     * written if MAX_LOG_LINE_CHARS had not been reached, it does not return the actual characters written in
     * all circumstances, hence why we need to check whether it's greater than MAX_LOG_LINE_CHARS and if so set
     * it to MAX_LOG_LINE_CHARS, preventing a buffer overrun further down this function.
     */

    /* check if there was a complete failure to write this part of the message, in which case return */
    if( chars_written < 0 )
    {
        fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
        r = pthread_mutex_unlock( &mutex1 );
        if( r != 0 )
        {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            return;
        }
    }
    else
    {
        if( ( line_current_pos + chars_written ) > MAX_LOG_LINE_CHARS )
        {
            fprintf( stderr,
                     "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n",
                     MAX_LOG_LINE_CHARS );
            line_current_pos = MAX_LOG_LINE_CHARS;
        }
        else
        {
            line_current_pos += chars_written;
        }
    }

    if( line_current_pos < MAX_LOG_LINE_CHARS )
    {
        switch( level )
        {

            case NWIPE_LOG_NONE:
            case NWIPE_LOG_NOTIMESTAMP:
                /* Do nothing. */
                break;

                /* NOTE! The debug labels, i.e. debug, info, notice etc should be left padded with spaces, in order
                 * to maintain column alignment. Pad a label to achieve the length of whatever the longest label happens
                 * to be. Important to know if you are thinking of adding another label.
                 */

            case NWIPE_LOG_DEBUG:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "  debug: " );
                break;

            case NWIPE_LOG_INFO:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "   info: " );
                break;

            case NWIPE_LOG_NOTICE:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, " notice: " );
                break;

            case NWIPE_LOG_WARNING:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "warning: " );
                break;

            case NWIPE_LOG_ERROR:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "  error: " );
                break;

            case NWIPE_LOG_FATAL:
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "  fatal: " );
                break;

            case NWIPE_LOG_SANITY:
                /* TODO: Request that the user report the log. */
                chars_written =
                    snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, " sanity: " );
                break;

            default:
                chars_written = snprintf(
                    message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "level %i: ", level );
        }

        /*
         * Has the end of the buffer been reached ?
         */
        if( chars_written < 0 )
        {
            fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
            r = pthread_mutex_unlock( &mutex1 );
            if( r != 0 )
            {
                fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
                return;
            }
        }
        else
        {
            if( ( line_current_pos + chars_written ) > MAX_LOG_LINE_CHARS )
            {
                fprintf( stderr,
                         "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n",
                         MAX_LOG_LINE_CHARS );
                line_current_pos = MAX_LOG_LINE_CHARS;
            }
            else
            {
                line_current_pos += chars_written;
            }
        }
    }

    /* The variable argument pointer. */
    va_list ap;

    /* Fetch the argument list. */
    va_start( ap, format );

    /* Print the event. */
    if( line_current_pos < MAX_LOG_LINE_CHARS )
    {
        chars_written =
            vsnprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos - 1, format, ap );

        if( chars_written < 0 )
        {
            fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
            r = pthread_mutex_unlock( &mutex1 );
            if( r != 0 )
            {
                fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
                va_end( ap );
                return;
            }
        }
        else
        {
            if( ( line_current_pos + chars_written ) > MAX_LOG_LINE_CHARS )
            {
                fprintf( stderr,
                         "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n",
                         MAX_LOG_LINE_CHARS );
                line_current_pos = MAX_LOG_LINE_CHARS;
            }
            else
            {
                line_current_pos += chars_written;
            }
        }
    }

    fflush( stdout );
    /* Increase the current log element pointer - we will write here, deallocation is done in cleanup() in nwipe.c */
    if( log_current_element == log_elements_allocated )
    {
        log_elements_allocated++;
        result = realloc( log_lines, ( log_elements_allocated ) * sizeof( char* ) );
        if( result == NULL )
        {
            fprintf( stderr, "nwipe_log: realloc failed when adding a log line.\n" );
            r = pthread_mutex_unlock( &mutex1 );
            if( r != 0 )
            {
                fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
                va_end( ap );
                return;
            }
        }
        log_lines = result;

        /* Allocate memory for storing a single log message, deallocation is done in cleanup() in nwipe.c */
        message_buffer_length = strlen( message_buffer ) * sizeof( char );
        malloc_result = malloc( ( message_buffer_length + 1 ) * sizeof( char ) );
        if( malloc_result == NULL )
        {
            fprintf( stderr, "nwipe_log: malloc failed when adding a log line.\n" );
            r = pthread_mutex_unlock( &mutex1 );
            if( r != 0 )
            {
                fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
                va_end( ap );
                return;
            }
        }
        log_lines[log_current_element] = malloc_result;
    }

    strcpy( log_lines[log_current_element], message_buffer );

    /*
        if( level >= NWIPE_LOG_WARNING )
        {
            vfprintf( stderr, format, ap );
        }
    */

    /* Release the argument list. */
    va_end( ap );

    /*
        if( level >= NWIPE_LOG_WARNING )
        {
            fprintf( stderr, "\n" );
        }
    */

    /* The log file pointer. */
    FILE* fp;

    /* The log file descriptor. */
    int fd;

    if( nwipe_options.logfile[0] == '\0' )
    {
        if( nwipe_options.nogui )
        {
            printf( "%s\n", log_lines[log_current_element] );
            log_elements_displayed++;
        }
    }
    else
    {
        /* Open the log file for appending. */
        fp = fopen( nwipe_options.logfile, "a" );

        if( fp != NULL )
        {

            /* Get the file descriptor of the log file. */
            fd = fileno( fp );

            /* Block and lock. */
            r = flock( fd, LOCK_EX );

            if( r != 0 )
            {
                perror( "nwipe_log: flock:" );
                fprintf( stderr, "nwipe_log: Unable to lock '%s' for logging.\n", nwipe_options.logfile );
                r = pthread_mutex_unlock( &mutex1 );
                if( r != 0 )
                {
                    fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );

                    /* Unlock the file. */
                    r = flock( fd, LOCK_UN );
                    fclose( fp );
                    return;
                }
            }

            fprintf( fp, "%s\n", log_lines[log_current_element] );

            /* Unlock the file. */
            r = flock( fd, LOCK_UN );

            if( r != 0 )
            {
                perror( "nwipe_log: flock:" );
                fprintf( stderr, "Error: Unable to unlock '%s' after logging.\n", nwipe_options.logfile );
            }

            /* Close the stream. */
            r = fclose( fp );

            if( r != 0 )
            {
                perror( "nwipe_log: fclose:" );
                fprintf( stderr, "Error: Unable to close '%s' after logging.\n", nwipe_options.logfile );
            }
        }
        else
        {
            /* Tell user we can't create/open the log and terminate nwipe */
            fprintf(
                stderr, "\nERROR:Unable to create/open '%s' for logging, permissions?\n\n", nwipe_options.logfile );
            r = pthread_mutex_unlock( &mutex1 );
            if( r != 0 )
            {
                fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            }
            user_abort = 1;
            terminate_signal = 1;
        }
    }

    log_current_element++;

    r = pthread_mutex_unlock( &mutex1 );
    if( r != 0 )
    {
        fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
    }
    return;

} /* nwipe_log */

void nwipe_perror( int nwipe_errno, const char* f, const char* s )
{
    /**
     * Wrapper for perror().
     */

    nwipe_log( NWIPE_LOG_ERROR, "%s: %s: %s", f, s, strerror( nwipe_errno ) );

} /* nwipe_perror */

void nwipe_log_OSinfo()
{
    /* Read /proc/version, format and write to the log */

    FILE* fp = NULL;
    char OS_info_temp[MAX_SIZE_OS_STRING + 1];
    char OS_info[MAX_SIZE_OS_STRING + 1];
    int idx;
    int idx2;
    int idx3;
    int idx4;

    /* initialise OS_info & OS_info_temp strings */
    idx = 0;
    while( idx < MAX_SIZE_OS_STRING + 1 )
    {
        OS_info_temp[idx] = 0;
        OS_info[idx] = 0;
        idx++;
    }

    /* Open a pipe to /proc/version for reading */
    fp = popen( "cat /proc/version", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_WARNING, "Unable to create a pipe to /proc/version" );
        return;
    }

    /* Read the OS info */
    if( fgets( OS_info_temp, MAX_SIZE_OS_STRING, fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_WARNING, "fget failed to read /proc/version" );
        fclose( fp );
        return;
    }

    /* Format the string for the log, place on multiple lines as necessary,
     * column aligned, left offset with n (OS_info_Line_offset) spaces */
    idx = 0;
    idx2 = 0;
    idx3 = OS_info_Line_Length;

    while( OS_info_temp[idx] != 0 )
    {
        while( idx2 < idx3 && idx2 < MAX_SIZE_OS_STRING )
        {
            /* remove newlines from the source */
            if( OS_info_temp[idx] == 0x0a )
            {
                idx++;
            }

            /* copy the character */
            OS_info[idx2++] = OS_info_temp[idx++];
        }
        if( OS_info_temp[idx] != 0 )
        {
            OS_info[idx2++] = 0x0a;
            idx4 = 0;

            /* left indent with spaces */
            while( idx4 < OS_info_Line_offset && idx2 < MAX_SIZE_OS_STRING )
            {
                OS_info[idx2++] = ' ';
                idx4++;
            }

            /* calculate idx3 ready for next line */
            idx3 += OS_info_Line_offset + OS_info_Line_Length;
        }
        else
        {
            continue;
        }
    }

    nwipe_log( NWIPE_LOG_INFO, "%s", OS_info );
    fclose( fp );
    return;
}

int nwipe_log_sysinfo()
{
    FILE* fp;
    char path[256];
    int len;
    int r;  // A result buffer.

    /*
     * Remove or add keywords to be searched, depending on what information is to
     * be logged, making sure the last entry in the array is a NULL string. To remove
     * an entry simply comment out the keyword with //
     */

    /* The 0/1 after the keyword determines whether the data for this
     * keyword is displayed when -q (anonymize) has been specified
     * by the user. An quick reminder about multi dimensional arrays, the first
     * []=the keyword (0-21) including the empty string. The second [] is the
     * 1 or 0 value (0 or 1). The third [] is the index value into either string.
     */
    char dmidecode_keywords[][2][24] = {
        { "bios-version", "1" },
        { "bios-release-date", "1" },
        { "system-manufacturer", "1" },
        { "system-product-name", "1" },
        { "system-version", "1" },
        { "system-serial-number", "0" },
        { "system-uuid", "0" },
        { "baseboard-manufacturer", "1" },
        { "baseboard-product-name", "1" },
        { "baseboard-version", "1" },
        { "baseboard-serial-number", "0" },
        { "baseboard-asset-tag", "0" },
        { "chassis-manufacturer", "1" },
        { "chassis-type", "1" },
        { "chassis-version", "1" },
        { "chassis-serial-number", "0" },
        { "chassis-asset-tag", "0" },
        { "processor-family", "1" },
        { "processor-manufacturer", "1" },
        { "processor-version", "1" },
        { "processor-frequency", "1" },
        { "", "" }  // terminates the keyword array. DO NOT REMOVE
    };

    char dmidecode_command[] = "dmidecode -s %s";
    char dmidecode_command2[] = "/sbin/dmidecode -s %s";
    char dmidecode_command3[] = "/usr/bin/dmidecode -s %s";
    char* p_dmidecode_command;

    char cmd[sizeof( dmidecode_keywords ) + sizeof( dmidecode_command2 )];

    unsigned int keywords_idx;

    keywords_idx = 0;

    p_dmidecode_command = 0;

    if( system( "which dmidecode > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/dmidecode > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/dmidecode > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install dmidecode !" );
            }
            else
            {
                p_dmidecode_command = &dmidecode_command3[0];
            }
        }
        else
        {
            p_dmidecode_command = &dmidecode_command2[0];
        }
    }
    else
    {
        p_dmidecode_command = &dmidecode_command[0];
    }

    if( p_dmidecode_command != 0 )
    {

        /* Run the dmidecode command to retrieve each dmidecode keyword, one at a time */
        while( dmidecode_keywords[keywords_idx][0][0] != 0 )
        {
            sprintf( cmd, p_dmidecode_command, &dmidecode_keywords[keywords_idx][0][0] );
            fp = popen( cmd, "r" );
            if( fp == NULL )
            {
                nwipe_log( NWIPE_LOG_WARNING, "nwipe_log_sysinfo: Failed to create stream to %s", cmd );
                return 1;
            }
            /* Read the output a line at a time - output it. */
            while( fgets( path, sizeof( path ) - 1, fp ) != NULL )
            {
                /* Remove any trailing return from the string, as nwipe_log automatically adds a return */
                len = strlen( path );
                if( path[len - 1] == '\n' )
                {
                    path[len - 1] = 0;
                }
                if( nwipe_options.quiet )
                {
                    if( *( &dmidecode_keywords[keywords_idx][1][0] ) == '0' )
                    {
                        nwipe_log(
                            NWIPE_LOG_INFO, "%s = %s", &dmidecode_keywords[keywords_idx][0][0], "XXXXXXXXXXXXXXX" );
                    }
                    else
                    {
                        nwipe_log( NWIPE_LOG_INFO, "%s = %s", &dmidecode_keywords[keywords_idx][0][0], path );
                    }
                }
                else
                {
                    nwipe_log( NWIPE_LOG_INFO, "%s = %s", &dmidecode_keywords[keywords_idx][0][0], path );
                }
            }
            /* close */
            r = pclose( fp );
            if( r > 0 )
            {
                nwipe_log( NWIPE_LOG_WARNING,
                           "nwipe_log_sysinfo(): dmidecode failed, \"%s\" exit status = %u",
                           cmd,
                           WEXITSTATUS( r ) );
                return 1;
            }
            keywords_idx++;
        }
    }
    return 0;
}

void nwipe_log_summary( nwipe_context_t** ptr, int nwipe_selected )
{
    /* Prints two summary tables, the first is the device pass and verification summary
     * and the second is the main summary table detaining the drives, status, throughput,
     * model and serial number.
     *
     * This function also calls the create_pdf() function that creates the PDF erasure
     * report file. A page report on the success or failure of the erasure operation
     */

    int i;
    int idx_src;
    int idx_dest;
    char device[18];
    char status[9];
    char throughput[13];
    char total_throughput_string[13];
    char summary_top_border[256];
    char summary_top_column_titles[256];
    char blank[3];
    char verify[3];
    // char duration[5];
    char duration[314];
    char model[18];
    char serial_no[NWIPE_SERIALNUMBER_LENGTH + 1];
    char exclamation_flag[2];
    int hours;
    int minutes;
    int seconds;
    u64 total_duration_seconds;
    u64 total_throughput;
    nwipe_context_t** c;
    c = ptr;

    exclamation_flag[0] = 0;
    device[0] = 0;
    status[0] = 0;
    throughput[0] = 0;
    summary_top_border[0] = 0;
    summary_top_column_titles[0] = 0;
    blank[0] = 0;
    verify[0] = 0;
    duration[0] = 0;
    model[0] = 0;
    serial_no[0] = 0;
    hours = 0;
    minutes = 0;
    seconds = 0;

    /* A time buffer. */
    time_t t;

    /* A pointer to the system time struct. */
    struct tm* p;

    /* Nothing to do, user never started a wipe so no summary table required. */
    if( global_wipe_status == 0 )
    {
        return;
    }

    /* Print the pass and verifications table */

    /* IMPORTANT: Keep maximum columns (line length) to 80 characters for use with 80x30 terminals, Shredos, ALT-F2 etc
     * --------------------------------01234567890123456789012345678901234567890123456789012345678901234567890123456789-*/
    nwipe_log( NWIPE_LOG_NOTIMESTAMP, "" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "******************************** Error Summary *********************************" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP, "!   Device | Pass Errors | Verifications Errors | Fdatasync I\\O Errors" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "--------------------------------------------------------------------------------" );

    for( i = 0; i < nwipe_selected; i++ )
    {
        if( c[i]->pass_errors != 0 || c[i]->verify_errors != 0 || c[i]->fsyncdata_errors != 0 )
        {
            strncpy( exclamation_flag, "!", 1 );
            exclamation_flag[1] = 0;
        }
        else
        {
            strncpy( exclamation_flag, " ", 1 );
            exclamation_flag[1] = 0;
        }

        /* Device name, strip any prefixed /dev/.. leaving up to 6 right justified
         * characters eg "   sda", prefixed with space to 6 characters, note that
         * we are processing the strings right to left */

        idx_dest = 6;
        device[idx_dest--] = 0;
        idx_src = strlen( c[i]->device_name );
        idx_src--;

        nwipe_strip_path( device, c[i]->device_name );

        nwipe_log( NWIPE_LOG_NOTIMESTAMP,
                   "%s %s |  %10llu |           %10llu |           %10llu",
                   exclamation_flag,
                   device,
                   c[i]->pass_errors,
                   c[i]->verify_errors,
                   c[i]->fsyncdata_errors );
    }

    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "********************************************************************************" );

    /* Print the main summary table */

    /* initialise */
    total_throughput = 0;

    /* Get the current time. */
    t = time( NULL );
    p = localtime( &t );
    /* IMPORTANT: Keep maximum columns (line length) to 80 characters for use with 80x30 terminals, Shredos, ALT-F2 etc
     * --------------------------------01234567890123456789012345678901234567890123456789012345678901234567890123456789-*/
    nwipe_log( NWIPE_LOG_NOTIMESTAMP, "" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "********************************* Drive Status *********************************" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP, "!   Device | Status | Thru-put | HH:MM:SS | Model/Serial Number" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "--------------------------------------------------------------------------------" );
    /* Example layout:
     *    "!     sdb |--FAIL--|  120MB/s | 01:22:01 | WD6788.8488YNHj/ZX677888388-N       "
     * ); "      sdc | Erased |  120MB/s | 01:25:04 | WD6784.8488JKGG/ZX677888388-N       " ); "     sdv | Erased |
     * 120MB/s | 01:19:07 | WD6788.848HHDDR/ZX677888388-N       " ); End of Example layout */

    for( i = 0; i < nwipe_selected; i++ )
    {
        /* Device name, strip any prefixed /dev/.. leaving up to 8 right justified
         * characters eg "   sda", prefixed with space to 8 characters, note that
         * we are processing the strings right to left */

        nwipe_strip_path( device, c[i]->device_name );

        extern int user_abort;

        /* Any errors ? if so set the exclamation_flag and fail message,
         * All status messages should be eight characters EXACTLY !
         */
        if( c[i]->pass_errors != 0 || c[i]->verify_errors != 0 || c[i]->fsyncdata_errors != 0 )
        {
            strncpy( exclamation_flag, "!", 1 );
            exclamation_flag[1] = 0;

            strncpy( status, "-FAILED-", 8 );
            status[8] = 0;

            strcpy( c[i]->wipe_status_txt, "FAILED" );  // copy to context for use by certificate
        }
        else
        {
            if( c[i]->wipe_status == 0 /* && user_abort != 1 */ )
            {
                strncpy( exclamation_flag, " ", 1 );
                exclamation_flag[1] = 0;

                strncpy( status, " Erased ", 8 );
                status[8] = 0;

                strcpy( c[i]->wipe_status_txt, "ERASED" );  // copy to context for use by certificate
            }
            else
            {
                if( c[i]->wipe_status == 1 && user_abort == 1 )
                {
                    strncpy( exclamation_flag, "!", 1 );
                    exclamation_flag[1] = 0;

                    strncpy( status, "UABORTED", 8 );
                    status[8] = 0;

                    strcpy( c[i]->wipe_status_txt, "ABORTED" );  // copy to context for use by certificate
                }
                else
                {
                    /* If this ever happens, there is a bug ! */
                    strncpy( exclamation_flag, " ", 1 );
                    exclamation_flag[1] = 0;

                    strncpy( status, "INSANITY", 8 );
                    status[8] = 0;

                    strcpy( c[i]->wipe_status_txt, "INSANITY" );  // copy to context for use by certificate
                }
            }
        }

        /* Determine the size of throughput so that the correct nomenclature can be used */
        Determine_C_B_nomenclature( c[i]->throughput, throughput, 13 );

        /* write the duration string to the drive context for later use by create_pdf() */
        snprintf( c[i]->throughput_txt, sizeof( c[i]->throughput_txt ), "%s", throughput );

        /* Add this devices throughput to the total throughput */
        total_throughput += c[i]->throughput;

        /* Retrieve the duration of the wipe in seconds and convert to hours and minutes and seconds */

        if( c[i]->start_time != 0 && c[i]->end_time != 0 )
        {
            /* For a summary when the wipe has finished */
            c[i]->duration = difftime( c[i]->end_time, c[i]->start_time );
        }
        else
        {
            if( c[i]->start_time != 0 && c[i]->end_time == 0 )
            {
                /* For a summary in the event of a system shutdown, user abort */
                c[i]->duration = difftime( t, c[i]->start_time );

                /* If end_time is zero, which may occur if the wipe is aborted, then set
                 * end_time to current time. Important to do as endtime is used by
                 * the PDF report function */
                c[i]->end_time = time( &t );
            }
        }

        total_duration_seconds = (u64) c[i]->duration;

        /* Convert binary seconds into three binary variables, hours, minutes and seconds */
        convert_seconds_to_hours_minutes_seconds( total_duration_seconds, &hours, &minutes, &seconds );

        /* write the duration string to the drive context for later use by create_pdf() */
        snprintf( c[i]->duration_str, sizeof( c[i]->duration_str ), "%02i:%02i:%02i", hours, minutes, seconds );

        /* Device Model */
        strncpy( model, c[i]->device_model, 17 );
        model[17] = 0;

        /* Serial No. */
        strncpy( serial_no, c[i]->device_serial_no, NWIPE_SERIALNUMBER_LENGTH );
        serial_no[NWIPE_SERIALNUMBER_LENGTH] = 0;
        model[17] = 0;

        nwipe_log( NWIPE_LOG_NOTIMESTAMP,
                   "%s %s |%s| %s/s | %02i:%02i:%02i | %s/%s",
                   exclamation_flag,
                   device,
                   status,
                   throughput,
                   hours,
                   minutes,
                   seconds,
                   model,
                   serial_no );

        /* Create the PDF report/certificate */
        if( nwipe_options.PDF_enable == 1 )
        // if( strcmp( nwipe_options.PDFreportpath, "noPDF" ) != 0 )
        {
            /* to have some progress indication. can help if there are many/slow disks */
            fprintf( stderr, "." );
            create_pdf( c[i] );
        }
    }

    /* Determine the size of throughput so that the correct nomenclature can be used */
    Determine_C_B_nomenclature( total_throughput, total_throughput_string, 13 );

    /* Blank abbreviations used in summary table B=blank, NB=no blank */
    if( nwipe_options.noblank )
    {
        strcpy( blank, "NB" );
    }
    else
    {
        strcpy( blank, "B" );
    }

    /* Verify abbreviations used in summary table */
    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            strcpy( verify, "NV" );
            break;

        case NWIPE_VERIFY_LAST:
            strcpy( verify, "VL" );
            break;

        case NWIPE_VERIFY_ALL:
            strcpy( verify, "VA" );
            break;
    }

    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "--------------------------------------------------------------------------------" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "[%i/%02i/%02i %02i:%02i:%02i] Total Throughput %s/s, %s, %iR+%s+%s",
               1900 + p->tm_year,
               1 + p->tm_mon,
               p->tm_mday,
               p->tm_hour,
               p->tm_min,
               p->tm_sec,
               total_throughput_string,
               nwipe_method_label( nwipe_options.method ),
               nwipe_options.rounds,
               blank,
               verify );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP,
               "********************************************************************************" );
    nwipe_log( NWIPE_LOG_NOTIMESTAMP, "" );

    /* Log information regarding where the PDF certificate is saved but log after the summary table so
     * this information is only printed once.
     */
    if( strcmp( nwipe_options.PDFreportpath, "noPDF" ) != 0 )
    {
        nwipe_log( NWIPE_LOG_NOTIMESTAMP, "Creating PDF report in %s\n", nwipe_options.PDFreportpath );
    }
}
