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
#include <ctype.h>
#include <limits.h>

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
#include "hpa_dco.h"
#include "miscellaneous.h"

#include <parted/parted.h>
#include <parted/debug.h>

/*
 * Tunable sizes for the wiping / verification I/O path.
 *
 * NWIPE_IO_BLOCKSIZE:
 *   - Target size of individual read()/write() operations.
 *   - Default is 4 MiB, so each syscall moves a lot of data instead of only
 *     4 KiB, drastically reducing syscall overhead.
 *
 * Notes:
 *   - We do NOT depend on O_DIRECT here; all code works fine with normal,
 *     buffered I/O.
 *   - But all I/O buffers are allocated aligned to the device block size so
 *     that the same code also works with O_DIRECT when the device is opened
 *     with it.
 */
#ifndef NWIPE_IO_BLOCKSIZE
#define NWIPE_IO_BLOCKSIZE ( 4 * 1024 * 1024UL ) /* 4 MiB I/O block */
#endif

#if NWIPE_IO_BLOCKSIZE > INT_MAX
#error "NWIPE_IO_BLOCKSIZE must fit in an int"
#endif

int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount );
char* trim( char* str );
static void nwipe_normalize_serial( char* serial );

/*
 * Resolve a device path (including /dev/disk/by-* symlinks) to its
 * underlying block device id (dev_t).
 *
 * Returns 0 on success and fills *out_rdev.
 * Returns -1 on error or if the path is not a block device.
 */
static int nwipe_path_to_rdev( const char* path, dev_t* out_rdev )
{
    struct stat st;

    if( path == NULL || out_rdev == NULL )
    {
        errno = EINVAL;
        return -1;
    }

    /*
     * stat() follows symlinks by default, which is what we want for
     * persistent names in /dev/disk/by-id, /dev/disk/by-path, etc.
     */
    if( stat( path, &st ) != 0 )
    {
        return -1;
    }

    if( !S_ISBLK( st.st_mode ) )
    {
        /* Not a block device node. */
        errno = ENOTBLK;
        return -1;
    }

    *out_rdev = st.st_rdev;
    return 0;
}

/*
 * Check whether a candidate device node should be excluded based on the
 * --exclude list. Matching is done primarily by device identity
 * (major/minor via st_rdev), so persistent names like /dev/disk/by-id/*
 * are safe. We keep legacy string-based matching as a fallback.
 *
 * Returns 1 if the candidate should be excluded, 0 otherwise.
 */
static int nwipe_is_excluded_device( const char* candidate_devnode )
{
    dev_t cand_rdev;
    int have_cand_rdev;
    int i;

    /* Try to resolve the candidate device to a dev_t. */
    have_cand_rdev = ( nwipe_path_to_rdev( candidate_devnode, &cand_rdev ) == 0 );

    for( i = 0; i < MAX_NUMBER_EXCLUDED_DRIVES; i++ )
    {
        const char* ex = nwipe_options.exclude[i];
        dev_t ex_rdev;
        int have_ex_rdev;
        const char* base;

        /* Empty slot in the exclude array. */
        if( ex == NULL || ex[0] == 0 )
        {
            continue;
        }

        /*
         * First try: both candidate and exclude entry resolve to block
         * devices; compare device ids (major/minor).
         */
        have_ex_rdev = ( nwipe_path_to_rdev( ex, &ex_rdev ) == 0 );
        if( have_cand_rdev && have_ex_rdev && ex_rdev == cand_rdev )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "Device %s excluded as per command line option -e", candidate_devnode );
            return 1;
        }

        /*
         * Fallback 1: exact string match. This keeps compatibility with
         * older usage like --exclude=/dev/sda or --exclude=/dev/mapper/cryptswap1.
         */
        if( strcmp( candidate_devnode, ex ) == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "Device %s excluded as per command line option -e", candidate_devnode );
            return 1;
        }

        /*
         * Fallback 2: match against the basename only, so that an
         * exclude entry like "sda" still works even if the full path is
         * /dev/sda.
         */
        base = strrchr( candidate_devnode, '/' );
        if( base != NULL )
        {
            base++;
        }
        else
        {
            base = candidate_devnode;
        }

        if( strcmp( base, ex ) == 0 )
        {
            nwipe_log( NWIPE_LOG_NOTICE, "Device %s excluded as per command line option -e", candidate_devnode );
            return 1;
        }
    }

    return 0;
}

