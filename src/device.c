/*
 *  device.c:  Device routines for nwipe.
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

#include <stdio.h>
#include <stdint.h>

#include "nwipe.h"
#include "context.h"
#include "device.h"
#include "method.h"
#include "options.h"
#include "logging.h"
#include <sys/ioctl.h>
#include <linux/hdreg.h>  // Drive specific defs
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <parted/parted.h>
#include <parted/debug.h>

int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount );
char* trim( char* str );

int nwipe_device_scan( nwipe_context_t*** c )
{
    /**
     * Scans the filesystem for storage device names.
     *
     * @parameter device_names  A reference to a null array pointer.
     * @modifies  device_names  Populates device_names with an array of nwipe_contect_t
     * @returns                 The number of strings in the device_names array.
     *
     */

    PedDevice* dev = NULL;
    ped_device_probe_all();

    int dcount = 0;

    while( ( dev = ped_device_get_next( dev ) ) )
    {
        if( check_device( c, dev, dcount ) )
            dcount++;
    }

    /* Return the number of devices that were found. */
    return dcount;

} /* nwipe_device_scan */

int nwipe_device_get( nwipe_context_t*** c, char** devnamelist, int ndevnames )
{
    /**
     * Gets information about devices
     *
     * @parameter device_names  A reference to a null array pointer.
     * @parameter devnamelist   An array of string pointers to the device names
     * @parameter ndevnames     Number of elements in devnamelist
     * @modifies  device_names  Populates device_names with an array of nwipe_contect_t
     * @returns                 The number of strings in the device_names array.
     *
     */

    PedDevice* dev = NULL;

    int i;
    int dcount = 0;

    for( i = 0; i < ndevnames; i++ )
    {

        dev = ped_device_get( devnamelist[i] );
        if( !dev )
        {
            nwipe_log( NWIPE_LOG_WARNING, "Device %s not found", devnamelist[i] );
            continue;
        }

        if( check_device( c, dev, dcount ) )
            dcount++;
    }

    /* Return the number of devices that were found. */
    return dcount;

} /* nwipe_device_get */

int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount )
{
    /* Populate this struct, then assign it to overall array of structs. */
    nwipe_context_t* next_device;
    int fd;
    int idx;
    int r;
    char tmp_serial[21];

    /* Check whether this drive is on the excluded drive list ? */
    idx = 0;
    while( idx < 10 )
    {
        if( !strcmp( dev->path, nwipe_options.exclude[idx++] ) )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "Device %s excluded as per command line option -e", dev->path );
            return 0;
        }
    }

    /* Try opening the device to see if it's valid. Close on completion. */
    if( !ped_device_open( dev ) )
    {
        nwipe_log( NWIPE_LOG_FATAL, "Unable to open device" );
        return 0;
    }
    ped_device_close( dev );

    /* New device, reallocate memory for additional struct pointer */
    *c = realloc( *c, ( dcount + 1 ) * sizeof( nwipe_context_t* ) );

    next_device = malloc( sizeof( nwipe_context_t ) );

    /* Check the allocation. */
    if( !next_device )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to create the array of enumeration contexts." );
        return 0;
    }

    /* Zero the allocation. */
    memset( next_device, 0, sizeof( nwipe_context_t ) );

    /* Get device information */
    next_device->device_model = dev->model;
    next_device->device_name = dev->path;
    next_device->device_size = dev->length * dev->sector_size;
    next_device->device_size_text = ped_unit_format_byte( dev, dev->length * dev->sector_size );
    next_device->result = -2;

    /* Attempt to get serial number of device. */
    if( ( fd = open( next_device->device_name = dev->path, O_RDONLY ) ) == ERR )
    {
        nwipe_log( NWIPE_LOG_WARNING, "Unable to open device %s to obtain serial number", next_device->device_name );
    }

    /*
     * We don't check the ioctl return status because there are plenty of situations where a serial number may not be
     * returned by ioctl such as USB drives, logical volumes, encryted volumes, so the log file would have multiple
     * benign ioctl errors reported which isn't necessarily a problem.
     */
    ioctl( fd, HDIO_GET_IDENTITY, &next_device->identity );
    close( fd );

    for( idx = 0; idx < 20; idx++ )
    {
        next_device->device_serial_no[idx] = next_device->identity.serial_no[idx];
    }

    // Terminate the string.
    next_device->device_serial_no[20] = 0;

    // Remove leading/trailing whitespace from serial number and left justify.
    trim( (char*) next_device->device_serial_no );

    /* if we couldn't obtain serial number by using the above method .. this this */
    r = nwipe_get_device_bus_type_and_serialno( next_device->device_name, &next_device->device_type, tmp_serial );

    /* If serial number & bus retrieved (0) OR unsupported USB bus identified (5) */
    if( r == 0 || r == 5 )
    {
        /* If the serial number hasn't already been populated */
        if( next_device->device_serial_no[0] == 0 )
        {
            strcpy( next_device->device_serial_no, tmp_serial );
        }
    }

    switch( next_device->device_type )
    {
        case NWIPE_DEVICE_UNKNOWN:
            strcpy( next_device->device_type_str, "UNK" );
            break;

        case NWIPE_DEVICE_IDE:
            strcpy( next_device->device_type_str, "IDE" );
            break;

        case NWIPE_DEVICE_SCSI:
            strcpy( next_device->device_type_str, "SCSI" );
            break;

        case NWIPE_DEVICE_COMPAQ:
            strcpy( next_device->device_type_str, "CPQ" );
            break;

        case NWIPE_DEVICE_USB:
            strcpy( next_device->device_type_str, "USB" );
            break;

        case NWIPE_DEVICE_IEEE1394:
            strcpy( next_device->device_type_str, "IEEE1394" );
            break;

        case NWIPE_DEVICE_ATA:
            strcpy( next_device->device_type_str, "ATA" );
            break;
    }

    if( strlen( (const char*) next_device->device_serial_no ) )
    {
        snprintf( next_device->device_label,
                  NWIPE_DEVICE_LABEL_LENGTH,
                  "%s %s (%s) %s/%s",
                  next_device->device_name,
                  next_device->device_type_str,
                  next_device->device_size_text,
                  next_device->device_model,
                  next_device->device_serial_no );
    }
    else
    {
        snprintf( next_device->device_label,
                  NWIPE_DEVICE_LABEL_LENGTH,
                  "%s %s (%s) %s",
                  next_device->device_name,
                  next_device->device_type_str,
                  next_device->device_size_text,
                  next_device->device_model );
    }

    nwipe_log( NWIPE_LOG_NOTICE,
               "Found %s, %s, %s, %s, S/N=%s",
               next_device->device_name,
               next_device->device_type_str,
               next_device->device_model,
               next_device->device_size_text,
               next_device->device_serial_no );

    ( *c )[dcount] = next_device;
    return 1;
}

