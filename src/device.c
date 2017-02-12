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

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "options.h"
#include "logging.h"

#include <parted/parted.h>
#include <parted/debug.h> 

int check_device( nwipe_context_t*** c, PedDevice* dev, int dcount );

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

        /* Try opening the device to see if it's valid. Close on completion. */
        if (!ped_device_open(dev))
                return 0;
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
        /* Attempt to get serial number of device. */
        ioctl(next_device->device_fd, HDIO_GET_IDENTITY, &next_device->identity);

        (*c)[dcount] = next_device;
        
        return 1;
}

/* eof */
