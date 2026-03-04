/*
 * create_json.c: Routines that create the JSON erasure report.
 *
 * Copyright Knogle <https://github.com/Knogle>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 */

#include <errno.h>

#include <libconfig.h>

#include "nwipe.h"
#include "cJSON.h"
#include "conf.h"
#include "context.h"
#include "create_json.h"
#include "logging.h"
#include "method.h"
#include "miscellaneous.h"
#include "options.h"
#include "version.h"

extern config_t nwipe_cfg;
extern char dmidecode_system_serial_number[DMIDECODE_RESULT_LENGTH];
extern char dmidecode_system_uuid[DMIDECODE_RESULT_LENGTH];
extern char dmidecode_baseboard_serial_number[DMIDECODE_RESULT_LENGTH];

static const char* nwipe_report_config_string( const char* path, const char* fallback )
{
    const char* value = NULL;

    if( nwipe_conf_read_setting( (char*) path, &value ) == 0 && value != NULL )
    {
        return value;
    }

    return fallback;
}

static void nwipe_json_add_string( cJSON* object, const char* key, const char* value )
{
    if( value == NULL )
    {
        value = "";
    }

    cJSON_AddStringToObject( object, key, value );
}

static void nwipe_copy_report_value( char* destination, size_t destination_size, const char* source )
{
    if( destination_size == 0 )
    {
        return;
    }

    strncpy( destination, source, destination_size - 1 );
    destination[destination_size - 1] = '\0';
}

static void nwipe_build_report_path( const nwipe_context_t* c, char* filepath, size_t filepath_size )
{
    char end_time_text[64];
    char model[128];
    char serial[128];
    char device[128];
    time_t report_time = c->end_time != 0 ? c->end_time : time( NULL );
    struct tm* tm_value = localtime( &report_time );

    if( tm_value != NULL )
    {
        strftime( end_time_text, sizeof( end_time_text ), "%Y-%m-%d_%H-%M-%S", tm_value );
    }
    else
    {
        nwipe_copy_report_value( end_time_text, sizeof( end_time_text ), "unknown_time" );
    }

    nwipe_copy_report_value( model, sizeof( model ), c->device_model != NULL ? c->device_model : "unknown_model" );
    nwipe_copy_report_value(
        serial, sizeof( serial ), c->device_serial_no[0] ? c->device_serial_no : "unknown_serial" );
    nwipe_copy_report_value(
        device, sizeof( device ), c->device_name_terse[0] ? c->device_name_terse : "unknown_device" );

    replace_non_alphanumeric( end_time_text, '-' );
    replace_non_alphanumeric( model, '_' );
    replace_non_alphanumeric( serial, '_' );
    replace_non_alphanumeric( device, '_' );

    snprintf( filepath,
              filepath_size,
              "%s/nwipe_report_%s_Model_%s_Serial_%s_device_%s.json",
              nwipe_options_report_directory(),
              end_time_text,
              model,
              serial,
              device );
}

static void nwipe_json_add_timestamp( cJSON* object, const char* name, time_t timestamp )
{
    char text[64];
    struct tm* tm_value;

    cJSON_AddNumberToObject( object, name, (double) timestamp );

    if( timestamp == 0 )
    {
        return;
    }

    tm_value = localtime( &timestamp );
    if( tm_value == NULL )
    {
        return;
    }

    if( strftime( text, sizeof( text ), "%Y-%m-%dT%H:%M:%S%z", tm_value ) > 0 )
    {
        char key[64];
        snprintf( key, sizeof( key ), "%s_text", name );
        cJSON_AddStringToObject( object, key, text );
    }
}