extern int terminate_signal;

int nwipe_device_scan( nwipe_context_t*** c )
{
    /**
     * Scans the filesystem for storage device names.
     *
     * @parameter device_names  A reference to a null array pointer.
     * @modifies  device_names  Populates device_names with an array of nwipe_context_t
     * @returns                 The number of strings in the device_names array.
     *
     */

    PedDevice* dev = NULL;
    ped_device_probe_all();

    int dcount = 0;

    while( ( dev = ped_device_get_next( dev ) ) )
    {
        /* to have some progress indication. can help if there are many/slow disks */
        fprintf( stderr, "." );

        if( check_device( c, dev, dcount ) )
            dcount++;

        /* Don't bother scanning drives if the terminate signal is active ! as in the case of
         * the readlink program missing which is required if the --nousb option has been specified */
        if( terminate_signal == 1 )
        {
            break;
        }
    }

    /* Return the number of devices that were found. */
    return dcount;

} /* nwipe_device_scan */

int nwipe_device_get( nwipe_context_t*** c, char** devnamelist, int ndevnames )
{
    PedDevice* dev = NULL;

    int i;
    int dcount = 0;

    for( i = 0; i < ndevnames; i++ )
    {
        /* to have some progress indication. can help if there are many/slow disks */
        fprintf( stderr, "." );

        dev = ped_device_get( devnamelist[i] );
        if( !dev )
        {
            nwipe_log( NWIPE_LOG_WARNING, "Device %s not found", devnamelist[i] );
            continue;
        }

        if( check_device( c, dev, dcount ) )
            dcount++;

        /* Don't bother scanning drives if the terminate signal is active ! as in the case of
         * the readlink program missing which is required if the --nousb option has been specified */
        if( terminate_signal == 1 )
        {
            break;
        }
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
    char tmp_serial[NWIPE_SERIALNUMBER_LENGTH + 1];
    nwipe_device_t bus;
    int is_ssd;
    int check_HPA;  // a flag that indicates whether we check for a HPA on this device

    bus = 0;

    /* Check whether this drive is on the excluded drive list. */
    if( nwipe_is_excluded_device( dev->path ) )
    {
        /* Already logged inside nwipe_is_excluded_device(). */
        return 0;
    }

    /* Check whether the user has specified using the --nousb option
     * that all USB devices should not be displayed or wiped whether
     * in GUI, --nogui or --autonuke modes */

    if( nwipe_options.nousb )
    {
        /* retrieve bus and drive serial number, HOWEVER we are only interested in the bus at this time */
        r = nwipe_get_device_bus_type_and_serialno( dev->path, &bus, &is_ssd, tmp_serial, NULL, 0 );

        /* See nwipe_get_device_bus_type_and_serialno() function for meaning of these codes */
        if( r == 0 || ( r >= 3 && r <= 6 ) )
        {
            if( bus == NWIPE_DEVICE_USB )
            {
                nwipe_log( NWIPE_LOG_NOTICE, "Device %s ignored as per command line option --nousb", dev->path );
                return 0;
            }
        }
        else
        {
            if( r == 2 )
            {
                nwipe_log(
                    NWIPE_LOG_NOTICE, "--nousb requires the 'readlink' program, please install readlink", dev->path );
                terminate_signal = 1;
                return 0;
            }
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

    /*
     * Get device busy state (possibly mounted or otherwise in use)
     * If libparted says device is safe to partition, it's safe to wipe.
     * So for our disk wiping purposes it should be an equally good metric.
     */
    next_device->device_busy = ped_device_is_busy( dev );

    /* Get device information */
    next_device->device_model = dev->model;
    remove_ATA_prefix( next_device->device_model );

    /* Some USB adapters have drive model endian swapped, pattern match and fix */
    fix_endian_model_names( next_device->device_model );

    /* full device name, i.e. /dev/sda */
    next_device->device_name = dev->path;

    /* remove /dev/ from device, right justify and prefix name so string length is eight characters */
    nwipe_strip_path( next_device->device_name_without_path, next_device->device_name );

    const char* device_name_terse;
    device_name_terse = skip_whitespace( next_device->device_name_without_path );
    if( device_name_terse != NULL )
    {
        /* remove the leading whitespace and save result, we use the device without path and no leading or trailing
         * space in pdf file creation later */
        strcpy( next_device->device_name_terse, device_name_terse );
    }
    else
    {
        strcpy( next_device->device_name_terse, "_" );
    }

    /* To maintain column alignment in the gui we have to remove /dev/ from device names that
     * exceed eight characters including the /dev/ path.
     */
    if( strlen( next_device->device_name ) > MAX_LENGTH_OF_DEVICE_STRING )
    {
        strcpy( next_device->gui_device_name, next_device->device_name_without_path );
    }
    else
    {
        strcpy( next_device->gui_device_name, next_device->device_name );
    }

    next_device->device_size = dev->length * dev->sector_size;
    next_device->device_sector_size = dev->sector_size;  // logical sector size
    next_device->device_phys_sector_size = dev->phys_sector_size;  // physical sector size
    next_device->device_size_in_sectors = next_device->device_size / next_device->device_sector_size;
    next_device->device_size_in_512byte_sectors = next_device->device_size / 512;
    Determine_C_B_nomenclature( next_device->device_size, next_device->device_size_txt, NWIPE_DEVICE_SIZE_TXT_LENGTH );
    next_device->device_size_text = next_device->device_size_txt;
    next_device->result = -2;

    /* Attempt to get serial number of device. */
    next_device->device_serial_no[0] = '\0'; /* initialise the serial number */

    fd = open( next_device->device_name = dev->path, O_RDONLY );
    if( fd == ERR )
    {
        nwipe_log( NWIPE_LOG_WARNING, "Unable to open device %s to obtain serial number", next_device->device_name );
    }
    else
    {
        /*
         * We don't check the ioctl return status because there are plenty of
         * situations where a serial number may not be returned by ioctl such as
         * USB drives, logical volumes, encrypted volumes, so the log file
         * would have multiple benign ioctl errors reported which isn't
         * necessarily a problem.
         */
        ioctl( fd, HDIO_GET_IDENTITY, &next_device->identity );
        close( fd );
    }

    for( idx = 0; idx < NWIPE_SERIALNUMBER_LENGTH; idx++ )
    {
        if( isascii( next_device->identity.serial_no[idx] ) && !iscntrl( next_device->identity.serial_no[idx] ) )
        {
            next_device->device_serial_no[idx] = next_device->identity.serial_no[idx];
        }
        else
        {
            break;
        }
    }

    // Terminate the string.
    next_device->device_serial_no[idx] = 0;

    // Remove leading/trailing whitespace from serial number and left justify.
    trim( (char*) next_device->device_serial_no );

    /* if we couldn't obtain serial number by using the above method .. try this */
    r = nwipe_get_device_bus_type_and_serialno( next_device->device_name,
                                                &next_device->device_type,
                                                &next_device->device_is_ssd,
                                                tmp_serial,
                                                next_device->device_sysfs_path,
                                                sizeof( next_device->device_sysfs_path ) );

    /* If serial number & bus retrieved (0) OR unsupported USB bus identified (5) */
    if( r == 0 || r == 5 )
    {
        /* If the serial number hasn't already been populated */
        if( next_device->device_serial_no[0] == 0 )
        {
            strncpy( next_device->device_serial_no, tmp_serial, NWIPE_SERIALNUMBER_LENGTH );
        }
    }

    /* Does the user want to anonymize serial numbers ? */
    if( nwipe_options.quiet )
    {
        if( next_device->device_serial_no[0] == 0 )
        {
            strncpy( next_device->device_serial_no, "????????????????????", NWIPE_SERIALNUMBER_LENGTH + 1 );
        }
        else
        {
            strncpy( next_device->device_serial_no, "XXXXXXXXXXXXXXXXXXXX", NWIPE_SERIALNUMBER_LENGTH + 1 );
        }
    }
    /* strncpy would have copied the null terminator BUT just to be sure, just in case somebody changes the length
     * of those strings we should explicitly terminate the string */
    next_device->device_serial_no[NWIPE_SERIALNUMBER_LENGTH] = 0;

    /* Ensure the serial number cannot break the ncurses UI. */
    nwipe_normalize_serial( next_device->device_serial_no );

    /* Initialise the variables that toggle the [size][temp c] with [HPA status]
     * Not currently used, but may be used in the future or for other purposes
     */
    next_device->HPA_toggle_time = time( NULL );
    next_device->HPA_display_toggle_state = 0;

    /* Initialise the HPA variables for this device
     */
    next_device->HPA_reported_set = 0;
    next_device->HPA_reported_real = 0;
    next_device->DCO_reported_real_max_sectors = 0;
    next_device->HPA_status = HPA_NOT_APPLICABLE;

    /* All device strings should be 4 characters, prefix with space if under 4 characters
     * We also set a switch for certain devices to check for the host protected area (HPA)
     */
    check_HPA = 0;

    // WARNING TEMP LINE WARNING
    // next_device->device_type = NWIPE_DEVICE_ATA;

    switch( next_device->device_type )
    {
        case NWIPE_DEVICE_UNKNOWN:
            strcpy( next_device->device_type_str, " UNK" );
            check_HPA = 1;
            break;

        case NWIPE_DEVICE_IDE:
            strcpy( next_device->device_type_str, " IDE" );
            check_HPA = 1;
            break;

        case NWIPE_DEVICE_SCSI:
            strcpy( next_device->device_type_str, " SCSI" );
            check_HPA = 1;
            break;

        case NWIPE_DEVICE_COMPAQ:
            strcpy( next_device->device_type_str, " CPQ" );
            break;

        case NWIPE_DEVICE_USB:
            strcpy( next_device->device_type_str, " USB" );
            check_HPA = 1;
            break;

        case NWIPE_DEVICE_IEEE1394:
            strcpy( next_device->device_type_str, "1394" );
            break;

        case NWIPE_DEVICE_ATA:
            strcpy( next_device->device_type_str, " ATA" );
            check_HPA = 1;
            break;

        case NWIPE_DEVICE_NVME:
            strcpy( next_device->device_type_str, "NVME" );
            break;

        case NWIPE_DEVICE_VIRT:
            strcpy( next_device->device_type_str, "VIRT" );
            break;

        case NWIPE_DEVICE_SAS:
            strcpy( next_device->device_type_str, " SAS" );
            break;

        case NWIPE_DEVICE_MMC:
            strcpy( next_device->device_type_str, " MMC" );
            break;
    }
    if( next_device->device_is_ssd )
    {
        strcpy( next_device->device_type_str + 4, "-SSD" );
    }
    else
    {
        strcpy( next_device->device_type_str + 4, "    " );
    }

    if( strlen( (const char*) next_device->device_serial_no ) )
    {
        snprintf( next_device->device_label,
                  NWIPE_DEVICE_LABEL_LENGTH,
                  "%s %s [%s] %s/%s",
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
                  "%s %s [%s] %s",
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

    nwipe_log( NWIPE_LOG_INFO,
               "%s, sector(logical)/block(physical) sizes %i/%i",
               next_device->device_name,
               dev->sector_size,
               dev->phys_sector_size );

    if( next_device->device_busy )
    {
        nwipe_log( NWIPE_LOG_WARNING, "%s is reported as IN USE (it could be mounted)", next_device->device_name );
    }

    /******************************
     * Check for hidden sector_size
     */
    if( check_HPA == 1 )
    {
        hpa_dco_status( next_device );
    }

    /*************************************
     * Check whether the device has a UUID
     */
    char uuid[UUID_SIZE] = "";
    if( get_device_uuid( next_device->device_name, uuid ) == 0 )
    {
        strncpy( next_device->device_UUID, uuid, UUID_SIZE );
        nwipe_log( NWIPE_LOG_INFO, "UUID for %s is: %s\n", next_device->device_name, next_device->device_UUID );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "No UUID available for %s\n", next_device->device_name );
    }

    /* print an empty line to separate the drives in the log */
    nwipe_log( NWIPE_LOG_INFO, " " );

    ( *c )[dcount] = next_device;
    return 1;
}

/* Remove leading/trailing whitespace from a string and left justify result */
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

/*
 * Remove non-ASCII and control characters from a serial number string,
 * then trim leading/trailing whitespace and left-justify it in-place.
 * This keeps the value safe for ncurses output.
 */
static void nwipe_normalize_serial( char* serial )
{
    unsigned char ch;
    char* src;
    char* dst;

    if( serial == NULL )
    {
        return;
    }

    src = dst = serial;

    while( ( ch = (unsigned char) *src++ ) != '\0' )
    {
        if( isascii( ch ) && !iscntrl( ch ) )
        {
            *dst++ = (char) ch;
        }
        /* Alle remaining control characters will be dropped ( >0x7F) */
    }

    *dst = '\0';

    /* Use existing trim() function */
    trim( serial );
}

int nwipe_get_device_bus_type_and_serialno( char* device,
                                            nwipe_device_t* bus,
                                            int* is_ssd,
                                            char* serialnumber,
                                            char* sysfs_path,
                                            size_t sysfs_path_size )

{
    /* The caller provides a string that contains the device, i.e. /dev/sdc, also a pointer
     * to an integer (bus type), another pointer to an integer (is_ssd), and finally a 21 byte
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
     * 5 = smartctl detected unsupported USB to IDE/SATA adapter
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
    int idx;
    int idx2;

    char readlink_command[] = "readlink /sys/block/%s";
    char readlink_command2[] = "/usr/bin/readlink /sys/block/%s";
    char readlink_command3[] = "/sbin/readlink /sys/block/%s";
    char smartctl_command[] = "smartctl -i %s";
    char smartctl_command2[] = "/sbin/smartctl -i %s";
    char smartctl_command3[] = "/usr/bin/smartctl -i %s";
    char smartctl_command4[] = "/usr/sbin/smartctl -i %s";
    char device_shortform[50];
    char result[512];
    char final_cmd_readlink[sizeof( readlink_command ) + sizeof( device_shortform )];
    char final_cmd_smartctl[sizeof( smartctl_command ) + 256];
    char* pResult;
    char smartctl_labels_to_anonymize[][18] = {
        "serial number:", "lu wwn device id:", "logical unit id:", "" /* Don't remove this empty string !, important */
    };

    /* Ensure the serialnumber buffer is in a defined state even if we
     * never find a "serial number:" line in smartctl output.
     */
    if( serialnumber != NULL )
    {
        memset( serialnumber, 0, NWIPE_SERIALNUMBER_LENGTH + 1 );
    }

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
                set_return_value = 2;

                /* Return immediately if --nousb specified. Readlink is a requirement for this option. */
                if( nwipe_options.nousb )
                {
                    return set_return_value;
                }
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
                strip_CR_LF( result );

                if( sysfs_path != NULL && sysfs_path_size > 0 )
                {
                    strncpy( sysfs_path, result, sysfs_path_size - 1 );
                    sysfs_path[sysfs_path_size - 1] = '\0';
                }

                if( nwipe_options.verbose )
                {
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
                    else
                    {
                        if( strstr( result, "/nvme/" ) != 0 )
                        {
                            *bus = NWIPE_DEVICE_NVME;
                        }
                        else
                        {
                            if( strstr( result, "/virtual/" ) != 0 )
                            {
                                *bus = NWIPE_DEVICE_VIRT;
                            }
                            else
                            {
                                if( strstr( result, "/mmcblk" ) != 0 )
                                {
                                    *bus = NWIPE_DEVICE_MMC;
                                }
                            }
                        }
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
                    set_return_value = 2;
                    if( nwipe_options.nousb )
                    {
                        return set_return_value;
                    }
                }
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
                if( system( "which /usr/sbin/smartctl > /dev/null 2>&1" ) )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install smartmontools !" );
                }
                else
                {
                    sprintf( final_cmd_smartctl, smartctl_command4, device );
                }
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
                /* Convert the label, i.e everything before the ':' to lower case, it's required to
                 * convert to lower case as smartctl seems to use inconsistent case when labeling
                 * for serial number, i.e mostly it produces labels "Serial Number:" but occasionally
                 * it produces a label "Serial number:" */

                idx = 0;
                while( result[idx] != 0 && result[idx] != ':' )
                {
                    /* If upper case alpha character, change to lower case */
                    if( result[idx] >= 'A' && result[idx] <= 'Z' )
                    {
                        result[idx] = tolower( result[idx] );
                    }

                    idx++;
                }

                if( nwipe_options.verbose && result[0] != 0x0A )
                {
                    strip_CR_LF( result );

                    /* Remove serial number if -q option specified */
                    if( nwipe_options.quiet )
                    {
                        /* initialise index into string array */
                        idx2 = 0;

                        while( smartctl_labels_to_anonymize[idx2][0] != 0 )
                        {
                            if( ( pResult = strstr( result, &smartctl_labels_to_anonymize[idx2][0] ) ) != 0 )
                            {
                                /* set index to character after end of label string */
                                idx = strlen( &smartctl_labels_to_anonymize[idx2][0] );

                                /* Ignore spaces, overwrite other characters */
                                while( *( pResult + idx ) != 0x0A && *( pResult + idx ) != 0x0D
                                       && *( pResult + idx ) != 0 && idx <= sizeof( result ) - 1 )
                                {
                                    if( *( pResult + idx ) == ' ' )
                                    {
                                        idx++;
                                        continue;
                                    }
                                    else
                                    {
                                        /* ignore if the serial number has been written over with '?' */
                                        if( *( pResult + idx ) != '?' )
                                        {
                                            *( pResult + idx ) = 'X';
                                        }
                                        idx++;
                                    }
                                }
                            }
                            idx2++;
                        }
                    }

                    nwipe_log( NWIPE_LOG_INFO, "smartctl: %s", result );
                }

                if( strstr( result, "serial number:" ) != 0 )
                {
                    /* strip any leading or trailing spaces and left justify, +15 is the length of "Serial Number:" */
                    trim( &result[15] );

                    strncpy( serialnumber, &result[15], NWIPE_SERIALNUMBER_LENGTH );
                    serialnumber[NWIPE_SERIALNUMBER_LENGTH] = 0;
                }

                if( *bus == 0 )
                {
                    if( strstr( result, "transport protocol:" ) != 0 )
                    {
                        /* strip any leading or trailing spaces and left justify, +4 is the length of "bus type:" */
                        trim( &result[19] );
                        for( idx = 19; result[idx]; idx++ )
                        {
                            result[idx] = tolower( result[idx] );
                        }

                        if( strncmp( &result[19], "sas", 3 ) == 0 )
                        {
                            *bus = NWIPE_DEVICE_SAS;
                        }
                    }

                    if( strstr( result, "sata version is:" ) != 0 )
                    {

                        /* strip any leading or trailing spaces and left justify, +4 is the length of "bus type:" */
                        trim( &result[16] );
                        for( idx = 16; result[idx]; idx++ )
                        {
                            result[idx] = tolower( result[idx] );
                        }

                        if( strncmp( &result[16], "sata", 4 ) == 0 )
                        {
                            *bus = NWIPE_DEVICE_ATA;
                        }
                    }
                }
                if( strstr( result, "rotation rate:" ) != 0 )
                {
                    /* strip any leading or trailing spaces and left justify, +15 is the length of "Rotation Rate:" */
                    trim( &result[15] );
                    for( idx = 15; result[idx]; idx++ )
                    {
                        result[idx] = tolower( result[idx] );
                    }

                    if( strncmp( &result[15], "solid state device", 19 ) == 0 )
                    {
                        *is_ssd = 1;
                    }
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
                    nwipe_log( NWIPE_LOG_WARNING, "Smartctl is unable to provide smart data for %s", device );

                    if( *bus == NWIPE_DEVICE_USB || *bus == NWIPE_DEVICE_MMC )
                    {
                        strcpy( serialnumber, "(S/N: unknown)" );
                        set_return_value = 5;
                    }
                }
            }
        }
    }

    return set_return_value;
}

