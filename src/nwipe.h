/*.
 *  nwipe.h: The header file of the nwipe program.
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

#ifndef NWIPE_H_
#define NWIPE_H_

/* Function prototypes */
int cleanup();
void check_for_autopoweroff( void );
void* signal_hand( void* );

/* Busybox headers. */
#ifdef BB_VER
#include "busybox.h"
#endif

/* System headers. */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Assert off_t is 64-bit (if this errors, it is not) */
#if defined( __STDC_VERSION__ ) && __STDC_VERSION__ >= 201112L
_Static_assert( sizeof( off_t ) == 8, "off_t must be 64-bit; is config.h (AC_SYS_LARGEFILE) included?" );
#else
typedef char off_t_must_be_64_bit[( sizeof( off_t ) == 8 ) ? 1 : -1];
#endif

/* System errors. */
extern int errno;

/* 0=wipe not yet started, 1=wipe has been started by the user */
extern int global_wipe_status;

/* Kernel device headers. */
#include <linux/hdreg.h>

/* These types are usually defined in <asm/types.h> for __KERNEL__ code. */
typedef unsigned long long u64;
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;

/* Define ioctls that cannot be included. */
#define BLKSSZGET _IO( 0x12, 104 )
#define BLKBSZGET _IOR( 0x12, 112, size_t )
#define BLKBSZSET _IOW( 0x12, 113, size_t )
#define BLKPBSZGET _IO( 0x12, 123 )
#define BLKGETSIZE64 _IOR( 0x12, 114, u64 )

#define THREAD_CANCELLATION_TIMEOUT 10

/* This is required for ioctl FDFLUSH. */
#include <linux/fd.h>

#endif /* NWIPE_H_ */
