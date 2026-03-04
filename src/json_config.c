#include "json_config.h"

#include <ctype.h>
#include <errno.h>

#include <libconfig.h>

#include "nwipe.h"
#include "cJSON.h"
#include "conf.h"
#include "context.h"
#include "logging.h"
#include "method.h"
#include "prng.h"
#include "options.h"

static char* nwipe_read_text_file( const char* path )
{
    FILE* fp;
    long length;
    size_t bytes_read;
    char* buffer;

    fp = fopen( path, "rb" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Unable to open config file %s: %s", path, strerror( errno ) );
        return NULL;
    }

    if( fseek( fp, 0, SEEK_END ) != 0 )
    {
        fclose( fp );
        return NULL;
    }

    length = ftell( fp );
    if( length < 0 )
    {
        fclose( fp );
        return NULL;
    }

    if( fseek( fp, 0, SEEK_SET ) != 0 )
    {
        fclose( fp );
        return NULL;
    }

    buffer = calloc( (size_t) length + 1, sizeof( char ) );
    if( buffer == NULL )
    {
        fclose( fp );
        return NULL;
    }

    bytes_read = fread( buffer, 1, (size_t) length, fp );
    fclose( fp );

    if( bytes_read != (size_t) length )
    {
        free( buffer );
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static int nwipe_json_config_looks_like_json( const char* path, const char* content )
{
    const char* extension = strrchr( path, '.' );

    if( extension != NULL && strcmp( extension, ".json" ) == 0 )
    {
        return 1;
    }

    while( *content != '\0' )
    {
        if( !isspace( (unsigned char) *content ) )
        {
            return ( *content == '{' || *content == '[' );
        }
        content++;
    }

    return 0;
}

static int nwipe_apply_string_setting( const cJSON* object, const char* key, const char* config_path )
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive( object, key );

    if( item == NULL )
    {
        return 0;
    }

    if( !cJSON_IsString( item ) || item->valuestring == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "JSON config key '%s' must be a string.", key );
        return -1;
    }

    return nwipe_conf_set_setting( config_path, item->valuestring, 0 );
}

static int nwipe_apply_boolean_setting( const cJSON* object, const char* key, const char* option_key )
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive( object, key );

    if( item == NULL )
    {
        return 0;
    }

    if( !cJSON_IsBool( item ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "JSON config key '%s' must be a boolean.", key );
        return -1;
    }

    return nwipe_options_apply_config_boolean( option_key, cJSON_IsTrue( item ) );
}

static int nwipe_apply_options_object( const cJSON* object )
{
    const cJSON* item;

    if( object == NULL )
    {
        return 0;
    }

    if( !cJSON_IsObject( object ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "JSON config section must be an object." );
        return -1;
    }

    cJSON_ArrayForEach( item, object )
    {
        if( item->string == NULL )
        {
            continue;
        }

        if( cJSON_IsString( item ) && item->valuestring != NULL )
        {
            if( nwipe_options_apply_config_string( item->string, item->valuestring ) != 0 )
            {
                nwipe_log( NWIPE_LOG_ERROR,
                           "Invalid string value '%s' for config key '%s'.",
                           item->valuestring,
                           item->string );
                return -1;
            }
            continue;
        }

        if( cJSON_IsBool( item ) )
        {
            if( nwipe_options_apply_config_boolean( item->string, cJSON_IsTrue( item ) ) != 0 )
            {
                nwipe_log( NWIPE_LOG_ERROR, "Invalid boolean value for config key '%s'.", item->string );
                return -1;
            }
            continue;
        }

        if( cJSON_IsNumber( item ) )
        {
            if( (double) ( (long long) item->valuedouble ) == item->valuedouble )
            {
                if( nwipe_options_apply_config_integer( item->string, (long long) item->valuedouble ) != 0 )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "Invalid integer value for config key '%s'.", item->string );
                    return -1;
                }
            }
            else
            {
                if( nwipe_options_apply_config_double( item->string, item->valuedouble ) != 0 )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "Invalid numeric value for config key '%s'.", item->string );
                    return -1;
                }
            }
            continue;
        }

        if( cJSON_IsArray( item ) )
        {
            const cJSON* entry;

            if( strcmp( item->string, "devices" ) != 0 && strcmp( item->string, "exclude" ) != 0 )
            {
                nwipe_log( NWIPE_LOG_ERROR, "Unsupported array config key '%s'.", item->string );
                return -1;
            }

            cJSON_ArrayForEach( entry, item )
            {
                if( !cJSON_IsString( entry ) || entry->valuestring == NULL )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "Config array '%s' must contain only strings.", item->string );
                    return -1;
                }

                if( strcmp( item->string, "devices" ) == 0 )
                {
                    if( nwipe_options_add_config_device( entry->valuestring ) != 0 )
                    {
                        return -1;
                    }
                }
                else if( nwipe_options_add_config_exclude( entry->valuestring ) != 0 )
                {
                    return -1;
                }
            }
            continue;
        }

        nwipe_log( NWIPE_LOG_ERROR, "Unsupported JSON type for config key '%s'.", item->string );
        return -1;
    }

    return 0;
}