int create_json( nwipe_context_t* c )
{
    cJSON* root = NULL;
    cJSON* metadata = NULL;
    cJSON* organization = NULL;
    cJSON* customer = NULL;
    cJSON* system = NULL;
    cJSON* device = NULL;
    cJSON* wipe = NULL;
    cJSON* errors = NULL;
    char* json_text = NULL;
    FILE* fp = NULL;
    char filepath[PATHNAME_MAX];
    time_t generated_at;
    int success;

    if( !nwipe_options.JSON_enable )
    {
        return 0;
    }

    nwipe_build_report_path( c, filepath, sizeof( filepath ) );
    generated_at = c->end_time != 0 ? c->end_time : time( NULL );
    success = ( c->wipe_status == 0 && c->pass_errors == 0 && c->verify_errors == 0 && c->fsyncdata_errors == 0 );

    root = cJSON_CreateObject();
    if( root == NULL )
    {
        return -1;
    }

    cJSON_AddStringToObject( root, "report_schema", "org.nwipe.report.v1" );
    cJSON_AddStringToObject( root, "report_type", "disk_erase_report" );

    metadata = cJSON_AddObjectToObject( root, "report_metadata" );
    organization = cJSON_AddObjectToObject( root, "organization_details" );
    customer = cJSON_AddObjectToObject( root, "selected_customer" );
    system = cJSON_AddObjectToObject( root, "system" );
    device = cJSON_AddObjectToObject( root, "device" );
    wipe = cJSON_AddObjectToObject( root, "wipe" );
    errors = cJSON_AddObjectToObject( wipe, "errors" );

    if( metadata == NULL || organization == NULL || customer == NULL || system == NULL || device == NULL
        || wipe == NULL || errors == NULL )
    {
        cJSON_Delete( root );
        return -1;
    }

    nwipe_json_add_timestamp( metadata, "generated_at", generated_at );
    nwipe_json_add_string( metadata, "software", "nwipe" );
    nwipe_json_add_string( metadata, "version", version_string );
    nwipe_json_add_string( metadata, "output_path", filepath );

    nwipe_json_add_string(
        organization, "business_name", nwipe_report_config_string( "Organisation_Details.Business_Name", "" ) );
    nwipe_json_add_string(
        organization, "business_address", nwipe_report_config_string( "Organisation_Details.Business_Address", "" ) );
    nwipe_json_add_string(
        organization, "contact_name", nwipe_report_config_string( "Organisation_Details.Contact_Name", "" ) );
    nwipe_json_add_string(
        organization, "contact_phone", nwipe_report_config_string( "Organisation_Details.Contact_Phone", "" ) );
    nwipe_json_add_string(
        organization, "op_tech_name", nwipe_report_config_string( "Organisation_Details.Op_Tech_Name", "" ) );

    nwipe_json_add_string(
        customer, "customer_name", nwipe_report_config_string( "Selected_Customer.Customer_Name", "" ) );
    nwipe_json_add_string(
        customer, "customer_address", nwipe_report_config_string( "Selected_Customer.Customer_Address", "" ) );
    nwipe_json_add_string(
        customer, "contact_name", nwipe_report_config_string( "Selected_Customer.Contact_Name", "" ) );
    nwipe_json_add_string(
        customer, "contact_phone", nwipe_report_config_string( "Selected_Customer.Contact_Phone", "" ) );

    nwipe_json_add_string( system, "system_serial_number", dmidecode_system_serial_number );
    nwipe_json_add_string( system, "system_uuid", dmidecode_system_uuid );
    nwipe_json_add_string( system, "baseboard_serial_number", dmidecode_baseboard_serial_number );
    nwipe_json_add_string( system, "user_defined_tag", nwipe_report_config_string( "PDF_Certificate.User_Defined_Tag", "" ) );

    nwipe_json_add_string( device, "name", c->device_name != NULL ? c->device_name : "" );
    nwipe_json_add_string( device, "terse_name", c->device_name_terse );
    nwipe_json_add_string( device, "model", c->device_model != NULL ? c->device_model : "" );
    nwipe_json_add_string( device, "serial_number", c->device_serial_no );
    nwipe_json_add_string( device, "uuid", c->device_UUID );
    nwipe_json_add_string( device, "size_text", c->device_size_text != NULL ? c->device_size_text : "" );
    nwipe_json_add_string(
        device, "real_size_text", c->Calculated_real_max_size_in_bytes_text[0] ? c->Calculated_real_max_size_in_bytes_text : "" );
    cJSON_AddNumberToObject( device, "size_bytes", (double) c->device_size );
    cJSON_AddNumberToObject( device, "real_size_bytes", (double) c->Calculated_real_max_size_in_bytes );
    cJSON_AddNumberToObject( device, "sector_size", c->device_sector_size );
    cJSON_AddNumberToObject( device, "physical_sector_size", c->device_phys_sector_size );
    nwipe_json_add_string( device, "type", c->device_type_str );

    cJSON_AddBoolToObject( wipe, "successful", success ? 1 : 0 );
    nwipe_json_add_string( wipe, "status", success ? "success" : "failure" );
    nwipe_json_add_string( wipe, "status_text", c->wipe_status_txt );
    nwipe_json_add_string( wipe, "method", nwipe_method_label( nwipe_options.method ) );
    nwipe_json_add_string( wipe, "prng", nwipe_options.prng != NULL ? nwipe_options.prng->label : "" );
    nwipe_json_add_string( wipe, "verify", nwipe_verify_label( nwipe_options.verify ) );
    cJSON_AddNumberToObject( wipe, "rounds_requested", nwipe_options.rounds );
    cJSON_AddNumberToObject( wipe, "rounds_completed", c->round_working );
    cJSON_AddNumberToObject( wipe, "throughput_bytes_per_second", (double) c->throughput );
    nwipe_json_add_string( wipe, "throughput_text", c->throughput_txt );
    nwipe_json_add_timestamp( wipe, "start_time", c->start_time );
    nwipe_json_add_timestamp( wipe, "end_time", c->end_time );
    cJSON_AddNumberToObject( wipe, "duration_seconds", c->duration );
    nwipe_json_add_string( wipe, "duration_text", c->duration_str );

    cJSON_AddNumberToObject( errors, "pass", (double) c->pass_errors );
    cJSON_AddNumberToObject( errors, "verify", (double) c->verify_errors );
    cJSON_AddNumberToObject( errors, "round", (double) c->round_errors );
    cJSON_AddNumberToObject( errors, "fsyncdata", (double) c->fsyncdata_errors );

    json_text = cJSON_Print( root );
    if( json_text == NULL )
    {
        cJSON_Delete( root );
        return -1;
    }

    fp = fopen( filepath, "w" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to open JSON report %s: %s", filepath, strerror( errno ) );
        cJSON_free( json_text );
        cJSON_Delete( root );
        return -1;
    }

    if( fputs( json_text, fp ) == EOF || fputc( '\n', fp ) == EOF )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write JSON report %s", filepath );
        fclose( fp );
        cJSON_free( json_text );
        cJSON_Delete( root );
        return -1;
    }

    fclose( fp );
    cJSON_free( json_text );
    cJSON_Delete( root );

    nwipe_log( NWIPE_LOG_INFO, "Created JSON report %s", filepath );
    return 0;
}
