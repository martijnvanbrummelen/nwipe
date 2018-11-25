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

#include <stdint.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "options.h"
#include "logging.h"
#include <sys/ioctl.h>
#include <linux/hdreg.h> //Drive specific defs
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include <parted/parted.h>
#include <parted/debug.h>

int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount );
char *trim(char *str);

int nwipe_device_scan( nwipe_context_t*** c )
{
	/**
	 * Scans the the filesystem for storage device names.
	 *
	 * @parameter device_names  A reference to a null array pointer.
	 * @modifies  device_names  Populates device_names with an array of nwipe_contect_t
	 * @returns                 The number of strings in the device_names array.
	 *
	 */

	PedDevice* dev = NULL;
        ped_device_probe_all();
	
	int dcount = 0;

        while ((dev = ped_device_get_next (dev)))
        {
                if (check_device(c, dev, dcount))
                        dcount++;
	}

	/* Return the number of devices that were found. */
	return dcount;

} /* nwipe_device_scan */

int nwipe_device_get( nwipe_context_t*** c, char **devnamelist, int ndevnames )
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

	for(i = 0; i < ndevnames; i++) {

		dev = ped_device_get(devnamelist[i]);
		if (!dev)
			break;

		if (check_device(c, dev, dcount))
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

	/* Try opening the device to see if it's valid. Close on completion. */
	if (!ped_device_open(dev))
	{
		nwipe_log( NWIPE_LOG_FATAL, "Unable to open device" );
		return 0;
	}
	ped_device_close(dev);
        
	/* New device, reallocate memory for additional struct pointer */
	*c = realloc (*c, (dcount+1) * sizeof(nwipe_context_t *));
        
	next_device = malloc (sizeof(nwipe_context_t)); 

	/* Check the allocation. */
	if( ! next_device )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to create the array of enumeration contexts." );
		return 0;
	}

	/* Zero the allocation. */
	memset( next_device , 0, sizeof( nwipe_context_t ) );

	/* Get device information */
	next_device->label = dev->model;
	next_device->device_name = dev->path;
	next_device->device_size = dev->length * dev->sector_size;
	next_device->device_size_text = ped_unit_format_byte(dev, dev->length * dev->sector_size);
	/* Attempt to get serial number of device. */
	if ( (fd = open ( next_device->device_name = dev->path, O_RDONLY)) == ERR )
	{
		nwipe_log( NWIPE_LOG_WARNING, "Unable to open device %s to obtain serial number", next_device->device_name );
	}
	/* We don't check the ioctl return status because there are plenty of situations where a serial number may not be
	 * returned by ioctl such as USB drives, logical volumes, encryted volumes, so the log file would have multiple
	 * benign ioctl errors reported which isn't necessarily a problem.
	 */
	ioctl(fd, HDIO_GET_IDENTITY, &next_device->identity);
	close( fd );

    int idx;
    for (idx=0; idx<20; idx++) next_device->serial_no[idx]=next_device->identity.serial_no[idx];
    
    next_device->serial_no[20]=0;               /* terminate the string */
    trim ( (char*) next_device->serial_no );    /* Remove leading/training whitespace from serial number and left justify */

    nwipe_log( NWIPE_LOG_INFO,"Found drive model=\"%s\", device path=\"%s\", size=\"%s\", serial number=\"%s\"", next_device->label, next_device->device_name, next_device->device_size_text, next_device->serial_no);

	(*c)[dcount] = next_device;
        
	return 1;
}

/* Remove leading/training whitespace from a string and left justify result */
char *trim(char *str)
{
	size_t len = 0;
	char *frontp = str;
	char *endp = NULL;

	if( str == NULL )
	{ 
		return NULL;
	}
	if( str[0] == '\0' )
	{ 
		return str;
	}
	len = strlen(str);
	endp = str + len;

	/* Move the front and back pointers to address the first non-whitespace
	 * characters from each end.
	 */
	while( isspace((unsigned char) *frontp) ) { ++frontp; }
	if( endp != frontp )
	{
		while( isspace((unsigned char) *(--endp)) && endp != frontp ) {}
	}
	if( str + len - 1 != endp )
		*(endp + 1) = '\0';
	else if( frontp != str &&  endp == frontp )
		*str = '\0';
	/* Shift the string so that it starts at str so that if it's dynamically
	 * allocated, we can still free it on the returned pointer.  Note the reuse
	 * of endp to mean the front of the string buffer now.
	 */
	endp = str;
	if( frontp != str )
	{
		while( *frontp ) { *endp++ = *frontp++; }
		*endp = '\0';
	}
	return str;
}

/* eof */
