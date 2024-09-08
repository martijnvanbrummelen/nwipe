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

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

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

/* workaround for Fedora */
#ifndef off64_t
#ifndef off_t
#define off64_t int64_t
#else
#define off64_t off_t
#endif
#endif

/*#include "config.h"*/

/* System errors. */
extern int errno;

/* 0=wipe not yet started, 1=wipe has been started by the user */
extern int global_wipe_status;

/* Ncurses headers. */
#ifdef NCURSES_IN_SUBDIR
#include <ncurses/ncurses.h>
#else
#include <ncurses.h>
#endif
#ifdef PANEL_IN_SUBDIR
#include <ncurses/panel.h>
#else
#include <panel.h>
#endif

/* Kernel device headers. */
#include <linux/hdreg.h>

/* These types are usually defined in <asm/types.h> for __KERNEL__ code. */
typedef unsigned long long u64;
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;

/* This is required for ioctl BLKGETSIZE64, but it conflicts with <wait.h>. */
/* #include <linux/fs.h> */

/* Define ioctls that cannot be included. */
#define BLKSSZGET _IO( 0x12, 104 )
#define BLKBSZGET _IOR( 0x12, 112, size_t )
#define BLKBSZSET _IOW( 0x12, 113, size_t )
#define BLKGETSIZE64 _IOR( 0x12, 114, sizeof( u64 ) )

#define THREAD_CANCELLATION_TIMEOUT 10

#define NWIPE_GITHUB_ISSUE_URL "https://github.com/martijnvanbrummelen/nwipe/issues"

/* This is required for ioctl FDFLUSH. */
#include <linux/fd.h>

#endif /* NWIPE_H_ */
