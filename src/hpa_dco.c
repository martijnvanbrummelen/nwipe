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
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include "nwipe.h"
#include "context.h"
#include "version.h"
#include "method.h"
#include "logging.h"
#include "options.h"
#include "hpa_dco.h"
#include "miscellaneous.h"

/* This function makes use of both the hdparm program to determine HPA/DCO status and we also access
 * the device configuration overlay identify data structure via the sg driver with ioctl calls.
 * I would prefer to write these functions without any reliance on hdparm however for the time being
 * we will utilize both methods. However, I don't like doing it like this as a change in formatted
 * output of hdparm could potentially break HPA/DCO detection requiring a fix. Time permitting I may
 * come back to this and fully implement it without any reliance on hdparm.
 */

int hpa_dco_status( nwipe_context_t* ptr )
{
    nwipe_context_t* c;
    c = ptr;

    int r;  // A result buffer.
    int set_return_value;
    int exit_status;
    int hpa_line_found;
    int dco_line_found;

    FILE* fp;
    char path_hdparm_cmd1_get_hpa[] = "hdparm --verbose -N";
    char path_hdparm_cmd2_get_hpa[] = "/sbin/hdparm --verbose -N";
    char path_hdparm_cmd3_get_hpa[] = "/usr/bin/hdparm --verbose -N";

    char path_hdparm_cmd4_get_dco[] = "hdparm --verbose --dco-identify";
    char path_hdparm_cmd5_get_dco[] = "/sbin/hdparm --verbose --dco-identify";
    char path_hdparm_cmd6_get_dco[] = "/usr/bin/hdparm --verbose --dco-identify";

    char pipe_std_err[] = "2>&1";

    char result[512];

    u64 nwipe_dco_real_max_sectors;

    char* p;

    /* Use the longest of the 'path_hdparm_cmd.....' strings above to
     *determine size in the strings below
     */
    char hdparm_cmd_get_hpa[sizeof( path_hdparm_cmd3_get_hpa ) + sizeof( c->device_name ) + sizeof( pipe_std_err )];
    char hdparm_cmd_get_dco[sizeof( path_hdparm_cmd6_get_dco ) + sizeof( c->device_name ) + sizeof( pipe_std_err )];

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
                          "%s %s %s\n",
                          path_hdparm_cmd3_get_hpa,
                          c->device_name,
                          pipe_std_err );
                snprintf( hdparm_cmd_get_dco,
                          sizeof( hdparm_cmd_get_dco ),
                          "%s %s %s\n",
                          path_hdparm_cmd6_get_dco,
                          c->device_name,
                          pipe_std_err );
            }
        }
        else
        {
            snprintf( hdparm_cmd_get_hpa,
                      sizeof( hdparm_cmd_get_hpa ),
                      "%s %s %s\n",
                      path_hdparm_cmd2_get_hpa,
                      c->device_name,
                      pipe_std_err );
            snprintf( hdparm_cmd_get_dco,
                      sizeof( hdparm_cmd_get_dco ),
                      "%s %s %s\n",
                      path_hdparm_cmd5_get_dco,
                      c->device_name,
                      pipe_std_err );
        }
    }
    else
    {
        snprintf( hdparm_cmd_get_hpa,
                  sizeof( hdparm_cmd_get_hpa ),
                  "%s %s %s\n",
                  path_hdparm_cmd1_get_hpa,
                  c->device_name,
                  pipe_std_err );
        snprintf( hdparm_cmd_get_dco,
                  sizeof( hdparm_cmd_get_dco ),
                  "%s %s %s\n",
                  path_hdparm_cmd4_get_dco,
                  c->device_name,
                  pipe_std_err );
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
                if( strstr( result, "sg_io: bad/missing sense data" ) != 0 )
                {
                    c->HPA_status = HPA_UNKNOWN;
                    nwipe_log( NWIPE_LOG_ERROR, "SG_IO bad/missing sense data %s", hdparm_cmd_get_hpa );
                    break;
                }
                else
                {
                    if( strstr( result, "hpa is disabled" ) != 0 )
                    {
                        c->HPA_status = HPA_DISABLED;

                        nwipe_log( NWIPE_LOG_DEBUG,
                                   "hdparm says the host protected area is disabled on %s but this information may or "
                                   "may not be correct, as occurs when you get a SG_IO error and 0/1 sectors and it "
                                   "says HPA is enabled. Further checks are conducted below..",
                                   c->device_name );
                        hpa_line_found = 1;
                        break;
                    }
                    else
                    {
                        if( strstr( result, "hpa is enabled" ) != 0 )
                        {
                            c->HPA_status = HPA_ENABLED;
                            nwipe_log( NWIPE_LOG_DEBUG,
                                       "hdparm says the host protected area is enabled on %s but this information may "
                                       "or may not be correct, as occurs when you get a SG_IO error and 0/1 sectors "
                                       "and it says HPA is enabled. Further checks are conducted below..",
                                       c->device_name );
                            hpa_line_found = 1;
                            break;
                        }
                        else
                        {
                            if( strstr( result, "accessible max address disabled" ) != 0 )
                            {
                                c->HPA_status = HPA_DISABLED;
                                nwipe_log( NWIPE_LOG_DEBUG,
                                           "hdparm says the accessible max address disabled on %s"
                                           "this means that there are no hidden sectors,  "
                                           "",
                                           c->device_name );
                                hpa_line_found = 1;
                                break;
                            }
                            else
                            {
                                if( strstr( result, "accessible max address enabled" ) != 0 )
                                {
                                    c->HPA_status = HPA_ENABLED;
                                    nwipe_log( NWIPE_LOG_DEBUG,
                                               "hdparm says the accessible max address enabled on %s"
                                               "this means that there are hidden sectors",
                                               c->device_name );
                                    hpa_line_found = 1;
                                    break;
                                }
                                else
                                {
                                    if( strstr( result, "invalid" ) != 0 )
                                    {
                                        c->HPA_status = HPA_ENABLED;
                                        nwipe_log(
                                            NWIPE_LOG_WARNING,
                                            "hdparm reports invalid output, sector information may be invalid, buggy "
                                            "drive firmware on %s?",
                                            c->device_name );
                                        // We'll assume the HPA values are in the string as we may be able to extract
                                        // something meaningful
                                        hpa_line_found = 1;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* if the HPA line was found then process the line,
             * extracting the 'hpa set' and 'hpa real' values.
             */
            if( hpa_line_found == 1 )
            {
                /* Extract the 'HPA set' value, the first value in the line and convert
                 * to binary and save in context */

                nwipe_log( NWIPE_LOG_INFO, "HPA: %s on %s", result, c->device_name );

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
                c->HPA_status = HPA_UNKNOWN;
                nwipe_log( NWIPE_LOG_WARNING,
                           "[UNKNOWN] We can't find the HPA line, has hdparm ouput unknown/changed? %s",
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
                           "hdparm:DCO Real max sectors reported as %lli on %s",
                           c->DCO_reported_real_max_sectors,
                           c->device_name );

                /* Validate the real max sectors to detect extreme or impossible
                 * values, so the size must be greater than zero but less than
                 * 200TB (429496729600 sectors). As its 2023 and the largest drive
                 * available is 20TB I wonder if somebody in the future will be looking
                 * at this and thinking, yep we need to increase that value... and I'm
                 * wondering what year that will be. This validation is necessary all
                 * because of a bug in hdparm v9.60 (and maybe other versions) which
                 * produced wildly inaccurate values, often negative.
                 */
                if( c->DCO_reported_real_max_sectors > 0 && c->DCO_reported_real_max_sectors < 429496729600 )
                {
                    nwipe_log( NWIPE_LOG_INFO,
                               "NWipe: DCO Real max sectors reported as %lli on %s",
                               c->DCO_reported_real_max_sectors,
                               c->device_name );
                }
                else
                {
                    /* Call nwipe's own low level function to retrieve the drive configuration
                     * overlay and retrieve the real max sectors. We may remove reliance on hdparm
                     * if nwipes own low level drive access code works well.
                     */
                    c->DCO_reported_real_max_sectors = nwipe_read_dco_real_max_sectors( c->device_name );

                    /* Check our real max sectors function is returning sensible data too */
                    if( c->DCO_reported_real_max_sectors > 0 && c->DCO_reported_real_max_sectors < 429496729600 )
                    {
                        nwipe_log( NWIPE_LOG_INFO,
                                   "NWipe: DCO Real max sectors reported as %lli on %s",
                                   c->DCO_reported_real_max_sectors,
                                   c->device_name );
                    }
                    else
                    {
                        c->DCO_reported_real_max_sectors = 0;
                        nwipe_log( NWIPE_LOG_INFO, "DCO Real max sectors not found" );
                    }
                }
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
     * can be considered that HPA is enabled, assuming 'HPA set'
     * and 'HPA real' are not 0/1 which occurs when a SG_IO error
     * occurs. That also is checked for as it often indicates a
     * poor USB device that does not have ATA pass through support.
     *
     * However we also need to consider that more recent drives
     * no longer support HPA/DCO such as the Seagate ST10000NM0016,
     * ST4000NM0033 and ST1000DM003. If you try to issue those drives
     * with the ATA command code 0xB1 (device configuration overlay)
     * you will get a generic illegal request in the returned sense data.
     *
     * One other thing to note, we use HPA enabled/disabled to mean
     * hidden area detected or not detected, this could be caused by
     * either the dco-setmax being issued or Np, either way an area
     * of the disc can be hidden. From the user interface we just call
     * it a HPA/DCO hidden area detected (or not) which is more
     * meaningful than just saying HDA enabled or disabled and a user
     * not familiar with the term HPA or DCO not understanding why a
     * HDA being detected could be significant.
     */

    /* Determine, based on the values of 'HPA set', 'HPA real and
     * 'real max sectors' whether we set the HPA flag as HPA_DISABLED,
     * HPA_ENABLED, HPA_UNKNOWN or HPA_NOT_APPLICABLE. The HPA flag
     * will be displayed in the GUI and on the certificate and is
     * used to determine whether to reset the HPA.
     *
     */

    /* WARNING temp assignments WARNING
     * s=28,r=28,rm=0
     *
     */
#if 0
    c->HPA_reported_set = 10;
    c->HPA_reported_real = 28;
    c->DCO_reported_real_max_sectors = 0;

    c->HPA_reported_set = 28;
    c->HPA_reported_real = 28;
    c->DCO_reported_real_max_sectors = 0;

    c->HPA_reported_set = 1000;
    c->HPA_reported_real = 2048;
    c->DCO_reported_real_max_sectors = 2048;
#endif

    /* If all three values match and none are zero then there is NO hidden disc area. HPA is disabled. */
    if( c->HPA_reported_set == c->HPA_reported_real && c->DCO_reported_real_max_sectors == c->HPA_reported_set
        && c->HPA_reported_set != 0 && c->HPA_reported_real != 0 && c->DCO_reported_real_max_sectors != 0 )
    {
        c->HPA_status = HPA_DISABLED;
    }
    else
    {
        /* If HPA set and DCO max sectors are equal it can also be considered that HPA is disabled */
        if( ( c->HPA_reported_set == c->DCO_reported_real_max_sectors ) && c->HPA_reported_set != 0
            && c->DCO_reported_real_max_sectors != 0 )
        {
            c->HPA_status = HPA_DISABLED;
        }
        else
        {
            if( c->DCO_reported_real_max_sectors > 0 && c->DCO_reported_real_max_sectors == ( c->device_size / 512 ) )
            {
                c->HPA_status = HPA_DISABLED;
            }
            else
            {
                if( c->DCO_reported_real_max_sectors > 0
                    && c->DCO_reported_real_max_sectors != ( c->device_size / 512 ) )
                {
                    c->HPA_status = HPA_ENABLED;
                }
                else
                {
                    if( c->HPA_reported_set == c->HPA_reported_real && c->DCO_reported_real_max_sectors == 0 )
                    {
                        c->HPA_status = HPA_NOT_APPLICABLE;
                    }
                    else
                    {
                        if( c->HPA_reported_set != c->DCO_reported_real_max_sectors && c->HPA_reported_set != 0 )
                        {
                            c->HPA_status = HPA_ENABLED;
                        }
                        else
                        {
                            /* This occurs when a SG_IO error occurs with USB devices that don't support ATA pass
                             * through */
                            if( c->HPA_reported_set == 0 && c->HPA_reported_real == 1 )
                            {
                                c->HPA_status = HPA_UNKNOWN;
                            }
                            else
                            {
                                /* NVMe drives don't support HPA/DCO */
                                if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
                                    || ( c->HPA_reported_set > 1 && c->DCO_reported_real_max_sectors < 2 ) )
                                {
                                    c->HPA_status = HPA_NOT_APPLICABLE;
                                }
                                else
                                {
                                    /* For recent enterprise and new drives that don't provide HPA/DCO anymore */
                                    if( c->HPA_reported_set > 0 && c->HPA_reported_real == 1
                                        && c->DCO_reported_real_max_sectors < 2 )
                                    {
                                        c->HPA_status = HPA_NOT_APPLICABLE;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if( c->HPA_status == HPA_DISABLED )
    {
        nwipe_log( NWIPE_LOG_INFO, "No hidden sectors on %s", c->device_name );
    }
    else
    {
        if( c->HPA_status == HPA_ENABLED )
        {
            nwipe_log( NWIPE_LOG_WARNING, " *********************************" );
            nwipe_log( NWIPE_LOG_WARNING, " *** HIDDEN SECTORS DETECTED ! *** on %s", c->device_name );
            nwipe_log( NWIPE_LOG_WARNING, " *********************************" );
        }
        else
        {
            if( c->HPA_status == HPA_UNKNOWN )
            {
                if( c->device_bus == NWIPE_DEVICE_USB )
                    nwipe_log( NWIPE_LOG_WARNING,
                               "HIDDEN SECTORS INDETERMINATE! on %s, Some USB adapters & memory sticks don't support "
                               "ATA pass through",
                               c->device_name );
                else
                {
                    if( c->HPA_status == HPA_NOT_APPLICABLE )
                    {
                        nwipe_log( NWIPE_LOG_WARNING, "%s may not support HPA/DCO", c->device_name );
                    }
                    else
                    {
                        nwipe_log(
                            NWIPE_LOG_SANITY, "Unrecognised HPA_status, should not be possible %s", c->device_name );
                    }
                }
            }
        }
    }

    /* -------------------------------------------------------------------
     * create two variables for later use  based on real max sectors
     * DCO_reported_real_max_size = real max sectors * sector size = bytes
     * DCO_reported_real_max_size_text = human readable string, i.e 1TB etc.
     */
    c->DCO_reported_real_max_size = c->DCO_reported_real_max_sectors * c->device_sector_size;
    Determine_C_B_nomenclature(
        c->DCO_reported_real_max_size, c->DCO_reported_real_max_size_text, NWIPE_DEVICE_SIZE_TXT_LENGTH );

    nwipe_dco_real_max_sectors = nwipe_read_dco_real_max_sectors( c->device_name );

    /* Analyse all the variations to produce the final real max bytes which takes into
     * account drives that don't support DCO or HPA. This result is used in the PDF
     * creation functions.
     */

    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        c->Calculated_real_max_size_in_bytes = c->device_size;
    }
    else
    {
        /* If the DCO is reporting a real max sectors > 1 then that is what we will use as the real disc size
         */
        if( c->DCO_reported_real_max_size > 1 )
        {
            c->Calculated_real_max_size_in_bytes = c->DCO_reported_real_max_sectors * c->device_sector_size;
        }
        else
        {
            /* If HPA is enabled and DCO real max sectors did not exist, then we have to assume - c->HPA_reported_real
             * is the value we need, however if that is zero, then c->HPA_reported_set and if that is zero then
             * c->device_size as reported by libata
             */
            if( c->HPA_reported_real > 0 )
            {
                c->Calculated_real_max_size_in_bytes = c->HPA_reported_real * c->device_sector_size;
            }
            else
            {
                if( c->HPA_reported_set > 0 )
                {
                    c->Calculated_real_max_size_in_bytes = c->HPA_reported_set * c->device_sector_size;
                }
                else
                {
                    c->Calculated_real_max_size_in_bytes = c->device_size;
                }
            }
        }
    }

    /* ----------------------------------------------------------------------------------
     * Determine the size of the HPA if it's enabled and store the results in the context
     */

    if( c->HPA_status == HPA_ENABLED )
    {
        if( c->Calculated_real_max_size_in_bytes != c->device_size )
        {
            c->HPA_sectors =
                ( (u64) ( c->Calculated_real_max_size_in_bytes - c->device_size ) / c->device_sector_size );
        }
        else
        {
            c->HPA_sectors = 0;
        }

        /* Convert the size to a human readable format and save in context */
        Determine_C_B_nomenclature( c->HPA_sectors, c->HPA_size_text, NWIPE_DEVICE_SIZE_TXT_LENGTH );
    }
    else
    {
        /* HPA not enabled so initialise these values */
        c->HPA_sectors = 0;
        c->HPA_size_text[0] = 0;
    }

    nwipe_log( NWIPE_LOG_DEBUG,
               "c->Calculated_real_max_size_in_bytes=%lli, c->device_size=%lli, c->device_sector_size=%lli, "
               "c->DCO_reported_real_max_size=%lli, c->HPA_sectors=%lli c->device_type=%i ",
               c->Calculated_real_max_size_in_bytes,
               c->device_size,
               c->device_sector_size,
               c->DCO_reported_real_max_size,
               c->HPA_sectors,
               c->device_type );

    return set_return_value;
}

u64 nwipe_read_dco_real_max_sectors( char* device )
{
    /* This function sends a device configuration overlay identify command 0xB1 (dco-identify)
     * to the drive and extracts the real max sectors. The value is incremented by 1 and
     * then returned. We rely upon this function to determine real max sectors as there
     * is a bug in hdparm 9.60, including possibly earlier or later versions but which is
     * fixed in 9.65, that returns a incorrect (negative) value
     * for some drives that are possibly over a certain size.
     */

    /* TODO Add checks in case of failure, especially with recent drives that may not
     * support drive configuration overlay commands.
     */

#define LBA_SIZE 512
#define CMD_LEN 16
#define BLOCK_MAX 65535
#define LBA_MAX ( 1 << 30 )
#define SENSE_BUFFER_SIZE 32

    u64 nwipe_real_max_sectors;

    /* This command issues command 0xb1 (dco-identify) 15th byte */
    unsigned char cmd_blk[CMD_LEN] = { 0x85, 0x08, 0x0e, 0x00, 0xc2, 0, 0x01, 0, 0, 0, 0, 0, 0, 0x40, 0xb1, 0 };

    sg_io_hdr_t io_hdr;
    unsigned char buffer[LBA_SIZE];  // Received data block
    unsigned char sense_buffer[SENSE_BUFFER_SIZE];  // Sense data

    /* three characters represent one byte of sense data, i.e
     * two characters and a space "01 AE 67"
     */
    char sense_buffer_hex[( SENSE_BUFFER_SIZE * 3 ) + 1];

    int i, i2;  // index
    int fd;  // file descripter

    if( ( fd = open( device, O_RDWR ) ) < 0 )
    {
        /* Unable to open device */
        return -1;
    }

    /******************************************
     * Initialise the sg header for reading the
     * device configuration overlay identify data
     */
    memset( &io_hdr, 0, sizeof( sg_io_hdr_t ) );
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof( cmd_blk );
    io_hdr.mx_sb_len = sizeof( sense_buffer );
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = LBA_SIZE;
    io_hdr.dxferp = buffer;
    io_hdr.cmdp = cmd_blk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 20000;

    if( ioctl( fd, SG_IO, &io_hdr ) < 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "IOCTL command failed retrieving DCO" );
        i2 = 0;
        for( i = 0, i2 = 0; i < SENSE_BUFFER_SIZE; i++, i2 += 3 )
        {
            /* IOCTL returned an error */
            snprintf( &sense_buffer_hex[i2], sizeof( sense_buffer_hex ), "%02x ", sense_buffer[i] );
        }
        sense_buffer_hex[i2] = 0;  // terminate string
        nwipe_log( NWIPE_LOG_DEBUG, "Sense buffer from failed DCO identify cmd:%s", sense_buffer_hex );
        return -2;
    }

    /* Close the device */
    close( fd );

    /***************************************************************
     * Extract the real max sectors from the returned 512 byte block.
     * Assuming the first word/byte is 0. We extract the bytes & switch
     * the endian. Words 3-6(bytes 6-13) contain the max sector address
     */
    nwipe_real_max_sectors = (u64) ( (u64) buffer[13] << 56 ) | ( (u64) buffer[12] << 48 ) | ( (u64) buffer[11] << 40 )
        | ( (u64) buffer[10] << 32 ) | ( (u64) buffer[9] << 24 ) | ( (u64) buffer[8] << 16 ) | ( (u64) buffer[7] << 8 )
        | buffer[6];

    /* Don't really understand this but hdparm adds 1 to
     * the real max sectors too, counting zero as sector?
     * but only increment if it's already greater than zero
     */
    if( nwipe_real_max_sectors > 0 )
    {
        nwipe_real_max_sectors++;
    }

    nwipe_log(
        NWIPE_LOG_INFO, "func:nwipe_read_dco_real_max_sectors(), DCO real max sectors = %lli", nwipe_real_max_sectors );

    return nwipe_real_max_sectors;
}

int ascii2binary_array( char* input, unsigned char* output_bin, int bin_size )
{
    /* Converts ascii sense data output by hdparm to binary.
     * Scans a character string that contains hexadecimal ascii data, ignores spaces
     * and extracts and converts the hexadecimal ascii data to binary and places in a array.
     * Typically for dco_identify sense data the bin size will be 512 bytes but for error
     * sense data this would be 32 bytes.
     */
    int idx_in;  // Index into ascii input string
    int idx_out;  // Index into the binary output array
    int byte_count;  // Counts which 4 bit value we are working on
    char upper4bits;
    char lower4bits;

    byte_count = 0;
    idx_in = 0;
    idx_out = 0;
    while( input[idx_in] != 0 )
    {
        if( input[idx_in] >= '0' && input[idx_in] <= '9' )
        {
            if( byte_count == 0 )
            {
                upper4bits = input[idx_in] - 0x30;
                byte_count++;
            }
            else
            {
                lower4bits = input[idx_in] - 0x30;
                output_bin[idx_out++] = ( upper4bits << 4 ) | ( lower4bits );
                byte_count = 0;

                if( idx_out >= bin_size )
                {
                    return 0;  // output array full.
                }
            }
        }
        else
        {
            if( input[idx_in] >= 'a' && input[idx_in] <= 'f' )
            {
                if( byte_count == 0 )
                {
                    upper4bits = input[idx_in] - 0x57;
                    byte_count++;
                }
                else
                {
                    lower4bits = input[idx_in] - 0x57;
                    output_bin[idx_out++] = ( upper4bits << 4 ) | ( lower4bits );
                    byte_count = 0;

                    if( idx_out >= bin_size )
                    {
                        return 0;  // output array full.
                    }
                }
            }
        }
        idx_in++;  // next byte in the input string
    }
    return 0;
}
