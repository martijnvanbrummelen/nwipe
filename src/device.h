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

void nwipe_device_identify( nwipe_context_t* c );  /* Get hardware information about the device.  */
int nwipe_device_scan( nwipe_context_t*** c );     /* Find devices that we can wipe.              */
int nwipe_device_get( nwipe_context_t*** c, char **devnamelist, int ndevnames ); /* Get info about devices to wipe */

#endif /* DEVICE_H_ */

/* eof */
