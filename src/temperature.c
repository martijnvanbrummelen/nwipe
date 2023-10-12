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
#include <sys/time.h>

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

    /* Why Initialise with 1000000 (defined as NO_TEMPERATURE_DATA)?
     * Because the GUI needs to know whether data has been obtained
     * so it can display appropriate information when a
     * device is unable to provide temperature data */

    c->templ_has_hwmon_data = 0;
    c->temp1_crit = NO_TEMPERATURE_DATA;
    c->temp1_highest = NO_TEMPERATURE_DATA;
    c->temp1_input = NO_TEMPERATURE_DATA;
    c->temp1_lcrit = NO_TEMPERATURE_DATA;
    c->temp1_lowest = NO_TEMPERATURE_DATA;
    c->temp1_max = NO_TEMPERATURE_DATA;
    c->temp1_min = NO_TEMPERATURE_DATA;
    c->temp1_monitored_wipe_max = NO_TEMPERATURE_DATA;
    c->temp1_monitored_wipe_min = NO_TEMPERATURE_DATA;
    c->temp1_monitored_wipe_avg = NO_TEMPERATURE_DATA;
    c->temp1_flash_rate = 0;
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
                            c->templ_has_hwmon_data = 1;
                        }
                    }
                    closedir( dir2 );
                }
            }
        }
        closedir( dir );
    }
    /* if no hwmon data available try scsi access (SAS Disks are known to be not working in hwmon */
    if( c->templ_has_hwmon_data == 0 && ( c->device_type == NWIPE_DEVICE_SAS || c->device_type == NWIPE_DEVICE_SCSI ) )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "no hwmon data for %s, try to get SCSI data", c->device_name );
        if( nwipe_init_scsi_temperature( c ) == 0 )
        {
            c->templ_has_scsitemp_data = 1;
            nwipe_log( NWIPE_LOG_INFO, "got SCSI temperature data for %s", c->device_name );
        }
        else
        {
            c->templ_has_scsitemp_data = 0;
            nwipe_log( NWIPE_LOG_INFO, "got no SCSI temperature data for %s", c->device_name );
        }
    }

    return 0;
}

float timedifference_msec( struct timeval tv_start, struct timeval tv_end )
{
    /* helper function for time measurement in msec */
    return ( tv_end.tv_sec - tv_start.tv_sec ) * 1000.0f + ( tv_end.tv_usec - tv_start.tv_usec ) / 1000.0f;
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
    struct timeval tv_start;
    struct timeval tv_end;
    float delta_t;

    /* avoid being called more often than 1x per 60 seconds */
    time_t nwipe_time_now = time( NULL );
    if( nwipe_time_now - c->temp1_time < 60 )
    {
        return;
    }

    /* measure time it takes to get the temperatures */
    gettimeofday( &tv_start, 0 );

    /* try to get temperatures from hwmon, standard */
    if( c->templ_has_hwmon_data == 1 )
    {
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
    }
    else
    {
        /* alternative method to get temperature from SCSI/SAS disks */
        if( c->device_type == NWIPE_DEVICE_SAS || c->device_type == NWIPE_DEVICE_SCSI )
        {
            if( c->templ_has_scsitemp_data == 1 )
            {
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_crit %dC", c->device_name, c->temp1_crit );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_highest %dC", c->device_name, c->temp1_highest );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_input %dC", c->device_name, c->temp1_input );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_lcrit %dC", c->device_name, c->temp1_lcrit );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_lowest %dC", c->device_name, c->temp1_lowest );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_max %dC", c->device_name, c->temp1_max );
                    nwipe_log( NWIPE_LOG_NOTICE, "hddtemp: %s temp1_min %dC", c->device_name, c->temp1_min );
                }
                if( nwipe_get_scsi_temperature( c ) != 0 )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "get_scsi_temperature error" );
                }
            }
        }
    }

    /* Update the time stamp that records when we checked the temperature,
     * this is used by the GUI to check temperatures periodically, typically
     * every 60 seconds */
    c->temp1_time = time( NULL );

    gettimeofday( &tv_end, 0 );
    delta_t = timedifference_msec( tv_start, tv_end );
    nwipe_log( NWIPE_LOG_NOTICE, "get temperature for %s took %f ms", c->device_name, delta_t );

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

    if( c->temp1_crit != NO_TEMPERATURE_DATA )
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

    if( c->temp1_max != NO_TEMPERATURE_DATA )
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "max=%ic, ", c->temp1_max );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "max=N/A, " );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_highest != NO_TEMPERATURE_DATA )
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

    if( c->temp1_lowest != NO_TEMPERATURE_DATA )
    {
        snprintf(
            &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "lowest=%ic, ", c->temp1_lowest );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "lowest=N/A, " );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_min != NO_TEMPERATURE_DATA )
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "min=%ic, ", c->temp1_min );
    }
    else
    {
        snprintf( &temperature_limits_txt[idx], ( sizeof( temperature_limits_txt ) - idx ), "min=N/A, " );
    }

    idx = strlen( temperature_limits_txt );

    if( c->temp1_lcrit != NO_TEMPERATURE_DATA )
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
