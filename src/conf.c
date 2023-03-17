/*
 *  conf.c: functions that handle the nwipe.conf configuration file.
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
 *
 *
 */

#include <libconfig.h>
#include <unistd.h>
#include <sys/types.h>
#include "nwipe.h"
#include "context.h"
#include "logging.h"

config_t nwipe_cfg;
config_setting_t* nwipe_conf_setting;
const char* nwipe_conf_str;

void nwipe_conf_init()
{
    FILE* fp;

    config_init( &nwipe_cfg );

    /* Read /etc/nwipe/nwipe.conf. If there is an error, determine whether
     * it's because it doesn't exist. If it doesn't exist create it and
     * populate it with a basic structure.
     */
    if( !config_read_file( &nwipe_cfg, "/etc/nwipe/nwipe.conf" ) )
    {
        fprintf( stderr,
                 "%s:%d - %s\n",
                 config_error_file( &nwipe_cfg ),
                 config_error_line( &nwipe_cfg ),
                 config_error_text( &nwipe_cfg ) );

        /* Does the /etc/nwipe/nwipe.conf file exist? If not, then create it */
        if( access( "/etc/nwipe/nwipe.conf", F_OK ) == 0 )
        {
            nwipe_log( NWIPE_LOG_INFO, "/etc/nwipe/nwipe.conf exists" );
        }
        else
        {
            nwipe_log( NWIPE_LOG_WARNING, "/etc/nwipe/nwipe.conf does not exist" );

            /* We assume the /etc/nwipe directory doesn't exist, so try to create it */
            mkdir( "/etc/nwipe", 0755 );

            /* create the nwipe.conf file */
            if( !( fp = fopen( "/etc/nwipe/nwipe.conf", "w" ) ) )
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to create /etc/nwipe/nwipe.conf" );
            }
            else
            {
                nwipe_log( NWIPE_LOG_INFO, "Created /etc/nwipe/nwipe.conf" );

                /* Populate with some basic structure */
                // NOTE ADD CODE HERE NOTE
            }
        }
    }
    return;
}

void nwipe_conf_close()
{
    config_destroy( &nwipe_cfg );
}
