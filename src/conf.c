/*
 *  conf.c: functions that handle the nwipe.conf configuration file
 *  and the creation of the nwipe_customers.csv file. nwipe.conf uses
 *  libconfig format, while nwipe_customers.csv uses comma separted
 *  values. CSV is used so that the user can build there own customer
 *  listing using spreadsheets rather than enter all the customer
 *  information via the nwipe GUI interface.
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
#include "method.h"
#include "options.h"
#include "conf.h"

config_t nwipe_cfg;
config_setting_t *nwipe_conf_setting, *group_organisation, *root, *group, *previous_group, *setting;
const char* nwipe_conf_str;
char nwipe_config_directory[] = "/etc/nwipe";
char nwipe_config_file[] = "/etc/nwipe/nwipe.conf";
char nwipe_customers_file[] = "/etc/nwipe/nwipe_customers.csv";
char nwipe_customers_file_backup[] = "/etc/nwipe/nwipe_customers.csv.backup";
char nwipe_customers_file_backup_tmp[] = "/etc/nwipe/nwipe_customers.csv.backup.tmp";

/*
 * Checks for the existence of nwipe.conf and nwipe_customers.csv
 * If either one does not exist, they are created and formatted
 * with a basic configuration.
 */

int nwipe_conf_init()
{
    FILE* fp_config;
    FILE* fp_customers;

    config_init( &nwipe_cfg );
    char nwipe_customers_initial_content[] =
        "\"Customer Name\";\"Contact Name\";\"Customer Address\";\"Contact Phone\"\n"
        "\"Not Applicable\";\"Not Applicable\";\"Not Applicable\";\"Not Applicable\"\n";

    /* Read /etc/nwipe/nwipe.conf. If there is an error, determine whether
     * it's because it doesn't exist. If it doesn't exist create it and
     * populate it with a basic structure.
     */

    /* Does the /etc/nwipe/nwipe.conf file exist? If not, then create it */
    if( access( nwipe_config_file, F_OK ) == 0 )
    {
        nwipe_log( NWIPE_LOG_INFO, "Nwipes config file %s exists", nwipe_config_file );

        /* Read the nwipe.conf configuration file and report any errors */

        nwipe_log( NWIPE_LOG_INFO, "Reading nwipe's config file %s", nwipe_config_file );
        if( !config_read_file( &nwipe_cfg, nwipe_config_file ) )
        {
            nwipe_log( NWIPE_LOG_ERROR,
                       "Syntax error: %s:%d - %s\n",
                       config_error_file( &nwipe_cfg ),
                       config_error_line( &nwipe_cfg ),
                       config_error_text( &nwipe_cfg ) );
        }
    }
    else
    {
        nwipe_log( NWIPE_LOG_WARNING, "%s does not exist", nwipe_config_file );

        /* We assume the /etc/nwipe directory doesn't exist, so try to create it */
        mkdir( nwipe_config_directory, 0755 );

        /* create the nwipe.conf file */
        if( !( fp_config = fopen( nwipe_config_file, "w" ) ) )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Failed to create %s", nwipe_config_file );
        }
        else
        {
            nwipe_log( NWIPE_LOG_INFO, "Created %s", nwipe_config_file );
            fclose( fp_config );
        }
    }

    root = config_root_setting( &nwipe_cfg );

    /**
     * If they don't already exist, populate nwipe.conf with groups, settings and values.
     * This will also fill in missing group or settings if they have been corrupted or
     * accidently deleted by the user. It will also update an existing nwipe.conf
     * file as new groups and settings are added to nwipe. If new settings are added
     * to nwipes conf file they MUST appear below in this list of groups and settings.
     */

    nwipe_conf_populate( "Organisation_Details.Business_Name", "Not Applicable (BN)" );
    nwipe_conf_populate( "Organisation_Details.Business_Address", "Not Applicable (BA)" );
    nwipe_conf_populate( "Organisation_Details.Contact_Name", "Not Applicable (BCN)" );
    nwipe_conf_populate( "Organisation_Details.Contact_Phone", "Not Applicable (BCP)" );
    nwipe_conf_populate( "Organisation_Details.Op_Tech_Name", "Not Applicable (OTN)" );

    /**
     * Add PDF Certificate/Report settings
     */
    nwipe_conf_populate( "PDF_Certificate.PDF_Enable", "ENABLED" );
    nwipe_conf_populate( "PDF_Certificate.PDF_Preview", "DISABLED" );

    /**
     * The currently selected customer that will be printed on the report
     */
    nwipe_conf_populate( "Selected_Customer.Customer_Name", "Not Applicable (CN)" );
    nwipe_conf_populate( "Selected_Customer.Customer_Address", "Not Applicable (CA)" );
    nwipe_conf_populate( "Selected_Customer.Contact_Name", "Not Applicable (CCN)" );
    nwipe_conf_populate( "Selected_Customer.Contact_Phone", "Not Applicable (CP)" );

    /**
     * Write out the new configuration.
     */
    if( !config_write_file( &nwipe_cfg, nwipe_config_file ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write nwipe config to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "Sucessfully written nwipe config to %s", nwipe_config_file );
    }

    /* Read the nwipe.conf configuration file and report any errors */
    if( !config_read_file( &nwipe_cfg, nwipe_config_file ) )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Syntax error: %s:%d - %s\n",
                   config_error_file( &nwipe_cfg ),
                   config_error_line( &nwipe_cfg ),
                   config_error_text( &nwipe_cfg ) );
    }

    /* -----------------------------------------------------------------------------
     * Now check nwipe_customers.csv exists, if not create it with a basic structure
     */
    if( access( nwipe_customers_file, F_OK ) == 0 )
    {
        nwipe_log( NWIPE_LOG_INFO, "Nwipes customer file %s exists", nwipe_customers_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_WARNING, "%s does not exist", nwipe_customers_file );

        /* We assume the /etc/nwipe directory doesn't exist, so try to create it */
        mkdir( nwipe_config_directory, 0755 );

        /* create the nwipe_customers.csv file */
        if( !( fp_customers = fopen( nwipe_customers_file, "w" ) ) )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Failed to create %s", nwipe_customers_file );
        }
        else
        {
            nwipe_log( NWIPE_LOG_INFO, "Created %s", nwipe_customers_file );

            /* Now populate the csv header and first entry, Lines 1 and 2 */
            if( fwrite( nwipe_customers_initial_content, sizeof( nwipe_customers_initial_content ), 1, fp_customers )
                == 1 )
            {
                nwipe_log( NWIPE_LOG_INFO, "Populated %s with basic config", nwipe_customers_file );
            }
            else
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to write basic config to %s", nwipe_customers_file );
            }
            fclose( fp_customers );
        }
    }
    return ( 0 );
}

