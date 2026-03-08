/*
 *  device.h:  Device routines for nwipe.
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

#ifndef DEVICE_H_
#define DEVICE_H_

#include <limits.h>

#define MAX_LENGTH_OF_DEVICE_STRING 8

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

void nwipe_device_identify( nwipe_context_t* c );  // Get hardware information about the device.
int nwipe_device_scan( nwipe_context_t*** c );  // Find devices that we can wipe.

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
int nwipe_device_get( nwipe_context_t*** c, char** devnamelist, int ndevnames );  // Get info about devices to wipe.

int nwipe_get_device_bus_type_and_serialno( char* device,
                                            nwipe_device_t* bus,
                                            int* is_ssd,
                                            char* serialnumber,
                                            char* sysfs_path,
                                            size_t sysfs_path_size );

int nwipe_update_geometry_for_io( nwipe_context_t* c );

void strip_CR_LF( char* );
void determine_disk_capacity_nomenclature( u64, char* );
void remove_ATA_prefix( char* );
char* trim( char* );

#endif /* DEVICE_H_ */