static int nwipe_apply_json_config( const char* path, const char* content )
{
    cJSON* root;
    const char* parse_error;
    cJSON* section;

    root = cJSON_Parse( content );
    if( root == NULL )
    {
        parse_error = cJSON_GetErrorPtr();
        nwipe_log( NWIPE_LOG_ERROR,
                   "JSON parse error in %s near: %s",
                   path,
                   parse_error != NULL ? parse_error : "(unknown)" );
        return -1;
    }

    if( !cJSON_IsObject( root ) )
    {
        cJSON_Delete( root );
        nwipe_log( NWIPE_LOG_ERROR, "JSON config %s must contain a top-level object.", path );
        return -1;
    }

    section = cJSON_GetObjectItemCaseSensitive( root, "organization_details" );
    if( section != NULL )
    {
        if( !cJSON_IsObject( section )
            || nwipe_apply_string_setting( section, "business_name", "Organisation_Details.Business_Name" ) != 0
            || nwipe_apply_string_setting( section, "business_address", "Organisation_Details.Business_Address" ) != 0
            || nwipe_apply_string_setting( section, "contact_name", "Organisation_Details.Contact_Name" ) != 0
            || nwipe_apply_string_setting( section, "contact_phone", "Organisation_Details.Contact_Phone" ) != 0
            || nwipe_apply_string_setting( section, "op_tech_name", "Organisation_Details.Op_Tech_Name" ) != 0 )
        {
            cJSON_Delete( root );
            return -1;
        }
    }

    section = cJSON_GetObjectItemCaseSensitive( root, "selected_customer" );
    if( section != NULL )
    {
        if( !cJSON_IsObject( section )
            || nwipe_apply_string_setting( section, "customer_name", "Selected_Customer.Customer_Name" ) != 0
            || nwipe_apply_string_setting( section, "customer_address", "Selected_Customer.Customer_Address" ) != 0
            || nwipe_apply_string_setting( section, "contact_name", "Selected_Customer.Contact_Name" ) != 0
            || nwipe_apply_string_setting( section, "contact_phone", "Selected_Customer.Contact_Phone" ) != 0 )
        {
            cJSON_Delete( root );
            return -1;
        }
    }

    section = cJSON_GetObjectItemCaseSensitive( root, "pdf_certificate" );
    if( section != NULL )
    {
        if( !cJSON_IsObject( section )
            || nwipe_apply_boolean_setting( section, "pdf_enable", "pdf_enable" ) != 0
            || nwipe_apply_boolean_setting( section, "pdf_preview", "pdf_preview" ) != 0
            || nwipe_apply_boolean_setting( section, "pdf_tag", "pdf_tag" ) != 0
            || nwipe_apply_string_setting( section, "user_defined_tag", "PDF_Certificate.User_Defined_Tag" ) != 0 )
        {
            cJSON_Delete( root );
            return -1;
        }
    }

    if( nwipe_apply_options_object( cJSON_GetObjectItemCaseSensitive( root, "options" ) ) != 0 )
    {
        cJSON_Delete( root );
        return -1;
    }

    if( nwipe_apply_options_object( cJSON_GetObjectItemCaseSensitive( root, "reporting" ) ) != 0 )
    {
        cJSON_Delete( root );
        return -1;
    }

    section = cJSON_GetObjectItemCaseSensitive( root, "devices" );
    if( section != NULL )
    {
        cJSON* wrapper = cJSON_CreateObject();
        if( wrapper == NULL )
        {
            cJSON_Delete( root );
            return -1;
        }
        cJSON_AddItemReferenceToObject( wrapper, "devices", section );
        if( nwipe_apply_options_object( wrapper ) != 0 )
        {
            cJSON_Delete( wrapper );
            cJSON_Delete( root );
            return -1;
        }
        cJSON_Delete( wrapper );
    }

    section = cJSON_GetObjectItemCaseSensitive( root, "exclude" );
    if( section != NULL )
    {
        cJSON* wrapper = cJSON_CreateObject();
        if( wrapper == NULL )
        {
            cJSON_Delete( root );
            return -1;
        }
        cJSON_AddItemReferenceToObject( wrapper, "exclude", section );
        if( nwipe_apply_options_object( wrapper ) != 0 )
        {
            cJSON_Delete( wrapper );
            cJSON_Delete( root );
            return -1;
        }
        cJSON_Delete( wrapper );
    }

    cJSON_Delete( root );
    return 0;
}

