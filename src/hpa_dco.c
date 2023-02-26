/*
 *  hpa_dco.c: functions that handle the host protected area (HPA) and
 *  device configuration overlay (DCO)
 *
 *  Copyright PartialVolume <https://github.com/PartialVolume>.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "nwipe.h"
#include "context.h"
#include "version.h"
#include "method.h"
#include "logging.h"
#include "options.h"
#include "hpa_dco.h"
#include "miscellaneous.h"

/* This function makes use of the hdparm program to determine HPA/DCO status. I would have prefered
 * to write this function using low level access to the hardware however I would have needed
 * to understand fully the various edge cases that would need be be dealt with. As I need to add
 * HPA/detection to nwipe as quickly as possible I decided it would just be quicker to utilise hdparm
 * rather than reinvent the wheel. However, I don't like doing it like this as a change in formatted
 * output of hdparm could potentially break HPA/DCO detection requiring a fix. Anybody that wants to
 * re-write this function for a purer nwipe without the use of hdparm then by all means please go
 * ahead and submit a pull request to https://github.com/martijnvanbrummelen/nwipe
 */

int hpa_dco_status( nwipe_context_t* ptr, int pre_or_post )
{
    nwipe_context_t* c;
    c = ptr;

    int r;  // A result buffer.
    int set_return_value;
    int exit_status;
    int hpa_line_found;
    int dco_line_found;

    FILE* fp;
    char path_hdparm_cmd1_get_hpa[] = "hdparm -N";
    char path_hdparm_cmd2_get_hpa[] = "/sbin/hdparm -N";
    char path_hdparm_cmd3_get_hpa[] = "/usr/bin/hdparm -N";

    char path_hdparm_cmd4_get_dco[] = "hdparm --dco-identify";
    char path_hdparm_cmd5_get_dco[] = "/sbin/hdparm --dco-identify";
    char path_hdparm_cmd6_get_dco[] = "/usr/bin/hdparm --dco-identify";

    char result[512];

    char* p;

    /* Use the longest of the 'path_hdparm_cmd.....' strings above to
     *determine size in the strings below
     */
    char hdparm_cmd_get_hpa[sizeof( path_hdparm_cmd3_get_hpa ) + sizeof( c->device_name )];
    char hdparm_cmd_get_dco[sizeof( path_hdparm_cmd6_get_dco ) + sizeof( c->device_name )];

    /* Initialise return value */
    set_return_value = 0;

    /* Construct the command including path to the binary if required, I do it like this to cope
     * with distros that don't setup their paths in a standard way or maybe don't even define a
     * path. By doing this we avoid the 'No such file or directory' message you would otherwise
     * get on some distros. -> debian SID
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
            else
            {
                snprintf( hdparm_cmd_get_hpa,
                          sizeof( hdparm_cmd_get_hpa ),
                          "%s %s\n",
                          path_hdparm_cmd3_get_hpa,
                          c->device_name );
                snprintf( hdparm_cmd_get_dco,
                          sizeof( hdparm_cmd_get_dco ),
                          "%s %s\n",
                          path_hdparm_cmd6_get_dco,
                          c->device_name );
            }
        }
        else
        {
            snprintf(
                hdparm_cmd_get_hpa, sizeof( hdparm_cmd_get_hpa ), "%s %s", path_hdparm_cmd2_get_hpa, c->device_name );
            snprintf(
                hdparm_cmd_get_dco, sizeof( hdparm_cmd_get_dco ), "%s %s\n", path_hdparm_cmd5_get_dco, c->device_name );
        }
    }
    else
    {
        snprintf( hdparm_cmd_get_hpa, sizeof( hdparm_cmd_get_hpa ), "%s %s", path_hdparm_cmd1_get_hpa, c->device_name );
        snprintf(
            hdparm_cmd_get_dco, sizeof( hdparm_cmd_get_dco ), "%s %s\n", path_hdparm_cmd4_get_dco, c->device_name );
    }

    /* Initialise the results buffer, so we don't some how inadvertently process a past result */
    memset( result, 0, sizeof( result ) );

    if( hdparm_cmd_get_hpa[0] != 0 )
    {

        fp = popen( hdparm_cmd_get_hpa, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING, "hpa_dco_status: Failed to create stream to %s", hdparm_cmd_get_hpa );

            set_return_value = 1;
        }

        if( fp != NULL )
        {
            hpa_line_found = 0;  //* init */

            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_DEBUG, "%s \n%s", hdparm_cmd_get_hpa, result );
                }

                /* Change the output of hdparm to lower case and search using lower case strings, to try
                 * to avoid minor changes in case in hdparm's output from breaking HPA/DCO detection
                 */
                strlower( result );  // convert the result to lower case

                /* Scan the hdparm results for HPA is disabled
                 */
                if( strstr( result, "hpa is disabled" ) != 0 )
                {
                    if( pre_or_post == PRE_WIPE_HPA_CHECK )
                    {
                        c->HPA_pre_erase_status = HPA_DISABLED;
                    }
                    else
                    {
                        c->HPA_post_erase_status = HPA_DISABLED;
                    }
                    nwipe_log( NWIPE_LOG_INFO, "[GOOD] The host protected area is disabled on %s", c->device_name );
                    hpa_line_found = 1;
                    break;
                }
                else
                {
                    if( strstr( result, "hpa is enabled" ) != 0 )
                    {
                        c->HPA_pre_erase_status = HPA_ENABLED;
                        nwipe_log(
                            NWIPE_LOG_WARNING, "[BAD] The host protected area is enabled on %s", c->device_name );
                        hpa_line_found = 1;
                        break;
                    }
                    else
                    {
                        if( strstr( result, "invalid" ) != 0 )
                        {
                            c->HPA_pre_erase_status = HPA_ENABLED;
                            nwipe_log( NWIPE_LOG_WARNING,
                                       "[UNSURE] hdparm reports invalid output, buggy drive firmware on %s?",
                                       c->device_name );
                            // We'll assume the HPA values are in the string as we may be able to extract something
                            // meaningful
                            hpa_line_found = 1;
                            break;
                        }
                    }
                }
            }

            /* if the line was found that contains hpa is enabled or disabled message
             * then process the line, extracting the 'hpa set' and 'hpa real' values.
             */
            if( hpa_line_found == 1 )
            {
                /* Extract the 'HPA set' value, the first value in the line and convert
                 * to binary and save in context */
                c->HPA_reported_set = str_ascii_number_to_ll( result );

                /* Extract the 'HPA real' value, the second value in the line and convert
                 * to binary and save in context, this is a little more difficult as sometimes
                 * a odd value is returned so instead of nnnnn/nnnnn you get nnnnnn/1(nnnnnn).
                 * So first we scan for a open bracket '(' then if there is no '(' we then start the
                 * search immediately after the '/'.
                 */
                if( ( p = strstr( result, "(" ) ) )
                {
                    c->HPA_reported_real = str_ascii_number_to_ll( p + 1 );
                }
                else
                {
                    if( ( p = strstr( result, "/" ) ) )
                    {
                        c->HPA_reported_real = str_ascii_number_to_ll( p + 1 );
                    }
                }
                nwipe_log( NWIPE_LOG_INFO,
                           "HPA values %lli / %lli on %s",
                           c->HPA_reported_set,
                           c->HPA_reported_real,
                           c->device_name );
            }
            else
            {
                c->HPA_pre_erase_status = HPA_UNKNOWN;
                nwipe_log( NWIPE_LOG_WARNING,
                           "[UNKNOWN] We can't find the HPA line, has hdparm ouput changed? %s",
                           c->device_name );
            }

            /* close */
            r = pclose( fp );
            if( r > 0 )
            {
                exit_status = WEXITSTATUS( r );
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_WARNING,
                               "hpa_dco_status(): hdparm failed, \"%s\" exit status = %u",
                               hdparm_cmd_get_hpa,
                               exit_status );
                }

                if( exit_status == 127 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Installing hdparm is mandatory !" );
                    set_return_value = 2;
                    if( nwipe_options.nousb )
                    {
                        return set_return_value;
                    }
                }
            }
        }
    }

    /* Initialise the results buffer again, so we don't
     * some how inadvertently process a past result */
    memset( result, 0, sizeof( result ) );

    /* -----------------------------------------------
     * Run the dco identify command and determine the
     * real max sectors, store it in the drive context
     * for comparison against the hpa reported drive
     * size values.
     */

    dco_line_found = 0;

    if( hdparm_cmd_get_dco[0] != 0 )
    {

        fp = popen( hdparm_cmd_get_dco, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING, "hpa_dco_status: Failed to create stream to %s", hdparm_cmd_get_dco );

            set_return_value = 1;
        }

        if( fp != NULL )
        {
            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                /* Change the output of hdparm to lower case and search using lower case strings, to try
                 * to avoid minor changes in case in hdparm's output from breaking HPA/DCO detection */
                strlower( result );

                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_DEBUG, "%s \n%s", hdparm_cmd_get_dco, result );
                }

                if( strstr( result, "real max sectors" ) != 0 )
                {
                    /* extract the real max sectors, convert to binary and store in drive context */
                    dco_line_found = 1;
                    break;
                }
            }
            /* DCO line found, now process it */
            if( dco_line_found == 1 )
            {
                c->DCO_reported_real_max_sectors = str_ascii_number_to_ll( result );
                nwipe_log( NWIPE_LOG_INFO,
                           "DCO Real max sectors reported as %lli on %s",
                           c->DCO_reported_real_max_sectors,
                           c->device_name );
            }
            else
            {
                c->DCO_reported_real_max_sectors = 0;
                nwipe_log( NWIPE_LOG_INFO, "DCO Real max sectors not found" );
            }

            /* close */
            r = pclose( fp );
            if( r > 0 )
            {
                exit_status = WEXITSTATUS( r );
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_WARNING,
                               "hpa_dco_status(): hdparm failed, \"%s\" exit status = %u",
                               hdparm_cmd_get_dco,
                               exit_status );
                }

                if( exit_status == 127 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Installing hdparm is mandatory !" );
                    set_return_value = 2;
                    if( nwipe_options.nousb )
                    {
                        return set_return_value;
                    }
                }
            }
        }
    }

    /* Compare the results of hdparm -N (HPA set / HPA real)
     * and hdparm --dco-identidy (real max sectors). All three
     * values may be different or perhaps 'HPA set' and 'HPA real' are
     * different and 'HPA real' matches 'real max sectors'.
     *
     * A perfect HPA disabled result would be where all three
     * values are the same. It can then be considered that the
     * HPA is disabled.
     *
     * If 'HPA set' and 'HPA real' are different then it
     * can be considered that HPA is enabled)
     */

    /* Determine, based on the values of 'HPA set', 'HPA real,
     * and 'real max sectors' whether we set the HPA
     * flag as HPA_DISABLED, HPA_ENABLED or HPA_UNKNOWN.
     * The HPA flag will be displayed in the GUI and on
     * the certificate and is used to determine whether
     * to reset the HPA.
     */
    /* If all three values match then there is no hidden disc area. HPA is disabled. */
    if( ( c->HPA_reported_set == c->HPA_reported_real ) && c->DCO_reported_real_max_sectors == c->HPA_reported_set )
    {
        c->HPA_pre_erase_status = HPA_DISABLED;
    }
    else
    {
        /* If HPA set and DCO max sectors are equal it can also be considered that HPA is disabled */
        if( c->HPA_reported_set == c->DCO_reported_real_max_sectors )
        {
            c->HPA_pre_erase_status = HPA_DISABLED;
        }
        else
        {
            if( c->HPA_reported_set != c->DCO_reported_real_max_sectors )
            {
                c->HPA_pre_erase_status = HPA_ENABLED;
            }
        }
    }

    if( c->HPA_pre_erase_status == HPA_DISABLED )
    {
        nwipe_log( NWIPE_LOG_INFO, "[GOOD] HPA is disabled on %s", c->device_name );
    }
    else
    {
        if( c->HPA_pre_erase_status == HPA_ENABLED )
        {
            nwipe_log( NWIPE_LOG_WARNING, "[BAD] HPA is enabled on %s", c->device_name );
        }
        else
        {
            if( c->HPA_pre_erase_status == HPA_UNKNOWN )
            {
                nwipe_log(
                    NWIPE_LOG_WARNING, "[UNKNOWN] We can't seem to determine the HPA status on %s", c->device_name );
            }
        }
    }

    /* Determine the size of the HPA and store the results in the
     * context.
     */

    // WARNING Add code here

    return set_return_value;
}
