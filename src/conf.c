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
#include "conf.h"

config_t nwipe_cfg;
config_setting_t *nwipe_conf_setting, *group_organisation, *group_selected_customer, *root, *group, *setting;
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
    root = config_root_setting( &nwipe_cfg );
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

            /* Populate with some basic structure */

            /* Add information about the business performing the erasure  */
            group_organisation = config_setting_add( root, "Organisation_Details", CONFIG_TYPE_GROUP );

            setting = config_setting_add( group_organisation, "Business_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (BN)" );

            setting = config_setting_add( group_organisation, "Business_Address", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (BA)" );

            setting = config_setting_add( group_organisation, "Contact_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (BCN)" );

            setting = config_setting_add( group_organisation, "Contact_Phone", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (BCP)" );

            setting = config_setting_add( group_organisation, "Op_Tech_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (OTN)" );

            /* Add PDF Certificate/Report settings */
            group_organisation = config_setting_add( root, "PDF_Certificate", CONFIG_TYPE_GROUP );

            setting = config_setting_add( group_organisation, "PDF_Enable", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "ENABLED" );

            setting = config_setting_add( group_organisation, "PDF_Preview", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "DISABLED" );

            /**
             * The currently selected customer that will be printed on the report
             */

            group_selected_customer = config_setting_add( root, "Selected_Customer", CONFIG_TYPE_GROUP );

            setting = config_setting_add( group_selected_customer, "Customer_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (CN)" );

            setting = config_setting_add( group_selected_customer, "Customer_Address", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (CA)" );

            setting = config_setting_add( group_selected_customer, "Contact_Name", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (CN)" );

            setting = config_setting_add( group_selected_customer, "Contact_Phone", CONFIG_TYPE_STRING );
            config_setting_set_string( setting, "Not Applicable (CP)" );

            /* Write out the new configuration. */
            if( !config_write_file( &nwipe_cfg, nwipe_config_file ) )
            {
                nwipe_log( NWIPE_LOG_ERROR, "Failed to write basic config to %s", nwipe_config_file );
            }
            else
            {
                nwipe_log( NWIPE_LOG_INFO, "Populated %s with basic config", nwipe_config_file );
            }
            fclose( fp_config );
        }
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
        }
        fclose( fp_customers );
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
    /* You would call this function if you wanted to read a settings value in nwipe.conf, i.e
     *
     * const char ** pReturnString;
     * nwipe_conf_read_setting( "PDF_Certificate", "PDF_Enable", pReturnString );
     *
     */

    /* Separate group_name_setting_name i.e "PDF_Certificate.PDF_Enable" string
     * into two separate strings by replacing the period with a NULL.
     */

    int return_status;
    int length = strlen( group_name_setting_name );

    char* group_name = malloc( length );
    char* setting_name = malloc( length );

    int idx = 0;

    while( group_name_setting_name[idx] != 0 && group_name_setting_name[idx] != '.' )
    {
        if( group_name_setting_name[idx] == '.' )
        {
            break;
        }
        idx++;
    }
    memcpy( group_name, group_name_setting_name, idx );
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
            nwipe_log( NWIPE_LOG_INFO, "setting_value = %s", *setting_value );
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

void nwipe_conf_close()
{
    config_destroy( &nwipe_cfg );
}