static int nwipe_apply_legacy_config( const char* path )
{
    config_t legacy_cfg;
    const char* value = NULL;

    config_init( &legacy_cfg );

    if( !config_read_file( &legacy_cfg, path ) )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Legacy config syntax error: %s:%d - %s",
                   config_error_file( &legacy_cfg ),
                   config_error_line( &legacy_cfg ),
                   config_error_text( &legacy_cfg ) );
        config_destroy( &legacy_cfg );
        return -1;
    }

    if( nwipe_conf_read_setting_from( &legacy_cfg, "Organisation_Details.Business_Name", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Organisation_Details.Business_Name", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Organisation_Details.Business_Address", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Organisation_Details.Business_Address", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Organisation_Details.Contact_Name", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Organisation_Details.Contact_Name", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Organisation_Details.Contact_Phone", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Organisation_Details.Contact_Phone", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Organisation_Details.Op_Tech_Name", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Organisation_Details.Op_Tech_Name", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Selected_Customer.Customer_Name", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Selected_Customer.Customer_Name", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Selected_Customer.Customer_Address", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Selected_Customer.Customer_Address", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Selected_Customer.Contact_Name", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Selected_Customer.Contact_Name", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "Selected_Customer.Contact_Phone", &value ) == 0 )
    {
        nwipe_conf_set_setting( "Selected_Customer.Contact_Phone", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "PDF_Certificate.User_Defined_Tag", &value ) == 0 )
    {
        nwipe_conf_set_setting( "PDF_Certificate.User_Defined_Tag", value, 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "PDF_Certificate.PDF_Enable", &value ) == 0 )
    {
        nwipe_conf_set_setting( "PDF_Certificate.PDF_Enable", value, 0 );
        nwipe_options_apply_config_boolean( "pdf_enable", strcmp( value, "ENABLED" ) == 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "PDF_Certificate.PDF_Preview", &value ) == 0 )
    {
        nwipe_conf_set_setting( "PDF_Certificate.PDF_Preview", value, 0 );
        nwipe_options_apply_config_boolean( "pdf_preview", strcmp( value, "ENABLED" ) == 0 );
    }
    if( nwipe_conf_read_setting_from( &legacy_cfg, "PDF_Certificate.PDF_tag", &value ) == 0 )
    {
        nwipe_conf_set_setting( "PDF_Certificate.PDF_tag", value, 0 );
        nwipe_options_apply_config_boolean( "pdf_tag", strcmp( value, "ENABLED" ) == 0 );
    }

    config_destroy( &legacy_cfg );
    return 0;
}

int nwipe_json_config_load_file( const char* path )
{
    char* content;
    int rc;

    content = nwipe_read_text_file( path );
    if( content == NULL )
    {
        return -1;
    }

    if( nwipe_json_config_looks_like_json( path, content ) )
    {
        rc = nwipe_apply_json_config( path, content );
    }
    else
    {
        rc = nwipe_apply_legacy_config( path );
    }

    free( content );
    return rc;
}
