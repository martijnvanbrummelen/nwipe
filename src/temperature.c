/*
 *  temperature.c: functions that populate the drive temperature variables
 *  in each drives context structure.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

//#define _LARGEFILE64_SOURCE
//#define _FILE_OFFSET_BITS 64
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "device.h"
#include "prng.h"
#include "options.h"
#include "device.h"
#include "logging.h"
#include "temperature.h"
#include "miscellaneous.h"

int nwipe_init_temperature( nwipe_context_t* c )
{
    /* See header definition for description of function
     */
    DIR* dir;
    DIR* dir2;
    const char dirpath[] = "/sys/class/hwmon";
    char dirpath_tmp[256];
    char dirpath_tmp2[256];
    char dirpath_hwmonX[256];
    char device[256];
    char device_context_name[256];
    // const char dirpath[] = "/home/nick/mouse/hwmon1";
    struct dirent* dp;
    struct dirent* dp2;

    /* Why Initialise with 1000000? Because the GUI needs to know whether data
     * has been obtained so it can display appropriate information when a
     * device is unable to provide temperature data */

    c->temp1_crit = 1000000;
    c->temp1_highest = 1000000;
    c->temp1_input = 1000000;
    c->temp1_lcrit = 1000000;
    c->temp1_lowest = 1000000;
    c->temp1_max = 1000000;
    c->temp1_min = 1000000;
    c->temp1_monitored_wipe_max = 1000000;
    c->temp1_monitored_wipe_min = 1000000;
    c->temp1_monitored_wipe_avg = 1000000;
    c->temp1_flash_rate = 2;
    c->temp1_flash_rate_counter = 0;
    c->temp1_path[0] = 0;
    c->temp1_time = 0;

    /* Each hwmonX directory is processed in turn and once a hwmonX directory has been
     * found that is a block device and the block device name matches the drive
     * name in the current context then the path to ../hwmonX is constructed and written
     * to the drive context structure '* c'. This path is used in the nwipe_update_temperature
     * function to retrieve temperature data and store it in the device context
     */

    if( ( dir = opendir( dirpath ) ) != NULL )
    {
        /* Process each hwmonX sub directory  in turn */
        while( ( dp = readdir( dir ) ) != NULL )
        {
            /* Does the directory start with 'hwmon' */
            if( strstr( dp->d_name, "hwmon" ) != NULL )
            {
                if( nwipe_options.verbose )
                {
                    /* print a empty line to separate the different hwmon sensors */
                    nwipe_log( NWIPE_LOG_DEBUG, "hwmon:" );
                }
                strcpy( dirpath_tmp, dirpath );
                strcat( dirpath_tmp, "/" );
                strcat( dirpath_tmp, dp->d_name );
                strcpy( dirpath_hwmonX, dirpath_tmp );
                strcat( dirpath_tmp, "/device/block" );

                /* Depending on the class of block device, the device name may
                 * appear in different sub-directories. So we try to open each
                 * directory that are known to contain block devices. These are
                 * /sys/class/hwmon/hwmonX/device/block
                 * /sys/class/hwmon/hwmonX/device/nvme/nvme0
                 * /sys/class/hwmon/hwmonX/device/
                 */

                if( ( dir2 = opendir( dirpath_tmp ) ) == NULL )
                {
                    if( nwipe_options.verbose )
                    {
                        nwipe_log( NWIPE_LOG_DEBUG, "hwmon: %s doesn't exist, trying next path", dirpath_tmp );
                    }
                    strcpy( dirpath_tmp2, dirpath_hwmonX );
                    strcat( dirpath_tmp2, "/device/nvme/nvme0" );
                    strcpy( dirpath_tmp, dirpath_tmp2 );

                    if( ( dir2 = opendir( dirpath_tmp ) ) == NULL )
                    {
                        if( nwipe_options.verbose )
                        {
                            nwipe_log( NWIPE_LOG_DEBUG, "hwmon: %s doesn't exist, trying next path", dirpath_tmp );
                        }

                        strcpy( dirpath_tmp2, dirpath_hwmonX );
                        strcat( dirpath_tmp2, "/device" );
                        strcpy( dirpath_tmp, dirpath_tmp2 );

                        if( ( dir2 = opendir( dirpath_tmp ) ) == NULL )
                        {
                            if( nwipe_options.verbose )
                            {
                                nwipe_log(
                                    NWIPE_LOG_DEBUG, "hwmon: %s doesn't exist, no more paths to try", dirpath_tmp );
                            }
                            continue;
                        }
                    }
                }

                if( dir2 != NULL )
                {
                    if( nwipe_options.verbose )
                    {
                        nwipe_log( NWIPE_LOG_DEBUG, "hwmon: Found %s", dirpath_tmp );
                    }

                    /* Read the device name */
                    while( ( dp2 = readdir( dir2 ) ) != NULL )
                    {
                        if( nwipe_options.verbose )
                        {
                            nwipe_log( NWIPE_LOG_DEBUG, "hwmon: dirpath_tmp=%s/%s", dirpath_tmp, &dp2->d_name[0] );
                        }

                        /* Skip the '.' and '..' directories */
                        if( dp2->d_name[0] == '.' )
                        {
                            continue;
                        }
                        strcpy( device, dp2->d_name );

                        /* Create a copy of the device name from the context but strip the path from it, right justify
                         * device name, prefix with spaces so length is 8. */
                        nwipe_strip_path( device_context_name, c->device_name );

                        /* Remove leading/training whitespace from a string and left justify result */
                        trim( device_context_name );

                        /* Does the hwmon device match the device for this drive context */
                        if( strcmp( device, device_context_name ) != 0 )
                        {
                            /* No, so try next hwmon device */
                            continue;
                        }
                        else
                        {
                            /* Match ! This hwmon device matches this context, so write the hwmonX path to the context
                             */
                            nwipe_log( NWIPE_LOG_NOTICE, "hwmon: %s has temperature monitoring", device, dirpath_tmp );
                            if( nwipe_options.verbose )
                            {
                                nwipe_log( NWIPE_LOG_DEBUG, "hwmon: %s found in %s", device, dirpath_tmp );
                            }
                            /* Copy the hwmon path to the drive context structure */
                            strcpy( c->temp1_path, dirpath_hwmonX );
                        }
                    }
                    closedir( dir2 );
                }
            }
        }
        closedir( dir );
    }

    return 0;
}