void remove_ATA_prefix( char* str )
{
    /* Remove "ATA" prefix if present in the model no. string, left justifing string */

    int idx_pre = 4;
    int idx_post = 0;

    if( !strncmp( str, "ATA ", 4 ) )
    {
        while( str[idx_pre] != 0 )
        {
            str[idx_post++] = str[idx_pre++];
        }

        str[idx_post] = 0;
    }
}

/* Returns 1 if n is positive and a power of two, 0 otherwise. */
static inline int is_positive_power_of_two( int n )
{
    return n > 0 && ( n & ( n - 1 ) ) == 0;
}

/*
 * Query kernel for the device's logical/physical sector sizes via ioctl,
 * cross-check them against the values previously obtained from libparted,
 * and update the device context with the most trustworthy and sane result.
 *
 * - All sizes must be positive and a power of two.
 * - Physical sector size must be a multiple of logical sector size.
 * - Ioctl values are preferred over libparted when otherwise valid.
 * - Invalid logical sector size is considered as fatal (returns -1).
 *
 * Returns 0 on success, -1 if no valid geometry could be established.
 */
static int nwipe_update_device_sectors( nwipe_context_t* c )
{
    int ioctl_sector_size = 0;
    int ioctl_phys_sector_size = 0;
    int libparted_sector_size = c->device_sector_size;
    int libparted_phys_sector_size = c->device_phys_sector_size;

    /* ---- Ioctl ( we have libparted as fallback ) ---- */
    if( ioctl( c->device_fd, BLKSSZGET, &ioctl_sector_size ) != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "BLKSSZGET" );
        nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed ioctl BLKSSZGET.", c->device_name );
    }

    if( ioctl( c->device_fd, BLKPBSZGET, &ioctl_phys_sector_size ) != 0 )
    {
        nwipe_perror( errno, __FUNCTION__, "BLKPBSZGET" );
        nwipe_log( NWIPE_LOG_WARNING, "Device '%s' failed ioctl BLKPBSZGET.", c->device_name );
    }

    /* ---- Logical sector size ---- */
    if( is_positive_power_of_two( ioctl_sector_size ) ) /* Ioctl */
    {
        if( ioctl_sector_size != libparted_sector_size )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "Device '%s' logical sector size mismatch: libparted=%i, ioctl=%i, using ioctl value.",
                       c->device_name,
                       libparted_sector_size,
                       ioctl_sector_size );
        }
        c->device_sector_size = ioctl_sector_size;
    }
    else if( is_positive_power_of_two( libparted_sector_size ) ) /* Libparted */
    {
        nwipe_log( NWIPE_LOG_WARNING,
                   "Device '%s' ioctl logical sector size invalid (%i), using libparted value (%i)",
                   c->device_name,
                   ioctl_sector_size,
                   libparted_sector_size );
        c->device_sector_size = libparted_sector_size;
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Device '%s' no valid logical sector size: libparted=%i, ioctl=%i.",
                   c->device_name,
                   libparted_sector_size,
                   ioctl_sector_size );
        return -1;
    }

    /* ---- Physical sector size ---- */
    if( is_positive_power_of_two( ioctl_phys_sector_size )
        && ioctl_phys_sector_size % c->device_sector_size == 0 ) /* Ioctl */
    {
        if( ioctl_phys_sector_size != libparted_phys_sector_size )
        {
            nwipe_log( NWIPE_LOG_WARNING,
                       "Device '%s' physical sector size mismatch: libparted=%i, ioctl=%i, using ioctl value.",
                       c->device_name,
                       libparted_phys_sector_size,
                       ioctl_phys_sector_size );
        }
        c->device_phys_sector_size = ioctl_phys_sector_size;
    }
    else if( is_positive_power_of_two( libparted_phys_sector_size )
             && libparted_phys_sector_size % c->device_sector_size == 0 ) /* Libparted */
    {
        nwipe_log( NWIPE_LOG_WARNING,
                   "Device '%s' ioctl physical sector size invalid (%i), using libparted value (%i).",
                   c->device_name,
                   ioctl_phys_sector_size,
                   libparted_phys_sector_size );
        c->device_phys_sector_size = libparted_phys_sector_size;
    }
    else
    {
        nwipe_log( NWIPE_LOG_WARNING,
                   "Device '%s' no valid physical sector size: libparted=%i, ioctl=%i, using logical sector size.",
                   c->device_name,
                   libparted_phys_sector_size,
                   ioctl_phys_sector_size );

        /*
         * We allow this to fail, as it's only used as a performance boost
         * But downstream users of this variable cannot use the invalid value.
         * It is common to use the logical sector size as a safe fallback here.
         */
        c->device_phys_sector_size = c->device_sector_size;
    }

    return 0;
} /* nwipe_update_device_sectors */

