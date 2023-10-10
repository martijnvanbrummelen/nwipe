/*
 * Copyright (C) 2002  Emmanuel VARAGNAT <hddtemp@guzu.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __HDDTEMP_H__
#define __HDDTEMP_H__

#include <time.h>
// #include "db.h"

//#ifdef ARCH_I386
//typedef unsigned short u16;
//#endif
#include <linux/types.h>
typedef __u16 u16;

#define MAX_ERRORMSG_SIZE      128
#define DEFAULT_ATTRIBUTE_ID   194

#define F_to_C(val) (int)(((double)(val)-32.0)/1.8)
#define C_to_F(val) (int)(((double)(val)*(double)1.8) + (double)32.0)

enum e_bustype { ERROR = 0, BUS_UNKNOWN, BUS_SATA, BUS_ATA, BUS_SCSI, BUS_TYPE_MAX };
// enum e_gettemp {
//   GETTEMP_ERROR,            /* Error */
//   GETTEMP_NOT_APPLICABLE,   /* */
//   GETTEMP_UNKNOWN,          /* Drive is not in database */
//   GETTEMP_GUESS,            /* Not in database, but something was guessed, user must
// 			       check that the temperature returned is correct */
//   GETTEMP_KNOWN,            /* Drive appear in database */
//   GETTEMP_NOSENSOR,         /* Drive appear in database but is known to have no sensor */
//   GETTEMP_DRIVE_SLEEP,      /* Drive is sleeping */
//   GETTEMP_SUCCESS           /* read temperature successfully */
// };

#define GETTEMP_SUCCESS 0
#define GETTEMP_ERROR 1
#define GETTEMP_NOT_APPLICABLE 2
#define GETTEMP_UNKNOWN 3
#define GETTEMP_GUESS 4
#define GETTEMP_KNOWN 5
#define GETTEMP_NOSENSOR 6
#define GETTEMP_DRIVE_SLEEP 7

enum e_powermode {
  PWM_UNKNOWN,
  PWM_ACTIVE,
  PWM_SLEEPING,
  PWM_STANDBY
};


struct disk {
  struct disk *            next;

  int                      fd;
  const char *             drive;
  const char *             model;
  enum e_bustype           type;
  int                      value;      /* the drive's temperature */
  int                      refvalue;   /* aka trip temperature    */
  struct harddrive_entry * db_entry;

  char                     errormsg[MAX_ERRORMSG_SIZE];
//  enum e_gettemp           ret;
  int		           ret;
  time_t                   last_time;
};

struct bustype {
  char *name;
  int (*probe)(int);
  const char *(*model)(int);
  enum e_gettemp (*get_temperature)(struct disk *);
};


extern struct bustype *   bus[BUS_TYPE_MAX];
extern char               errormsg[MAX_ERRORMSG_SIZE];
extern int                tcp_daemon, debug, quiet, wakeup, af_hint;
extern char               separator;
extern long               portnum, syslog_interval;
extern char *             listen_addr;

int value_to_unit(struct disk *dsk);
char get_unit(struct disk *dsk);

#endif
