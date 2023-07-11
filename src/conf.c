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
#include "conf.h"

config_t nwipe_cfg;
config_setting_t *nwipe_conf_setting, *group_organisation, *group_customers, *group_customer_1, *root, *group, *setting;
const char* nwipe_conf_str;
char nwipe_config_file[] = "/etc/nwipe/nwipe.conf";
char nwipe_config_directory[] = "/etc/nwipe";

int nwipe_conf_init()
{
    FILE* fp;

    config_init( &nwipe_cfg );
    root = config_root_setting( &nwipe_cfg );

    /* Read /etc/nwipe/nwipe.conf. If there is an error, determine whether
     * it's because it doesn't exist. If it doesn't exist create it and
     * populate it with a basic structure.
     */

    /* Does the /etc/nwipe/nwipe.conf file exist? If not, then create it */
    if( access( nwipe_config_file, F_OK ) == 0 )
    {
        nwipe_log( NWIPE_LOG_INFO, "NWIPE_CONFIG_FILE %s exists", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_WARNING, "/etc/nwipe/nwipe.conf does not exist" );

        /* We assume the /etc/nwipe directory doesn't exist, so try to create it */
        mkdir( nwipe_config_directory, 0755 );

        /* create the nwipe.conf file */
        if( !( fp = fopen( nwipe_config_file, "w" ) ) )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Failed to create %s", nwipe_config_file );
        }
        else
        {
            nwipe_log( NWIPE_LOG_INFO, "Created %s", nwipe_config_file );

            /* Populate with some basic structure */

            /* Add some settings to the configuration. */
            group_organisation = config_setting_add( root, "Organisation_Details", CONFIG_TYPE_GROUP );

            setting = config_setting_add( group_organisation, "Business_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Universal Erasure Ltd" );

            setting = config_setting_add( group_organisation, "Business_Address", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "85 Albert Embankment" );

            setting = config_setting_add( group_organisation, "Contact_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "V.Lynd" );

            setting = config_setting_add( group_organisation, "Contact_Phone", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "-------------" );

            group_customers = config_setting_add( root, "Customers", CONFIG_TYPE_GROUP );

            group_customer_1 = config_setting_add( group_customers, "Customer_1", CONFIG_TYPE_GROUP );

            setting = config_setting_add( group_customer_1, "Customer_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Erase My Discs" );

            setting = config_setting_add( group_customer_1, "Contact_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "J.B" );

            setting = config_setting_add( group_customer_1, "Customer_Address", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Somewhere St, Chelsea, London." );

            /* Write out the new configuration. */
            if( !config_write_file( &nwipe_cfg, nwipe_config_file ) )
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to write basic config to %s", nwipe_config_file );
                config_destroy( &nwipe_cfg );
                return ( -1 );
            }
            else
            {
                nwipe_log( NWIPE_LOG_INFO, "Populated %s with basic config", nwipe_config_file );
            }
        }
    }

    if( !config_read_file( &nwipe_cfg, nwipe_config_file ) )
    {
        fprintf( stderr,
                 "%s:%d - %s\n",
                 config_error_file( &nwipe_cfg ),
                 config_error_line( &nwipe_cfg ),
                 config_error_text( &nwipe_cfg ) );
    }

    config_destroy( &nwipe_cfg );
    return ( 0 );
}

void nwipe_conf_close()
{
    config_destroy( &nwipe_cfg );
}