/*
 * Sets minimum I/O buffer alignment for a given device context.
 *
 * This is the logical sector size, raised to at least 512 bytes
 * to satisfy O_DIRECT requirements - also usable for cached I/O.
 */
static void nwipe_set_io_buffer_alignment( nwipe_context_t* c )
{
    if( c->device_sector_size < 512 )
    {
        /* O_DIRECT requires at least 512 bytes */
        c->device_io_buffer_alignment = 512;
    }
    else
    {
        /* The logical sector size is the minimum requirement */
        c->device_io_buffer_alignment = (size_t) c->device_sector_size;
    }
} /* nwipe_set_io_buffer_alignment */

/*
 * Sets effective I/O block size for a given device context.
 *
 * For direct I/O, aligns to the physical sector size (falling back to
 * logical sector size) for performance (where possible). For cached I/O,
 * uses st_blksize as a performance hint; alignment is left to the kernel.
 *
 * It is a multiple of the alignment base and never exceeds device_size.
 * A warning is printed if device_size is misaligned with logical sector size.
 */
static void nwipe_set_io_block_size( nwipe_context_t* c )
{
    size_t bs = 0;
    size_t io_bs = (size_t) NWIPE_IO_BLOCKSIZE;
    size_t st_blksize = 4096; /* Sane default (covers 4K and 512) */
    size_t dev_sector_size = (size_t) c->device_sector_size;
    size_t dev_phy_sector_size = (size_t) c->device_phys_sector_size;

    /* We use external values only if we can trust them to be accurate */

    if( is_positive_power_of_two( (int) c->device_stat.st_blksize ) )
        st_blksize = (size_t) c->device_stat.st_blksize;

    /* Sanity-check the device size and warn if it's not a multiple of the logical sector size */

    if( c->device_size > 0 && c->device_size % (u64) dev_sector_size != 0 )
    {
        nwipe_log( NWIPE_LOG_WARNING,
                   "%s: The size of '%s' is not a multiple of its logical sector size %zu.",
                   __FUNCTION__,
                   c->device_name,
                   dev_sector_size );
    }

    /* Now calculate the I/O size based on the I/O access mode requirements */

    if( c->io_mode == NWIPE_IO_MODE_DIRECT )
    {
        /* Alignment with logical sector size is the hard requirement */
        bs = dev_sector_size;

        if( dev_phy_sector_size % dev_sector_size == 0 )
        {
            /*
             * For best performance, we align to the physical sector size.
             * We can only do this if it is aligned to the logical sector size.
             * But this is usually always the case with any sane block devices.
             */
            bs = dev_phy_sector_size;
        }
    }
    else
    {
        /*
         * In cached I/O we use the recommended buffered block size,
         * we do not need to consider any direct I/O alignment rules.
         */
        bs = st_blksize;
    }

    c->device_io_block_alignment = bs;

    /* We cannot go lower than our chosen minimum block size */
    if( io_bs < bs )
    {
        io_bs = bs;
    }

    /* Round down to next multiple of the minimum block size. */
    if( io_bs % bs != 0 )
    {
        io_bs -= ( io_bs % bs );
    }

    /* This shouldn't be possible here, but just in case. */
    if( io_bs == 0 )
    {
        io_bs = bs;
    }

    /*
     * Clamp to device size; safe because device_size should be a multiple of
     * logical sector size, so the result would also be aligned for direct I/O.
     */
    if( (u64) io_bs > c->device_size )
    {
        io_bs = (size_t) c->device_size;
        io_bs -= ( io_bs % bs );
        if( io_bs == 0 )
            io_bs = bs;
    }

    c->device_io_block_size = io_bs;

} /* nwipe_set_io_block_size */

/*
 * Request device geometry from the kernel and compute I/O parameters.
 * This populates the context with the correct values usable for any I/O.
 *
 * Queries sector sizes via ioctl (with libparted as fallback), then
 * sets the alignment and block size used by subsequent I/O operations.
 *
 * Returns 0 on success, -1 if no valid geometry could be established.
 * Requires open FD; call right before a wipe begins (but after HPA/DCO).
 */
int nwipe_update_geometry_for_io( nwipe_context_t* c )
{
    if( nwipe_update_device_sectors( c ) != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "No sane sector sizes for '%s'.", c->device_name );
        return -1;
    }

    nwipe_set_io_buffer_alignment( c );
    nwipe_set_io_block_size( c );

    return 0;
} /* nwipe_update_geometry_for_io */