void save_selected_customer( char** customer )
{
    /* This function saves the user selected customer
     * to nwipe's config file /etc/nwipe/nwipe.conf
     * for later use by the PDF creation functions.
     */

    int idx;
    int field_count;
    int field_idx;

    char field_1[FIELD_LENGTH];
    char field_2[FIELD_LENGTH];
    char field_3[FIELD_LENGTH];
    char field_4[FIELD_LENGTH];

    /* zero the field strings */
    for( idx = 0; idx < FIELD_LENGTH; idx++ )
        field_1[idx] = 0;
    for( idx = 0; idx < FIELD_LENGTH; idx++ )
        field_2[idx] = 0;
    for( idx = 0; idx < FIELD_LENGTH; idx++ )
        field_3[idx] = 0;
    for( idx = 0; idx < FIELD_LENGTH; idx++ )
        field_4[idx] = 0;

    /* Extract the field contents from the csv string
     */
    idx = 0;
    field_idx = 0;
    field_count = 1;

    while( *( *customer + idx ) != 0 && field_count < NUMBER_OF_FIELDS + 1 )
    {
        /* Start of a field? */
        if( *( *customer + idx ) == '\"' )
        {
            idx++;

            while( *( *customer + idx ) != '\"' && *( *customer + idx ) != 0 )
            {
                if( field_count == 1 && field_idx < ( FIELD_LENGTH - 1 ) )
                {
                    field_1[field_idx++] = *( *customer + idx );
                }
                else
                {
                    if( field_count == 2 && field_idx < ( FIELD_LENGTH - 1 ) )
                    {
                        field_2[field_idx++] = *( *customer + idx );
                    }
                    else
                    {
                        if( field_count == 3 && field_idx < ( FIELD_LENGTH - 1 ) )
                        {
                            field_3[field_idx++] = *( *customer + idx );
                        }
                        else
                        {
                            if( field_count == 4 && field_idx < ( FIELD_LENGTH - 1 ) )
                            {
                                field_4[field_idx++] = *( *customer + idx );
                            }
                        }
                    }
                }
                idx++;
            }
            if( *( *customer + idx ) == '\"' )
            {
                /* Makesure the field string is terminated */
                switch( field_count )
                {
                    case 1:
                        field_1[field_idx] = 0;
                        break;
                    case 2:
                        field_2[field_idx] = 0;
                        break;
                    case 3:
                        field_3[field_idx] = 0;
                        break;
                    case 4:
                        field_4[field_idx] = 0;
                        break;
                }

                field_count++;
                field_idx = 0;
            }
        }
        idx++;
    }

    /* All 4 fields present? */
    if( field_count != NUMBER_OF_FIELDS + 1 )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Insuffient fields in customer entry, expected %i, actual(field_count) %i, idx=%i",
                   NUMBER_OF_FIELDS,
                   field_count,
                   idx );
        return;
    }

    /* -------------------------------------------------------------
     * Write the fields to nwipe's config file /etc/nwipe/nwipe.conf
     */
    if( ( setting = config_lookup( &nwipe_cfg, "Selected_Customer.Customer_Name" ) ) )
    {
        config_setting_set_string( setting, field_1 );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Can't find \"Selected Customers.Customer_Name\" in %s", nwipe_config_file );
    }

    if( ( setting = config_lookup( &nwipe_cfg, "Selected_Customer.Customer_Address" ) ) )
    {
        config_setting_set_string( setting, field_2 );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Can't find \"Selected Customers.Customer_Address\" in %s", nwipe_config_file );
    }

    if( ( setting = config_lookup( &nwipe_cfg, "Selected_Customer.Contact_Name" ) ) )
    {
        config_setting_set_string( setting, field_3 );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Can't find \"Selected Customers.Contact_Name\" in %s", nwipe_config_file );
    }

    if( ( setting = config_lookup( &nwipe_cfg, "Selected_Customer.Contact_Phone" ) ) )
    {
        config_setting_set_string( setting, field_4 );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Can't find \"Selected Customers.Contact_Phone\" in %s", nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( !config_write_file( &nwipe_cfg, nwipe_config_file ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write user selected customer to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "Populated %s with user selected customer", nwipe_config_file );
    }
}