/* Remove leading/training whitespace from a string and left justify result */
char* trim( char* str )
{
    size_t len = 0;
    char* frontp = str;
    char* endp = NULL;

    if( str == NULL )
    {
        return NULL;
    }
    if( str[0] == '\0' )
    {
        return str;
    }
    len = strlen( str );
    endp = str + len;

    /*
     * Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while( isspace( (unsigned char) *frontp ) )
    {
        ++frontp;
    }
    if( endp != frontp )
    {
        while( isspace( (unsigned char) *( --endp ) ) && endp != frontp )
        {
        }
    }
    if( str + len - 1 != endp )
        *( endp + 1 ) = '\0';
    else if( frontp != str && endp == frontp )
        *str = '\0';
    /*
     * Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
        while( *frontp )
        {
            *endp++ = *frontp++;
        }
        *endp = '\0';
    }
    return str;
}

int nwipe_get_device_bus_type_and_serialno( char* device, nwipe_device_t* bus, char* serialnumber )
{
    /* The caller provides a string that contains the device, i.e. /dev/sdc, also a pointer
     * to a integer (bus type) and thirdly a 21 byte
     * character string which this function populates with the serial number (20 characters + null terminator).
     *
     * The function populates the bus integer and serial number strings for the given device.
     * Results for bus would typically be ATA or USB see nwipe_device_t in context.h
     *
     * Return Values:
     * 0 = Success
     * 1 = popen failed to create stream for readlink
     * 2 = readlink exit code not 0, see nwipe logs
     * 3 = popen failed to create stream for smartctl
     * 4 = smartctl command not found, install smartmontools
     * 5 = smartctl detected un supported USB to IDE/SATA adapter
     * 6 = All other errors !
     *
     */

    FILE* fp;

    int r;  // A result buffer.
    int idx_src;
    int idx_dest;
    int device_len;
    int set_return_value;
    int exit_status;

    char readlink_command[] = "readlink /sys/block/%s";
    char readlink_command2[] = "/usr/bin/readlink /sys/block/%s";
    char readlink_command3[] = "/sbin/readlink /sys/block/%s";
    char smartctl_command[] = "smartctl -i %s";
    char smartctl_command2[] = "/sbin/smartctl -i %s";
    char smartctl_command3[] = "/usr/bin/smartctl -i %s";
    char device_shortform[50];
    char result[512];
    char final_cmd_readlink[sizeof( readlink_command ) + sizeof( device_shortform )];
    char final_cmd_smartctl[sizeof( smartctl_command ) + 256];

    /* Initialise return value */
    set_return_value = 0;

    *bus = 0;

    /* Scan device name and if device is for instance /dev/sdx then convert to sdx
     * If already sdx then just copy. */

    idx_dest = 0;
    device_shortform[idx_dest] = 0;
    device_len = strlen( device );
    idx_src = device_len;

    while( idx_src >= 0 )
    {
        if( device[idx_src] == '/' || idx_src == 0 )
        {
            idx_src++;

            /* Now scan forwards copying the short form device i.e sdc */
            while( idx_src < device_len )
            {
                device_shortform[idx_dest++] = device[idx_src++];
            }
            break;
        }
        else
        {
            idx_src--;
        }
    }
    device_shortform[idx_dest] = 0;

    final_cmd_readlink[0] = 0;

    /* Determine whether we can access readlink, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */
    if( system( "which readlink > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/readlink > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/readlink > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install readlink !" );
            }
            else
            {
                sprintf( final_cmd_readlink, readlink_command3, device_shortform );
            }
        }
        else
        {
            sprintf( final_cmd_readlink, readlink_command2, device_shortform );
        }
    }
    else
    {
        sprintf( final_cmd_readlink, readlink_command, device_shortform );
    }

    if( final_cmd_readlink[0] != 0 )
    {

        fp = popen( final_cmd_readlink, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "nwipe_get_device_bus_type_and_serialno: Failed to create stream to %s",
                       readlink_command );

            set_return_value = 1;
        }

        if( fp != NULL )
        {
            /* Read the output a line at a time - output it. */
            if( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                if( nwipe_options.verbose )
                {
                    strip_CR_LF( result );
                    nwipe_log( NWIPE_LOG_DEBUG, "Readlink: %s", result );
                }

                /* Scan the readlink results for bus types, i.e. USB or ATA
                 * Example: readlink
                 * /sys/block/sdd../devices/pci0000:00/0000:00:1d.0/usb2/2-1/2-1.3/2-1.3:1.0/host6/target6:0:0/6:0:0:0/block/sdd
                 */

                if( strstr( result, "/usb" ) != 0 )
                {
                    *bus = NWIPE_DEVICE_USB;
                }
                else
                {
                    if( strstr( result, "/ata" ) != 0 )
                    {
                        *bus = NWIPE_DEVICE_ATA;
                    }
                }
            }
            /* close */
            r = pclose( fp );

            if( r > 0 )
            {
                exit_status = WEXITSTATUS( r );
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_WARNING,
                               "nwipe_get_device_bus_type_and_serialno(): readlink failed, \"%s\" exit status = %u",
                               final_cmd_readlink,
                               exit_status );
                }

                if( exit_status == 127 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install Readlink recommended !" );
                }

                set_return_value = 2;
            }
        }
    }

    /*
     * Retrieve smartmontools drive information if USB bridge supports it, so we can retrieve the serial number of the
     * drive that's on the other side of the USB bridge.. */

    final_cmd_smartctl[0] = 0;

    /* Determine whether we can access smartctl, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */
    if( system( "which smartctl > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/smartctl > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/smartctl > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install smartmontools !" );
            }
            else
            {
                sprintf( final_cmd_smartctl, smartctl_command3, device );
            }
        }
        else
        {
            sprintf( final_cmd_smartctl, smartctl_command2, device );
        }
    }
    else
    {
        sprintf( final_cmd_smartctl, smartctl_command, device );
    }

    if( final_cmd_smartctl[0] != 0 )
    {
        fp = popen( final_cmd_smartctl, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "nwipe_get_device_bus_type_and_serialno(): Failed to create stream to %s",
                       smartctl_command );

            set_return_value = 3;
        }
        else
        {
            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                if( nwipe_options.verbose && result[0] != 0x0A )
                {
                    strip_CR_LF( result );
                    nwipe_log( NWIPE_LOG_DEBUG, "smartctl: %s", result );
                }

                if( strstr( result, "Serial Number:" ) != 0 )
                {
                    /* strip any leading or trailing spaces and left justify, +15 is the length of "Serial Number:" */
                    trim( &result[15] );

                    strncpy( serialnumber, &result[15], 20 );
                }
            }

            /* close */
            r = pclose( fp );

            if( r > 0 )
            {
                exit_status = WEXITSTATUS( r );
                if( nwipe_options.verbose && exit_status != 1 )
                {
                    nwipe_log( NWIPE_LOG_WARNING,
                               "nwipe_get_device_bus_type_and_serialno(): smartctl failed, \"%s\" exit status = %u",
                               final_cmd_smartctl,
                               exit_status );
                }
                set_return_value = 6;

                if( exit_status == 127 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install Smartctl recommended !" );

                    set_return_value = 4;
                }

                if( exit_status == 1 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "%s USB bridge, no passthru support", device );

                    if( *bus == NWIPE_DEVICE_USB )
                    {
                        strcpy( serialnumber, "(no ATA pass thru)" );
                        set_return_value = 5;
                    }
                }
            }
        }
    }

    return set_return_value;
}

void strip_CR_LF( char* str )
{
    /* In the specified string, replace any CR or LF with a space */
    int idx = 0;
    int len = strlen( str );
    while( idx < len )
    {
        if( str[idx] == 0x0A || str[idx] == 0x0D )
        {
            str[idx] = ' ';
        }
        idx++;
    }
}
