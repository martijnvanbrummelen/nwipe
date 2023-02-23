/*
 *  hpa_dco.c: functions that handle the host protected area (HPA) and
 *  device configuration overlay (DCO)
 *
 *  Copyright PartialVolume <https://github.com/PartialVolume>.
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
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "nwipe.h"
#include "context.h"
#include "version.h"
#include "method.h"
#include "logging.h"
#include "options.h"
#include "hpa_dco.h"

int hpa_dco_status( nwipe_context_t* ptr, int pre_or_post )
{
    nwipe_context_t* c;
    c = ptr;

    int r;  // A result buffer.
    int set_return_value;
    int exit_status;

    FILE* fp;
    char hdparm_command[] = "hdparm -N %s";
    char hdparm_command2[] = "/sbin/hdparm -N %s";
    char hdparm_command3[] = "/usr/bin/hdparm -N %s";
    char result[512];
    char device_shortform[50];
    char final_cmd_hdparm[sizeof( hdparm_command ) + sizeof( device_shortform )];

    /* Initialise return value */
    set_return_value = 0;

    if( system( "which hdparm > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/hdparm > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/hdparm > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "hdparm command not found." );
                nwipe_log( NWIPE_LOG_WARNING,
                           "Required by nwipe for HPA/DCO detection & correction and ATA secure erase." );
                nwipe_log( NWIPE_LOG_WARNING, "** Please install hdparm **\n" );
                cleanup();
                exit( 1 );
            }
            else
            {
                sprintf( final_cmd_hdparm, hdparm_command3, device_shortform );
            }
        }
        else
        {
            sprintf( final_cmd_hdparm, hdparm_command2, device_shortform );
        }
    }
    else
    {
        sprintf( final_cmd_hdparm, hdparm_command, device_shortform );
    }

    if( final_cmd_hdparm[0] != 0 )
    {

        fp = popen( final_cmd_hdparm, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING, "hpa_dco_status: Failed to create stream to %s", hdparm_command );

            set_return_value = 1;
        }

        if( fp != NULL )
        {
            /* Read the output a line at a time - output it. */
            if( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                if( nwipe_options.verbose )
                {
                    nwipe_log( NWIPE_LOG_DEBUG, "hdparm -N: %s\n%s", c->device_name, result );
                }

                /* Scan the hdparm results for HPA is disabled
                 */
                if( pre_or_post == PRE_WIPE_HPA_CHECK )
                {
                    if( strstr( result, "HPA is disabled" ) != 0 )
                    {
                        c->HPA_pre_erase_status = HPA_DISABLED;
                        nwipe_log( NWIPE_LOG_INFO, "[GOOD]The host protected area is disabled on %s", c->device_name );
                    }
                    else
                    {
                        if( strstr( result, "HPA is enabled" ) != 0 )
                        {
                            c->HPA_pre_erase_status = HPA_ENABLED;
                            nwipe_log(
                                NWIPE_LOG_WARNING, "[BAD]The host protected area is enabled on %s", c->device_name );
                        }
                        else
                        {
                            c->HPA_pre_erase_status = HPA_UNKNOWN;
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
                               "hpa_dco_status(): hdparm failed, \"%s\" exit status = %u",
                               final_cmd_hdparm,
                               exit_status );
                }

                if( exit_status == 127 )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Installing hdparm is mandatory !" );
                    set_return_value = 2;
                    if( nwipe_options.nousb )
                    {
                        return set_return_value;
                    }
                }
            }
        }
    }
    return set_return_value;
}
