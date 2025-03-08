/*
 *  get_scsi_temp.c: functions that populate the drive temperature variables
 *  in SCSI/SAS drives context structure.
 *  Routines from hddtemp are used here.
 *
 *  Author: Gerold Gruber <Gerold.Gruber@edv2g.de>
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, see <http://www.gnu.org/licenses/>.
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
#include <assert.h>

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
#include "hddtemp.h"
#include "scsi.h"

int scsi_get_temperature( struct disk * );

int nwipe_init_scsi_temperature( nwipe_context_t* c )
{

/* dsk anlegen, malloc */
    struct disk *dsk = (struct disk *) malloc(sizeof(struct disk));
 
    /* Check the allocation. */
    if( !dsk )
    {
        nwipe_perror( errno, __FUNCTION__, "malloc" );
        nwipe_log( NWIPE_LOG_FATAL, "Unable to get memory for disk struct for %s",
                   c->device_name );
        exit( 1 ) ;
    }

    assert(dsk);

    memset(dsk, 0, sizeof(*dsk));

    /* save the dsk pointer for later use */
    c->templ_disk = dsk;

    /* initialize */
    dsk->drive = c->device_name;
    dsk->type = BUS_SCSI;     /* we know this as we are only called in this case */

    errno = 0;
    dsk->errormsg[0] = '\0';
    if( (dsk->fd = open(dsk->drive, O_RDONLY | O_NONBLOCK)) < 0) {
      snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, "open: %s\n", strerror(errno));
      dsk->type = ERROR;
      return 1;
    }

    // sg_logs -t <device>
    if( scsi_get_temperature( dsk ) == GETTEMP_SUCCESS )
    {
	c->temp1_input = dsk->value;
	c->temp1_crit = dsk->refvalue;
        c->temp1_lcrit = -40; /* just to give it a value with some kind of sense */
        c->temp1_highest = dsk->value;
        c->temp1_lowest = dsk->value;
        c->temp1_max = dsk->refvalue - 5; /* seems to be kind of useful */
    }
    else
    {
	nwipe_log( NWIPE_LOG_ERROR, "Can not read SCSI temperature for %s, %s",
	           dsk->drive, dsk->errormsg );
        close( dsk->fd );
	free( dsk );
	c->templ_disk = NULL;
	return 1;
    }

    return 0;
}


int nwipe_get_scsi_temperature( nwipe_context_t* c )
{
    struct disk *dsk;

    dsk = c->templ_disk;

    if( c->templ_disk != NULL && c->templ_disk->fd != -1 )
    {
	if( scsi_get_temperature( dsk ) == GETTEMP_SUCCESS )
	{
	    c->temp1_input = dsk->value;

/* not at all of interest */
            if( c->temp1_input > c->temp1_highest )
            {
                c->temp1_highest = c->temp1_input;
            }
            if( c->temp1_input < c->temp1_lowest )
            {
                c->temp1_lowest = c->temp1_input;
            }

/* end not at all of interest ;-) */

	}
	else
	{
	     nwipe_log( NWIPE_LOG_ERROR, "Could not read SCSI temperature for %s, %s",
                        dsk->drive, dsk->errormsg );
	     return 2;
        }
    }
    else
    {
	nwipe_log( NWIPE_LOG_INFO, "no SCSI temperature reading for %s", dsk->drive );
	return 1;
    }
    return 0;
}

void nwipe_shut_scsi_temperature( nwipe_context_t* c )
{
    if( c->templ_disk->fd != -1 )
    {
	close( c->templ_disk->fd );
    }
    if( c->templ_disk != NULL )
    {
	free( c->templ_disk );
    }

    return;
}