void nwipe_update_temperature( nwipe_context_t* c )
{
    /* For the given drive context obtain the path to it's hwmon temperature settings
     * and read then write the temperature values back to the context. A numeric ascii to integer conversion is
     * performed. The temperaures should be updated no more frequently than every 60 seconds
     */

    char temperature_label[NUMBER_OF_FILES][20] = {
        "temp1_crit", "temp1_highest", "temp1_input", "temp1_lcrit", "temp1_lowest", "temp1_max", "temp1_min" };
    int* temperature_pcontext[NUMBER_OF_FILES] = {

        &( c->temp1_crit ),
        &( c->temp1_highest ),
        &( c->temp1_input ),
        &( c->temp1_lcrit ),
        &( c->temp1_lowest ),
        &( c->temp1_max ),
        &( c->temp1_min ) };

    char path[256];
    char temperature[256];
    FILE* fptr;
    int idx;
    int result;

    for( idx = 0; idx < NUMBER_OF_FILES; idx++ )
    {
        /* Construct the full path including filename */
        strcpy( path, c->temp1_path );
        strcat( path, "/" );
        strcat( path, &( temperature_label[idx][0] ) );

        /* Open the file */
        if( ( fptr = fopen( path, "r" ) ) != NULL )
        {
            /* Acquire data until we reach a newline */
            result = fscanf( fptr, "%[^\n]", temperature );

            /* Convert numeric ascii to binary integer */
            *( temperature_pcontext[idx] ) = atoi( temperature );

            /* Divide by 1000 to get degrees celsius */
            *( temperature_pcontext[idx] ) = *( temperature_pcontext[idx] ) / 1000;

            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_NOTICE, "hwmon: %s %dC", path, *( temperature_pcontext[idx] ) );
            }

            fclose( fptr );
        }
        else
        {
            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_NOTICE, "hwmon: Unable to  open %s", path );
            }
        }
    }

    /* Update the time stamp that records when we checked the temperature,
     * this is used by the GUI to check temperatures periodically, typically
     * every 60 seconds */
    c->temp1_time = time( NULL );

    return;
}

void nwipe_log_drives_temperature_limits( nwipe_context_t* c )
{
    /* See header for description of function
     */

    char temperature_limits_txt[500];

    int idx = 0;

    /*
     * Initialise the character string, as we are building it a few
     * characters at a time and it's important there it is populated
     * with all zeros as we are using strlen() as we build the line up.
     */
    memset( &temperature_limits_txt, 0, sizeof( temperature_limits_txt ) );

    if( c->temp1_crit != 1000000 )
    {
        snprintf( temperature_limits_txt,
                  sizeof( temperature_limits_txt ),
                  "Temperature limits for %s, critical=%ic, ",
                  c->device_name,
                  c->temp1_crit );
    }
    else
    {
        snprintf( temperature_limits_txt,
                  sizeof( temperature_limits_txt ),
                  "Temperature limits for %s, critical=N/A, ",
                  c->device_name );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_highest != 1000000 )
    {
        snprintf( &temperature_limits_txt[idx],
                  ( sizeof( temperature_limits_txt ) - idx ),
                  "highest=%ic, ",
                  c->temp1_highest );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "highest=N/A, " );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_lowest != 1000000 )
    {
        snprintf(
            &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "lowest=%ic, ", c->temp1_lowest );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "lowest=N/A, " );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_lcrit != 1000000 )
    {
        snprintf( &temperature_limits_txt[idx],
                  ( sizeof( temperature_limits_txt ) - idx ),
                  "low critical=%ic.",
                  c->temp1_lcrit );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "low critical=N/A. " );
    }

    nwipe_log( NWIPE_LOG_INFO, "%s", temperature_limits_txt );

    return;
}