int nwipe_conf_update_setting( char* group_name_setting_name, char* setting_value )
{
    /* You would call this function of you wanted to update an existing setting in nwipe.conf, i.e
     *
     * nwipe_conf_update_setting( "PDF_Certificate.PDF_Enable", "ENABLED" )
     *
     * It is NOT used for creating a new group or setting name.
     */

    /* -------------------------------------------------------------
     * Write the field to nwipe's config file /etc/nwipe/nwipe.conf
     */
    if( ( setting = config_lookup( &nwipe_cfg, group_name_setting_name ) ) )
    {
        config_setting_set_string( setting, setting_value );
    }
    else
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "Can't find group.setting_name %s in %s", group_name_setting_name, nwipe_config_file );
        return 1;
    }

    /* Write the new configuration to nwipe.conf
     */
    if( !config_write_file( &nwipe_cfg, nwipe_config_file ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write %s to %s", group_name_setting_name, nwipe_config_file );
        return 2;
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO,
                   "Updated %s with value %s in %s",
                   group_name_setting_name,
                   setting_value,
                   nwipe_config_file );
    }

    return 0;

}  // end nwipe_conf_update_setting()

int nwipe_conf_read_setting( char* group_name_setting_name, const char** setting_value )
{
    /* This function returns a setting value from nwipe's configuration file nwipe.conf
     * when provided with a groupname.settingname string.
     *
     * Example:
     * const char ** pReturnString;
     * nwipe_conf_read_setting( "PDF_Certificate", "PDF_Enable", pReturnString );
     */

    /* Separate group_name_setting_name i.e "PDF_Certificate.PDF_Enable" string
     * into two separate strings by replacing the period with a NULL.
     */

    int return_status;
    int length = strlen( group_name_setting_name );

    char* group_name = calloc( length, sizeof( char ) );
    char* setting_name = calloc( length, sizeof( char ) );

    int idx = 0;

    while( group_name_setting_name[idx] != 0 && group_name_setting_name[idx] != '.' )
    {
        idx++;
    }
    // Copy the group name from the combined input string
    memcpy( group_name, group_name_setting_name, idx );
    group_name[idx] = '\0';  // Null-terminate group_name

    // Copy the setting name from the combined input string
    strcpy( setting_name, &group_name_setting_name[idx + 1] );

    if( !( setting = config_lookup( &nwipe_cfg, group_name ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Can't find group name %s.%s in %s", group_name, setting_name, nwipe_config_file );
        return_status = -1;
    }
    else
    {
        /* Retrieve data from nwipe.conf */
        if( CONFIG_TRUE == config_setting_lookup_string( setting, setting_name, setting_value ) )
        {
            return_status = 0; /* Success */
        }
        else
        {
            nwipe_log(
                NWIPE_LOG_ERROR, "Can't find setting_name %s.%s in %s", group_name, setting_name, nwipe_config_file );
            return_status = -2;
        }
    }

    free( group_name );
    free( setting_name );
    return ( return_status );

}  // end nwipe_conf_read_setting()

int nwipe_conf_populate( char* path, char* value )
{
    /* This function will check that a path containing a group or multiple groups that lead to a setting all exist,
     * if they don't exist, the group/s, settings and associated value are created.
     *
     * The path, a string consisting of one or more groups separted by a period symbol
     * '.' with the final element in the path being the setting name. For instance the path might be
     * PDF_Certificate.PDF_Enable. Where PDF_Certificate is the group name and PDF_Enable is the setting name.
     * If one or other don't exist then they will be created.
     *
     * An arbitary depth of four groups are allowed for nwipe's configuration file, although we only currently, as of
     * October 2023 use a depth of one group. The number of groups can be increased in the future if required by
     * changing the definition MAX_GROUP_DEPTH in conf.h
     */

    char* path_copy;
    char* path_build;

    char* group_start[MAX_GROUP_DEPTH + 2];  // A pointer to the start of each group string
    char* setting_start;

    int idx;  // General index
    int group_count;  // Counts the number of groups in the path

    /* Initialise the pointers */
    memset( group_start, 0, MAX_GROUP_DEPTH + 2 );
    memset( &setting_start, 0, 1 );

    /* Allocate enough memory for a copy of the path and initialise to zero */
    path_copy = calloc( strlen( path ) + 1, sizeof( char ) );
    path_build = calloc( strlen( path ) + 1, sizeof( char ) );

    /* Duplicate the path */
    strcpy( path_copy, path );

    /* Create individual strings by replacing the period '.' with NULL, counting the number of groups. */
    idx = 0;
    group_count = 0;

    /* pointer to first group */
    group_start[group_count] = path_copy;

    while( *( path_copy + idx ) != 0 )
    {
        if( group_count > MAX_GROUP_DEPTH )
        {
            nwipe_log( NWIPE_LOG_ERROR,
                       "Too many groups in path, specified = %i, allowed = %i ",
                       group_count,
                       MAX_GROUP_DEPTH );
            return 1;
        }

        if( *( path_copy + idx ) == '.' )
        {
            *( path_copy + idx ) = 0;
            group_count++;
            group_start[group_count] = path_copy + idx + 1;
        }
        idx++;
    }

    /* setting_start points to a string that represents the setting to be created */
    setting_start = group_start[group_count];

    /* Remove the last entry from group_start as that would be the setting and not a group */
    group_start[group_count] = 0;

    if( group_count == 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "No groups specified in path, i.e. no period . separator found." );
        return 2;
    }

    /**
     * Now determine whether the group/s in the path exist, if not create them.
     */

    idx = 0;
    while( group_start[idx] != 0 )
    {
        strcat( path_build, group_start[idx] );

        if( !( group = config_lookup( &nwipe_cfg, path_build ) ) )
        {
            if( idx == 0 )
            {
                group = config_setting_add( root, path_build, CONFIG_TYPE_GROUP );
                previous_group = group;
            }
            else
            {
                group = config_setting_add( previous_group, group_start[idx], CONFIG_TYPE_GROUP );
                previous_group = group;
            }
            if( group )
            {
                nwipe_log( NWIPE_LOG_INFO, "Created group [%s] in %s", path_build, nwipe_config_file );
            }
            else
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to create group [%s] in %s", path_build, nwipe_config_file );
            }
        }
        else
        {
            previous_group = group;
        }

        idx++;

        if( group_start[idx] != 0 )
        {
            strcat( path_build, "." );
        }
    }

    /**
     * And finally determine whether the setting already exists, if not create it and assign the value to it
     */

    /* Does the full path exist ? i.e AAA.BBB */
    if( ( group = config_lookup( &nwipe_cfg, path_build ) ) )
    {
        /* Does the path and setting exist? AAA.BBB.SETTING_NAME */
        if( !( setting = config_lookup( &nwipe_cfg, path ) ) )
        {
            /* Add the new SETTING_NAME */
            if( ( setting = config_setting_add( group, setting_start, CONFIG_TYPE_STRING ) ) )
            {
                nwipe_log( NWIPE_LOG_INFO, "Created setting name %s in %s", path, nwipe_config_file );
            }
            else
            {
                nwipe_log(
                    NWIPE_LOG_ERROR, "Failed to create setting name %s in %s", setting_start, nwipe_config_file );
            }

            if( config_setting_set_string( setting, value ) == CONFIG_TRUE )
            {
                nwipe_log( NWIPE_LOG_INFO, "Set value for %s in %s to %s", path, nwipe_config_file, value );
            }
            else
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to set value for %s in %s to %s", path, nwipe_config_file, value );
            }
        }
        else
        {
            if( nwipe_options.verbose )
            {
                nwipe_log( NWIPE_LOG_INFO, "Setting already exists [%s] in %s", path, nwipe_config_file );
            }
        }
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "Couldn't find constructed path [%s] in %s", path_build, nwipe_config_file );
    }

    free( path_copy );
    free( path_build );

    return 0;
}

void nwipe_conf_close()
{
    config_destroy( &nwipe_cfg );
}
