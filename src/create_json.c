/*
 * create_json.c: Routines that create the JSON erasure report
 *
 * Copyright Knogle <https://github.com/Knogle>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <libconfig.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "options.h"
#include "logging.h"
#include "create_json.h"
#include "version.h"

extern config_t nwipe_cfg;  // Access to global config for organization details

/* Helper to escape JSON strings to prevent format errors */
void json_print_string( FILE* fp, const char* key, const char* value, int is_last )
{
    fprintf( fp, "\"%s\": \"", key );
    if( value )
    {
        for( const char* p = value; *p; p++ )
        {
            switch( *p )
            {
                case '"':
                    fprintf( fp, "\\\"" );
                    break;
                case '\\':
                    fprintf( fp, "\\\\" );
                    break;
                case '\b':
                    fprintf( fp, "\\b" );
                    break;
                case '\f':
                    fprintf( fp, "\\f" );
                    break;
                case '\n':
                    fprintf( fp, "\\n" );
                    break;
                case '\r':
                    fprintf( fp, "\\r" );
                    break;
                case '\t':
                    fprintf( fp, "\\t" );
                    break;
                default:
                    fputc( *p, fp );
                    break;
            }
        }
    }
    fprintf( fp, "\"%s\n", is_last ? "" : "," );
}

int create_json( nwipe_context_t* c )
{
    char filename[PATHNAME_MAX];
    char filepath[PATHNAME_MAX];
    time_t now;
    struct tm* t;
    char time_str[64];
    FILE* fp;

    // Check if JSON generation is enabled
    if( !nwipe_options.JSON_enable )
    {
        return 0;
    }

    // Prepare timestamp for filename
    now = time( NULL );
    t = localtime( &now );
    strftime( time_str, sizeof( time_str ), "%Y-%m-%d_%H-%M-%S", t );

    // Determine filename based on serial number or fallback
    const char* identifier = c->device_serial_no[0] ? c->device_serial_no : "Unknown";
    snprintf( filename, sizeof( filename ), "nwipe_report_%s_%s.json", identifier, time_str );

    // Determine full output path
    if( nwipe_options.PDFreportpath[0] != '\0' )
    {
        snprintf( filepath, sizeof( filepath ), "%s/%s", nwipe_options.PDFreportpath, filename );
    }
    else
    {
        snprintf( filepath, sizeof( filepath ), "%s", filename );
    }

    nwipe_log( NWIPE_LOG_INFO, "Writing JSON report to %s", filepath );

    fp = fopen( filepath, "w" );
    if( !fp )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to open file %s for writing JSON report.", filepath );
        return -1;
    }

    // Start JSON Object
    fprintf( fp, "{\n" );

    // -- Metadata Section --
    fprintf( fp, "  \"report_metadata\": {\n" );
    json_print_string( fp, "generated_at", time_str, 0 );
    json_print_string( fp, "software", "nwipe", 0 );
    json_print_string( fp, "version", version_string, 1 );
    fprintf( fp, "  },\n" );

    // -- System / Organization Info --
    fprintf( fp, "  \"organization\": {\n" );
    // Try to read from config, similar to create_pdf.c
    const char* org_name = "Unknown";
    config_setting_t* setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( setting )
    {
        config_setting_lookup_string( setting, "Business_Name", &org_name );
    }
    json_print_string( fp, "business_name", org_name, 1 );
    fprintf( fp, "  },\n" );

    // -- Device Info --
    fprintf( fp, "  \"device\": {\n" );
    json_print_string( fp, "name", c->device_name, 0 );
    json_print_string( fp, "model", c->device_model, 0 );
    json_print_string( fp, "serial_no", c->device_serial_no, 0 );

    // Numeric size
    fprintf( fp, "    \"size_bytes\": %lld,\n", (long long) c->Calculated_real_max_size_in_bytes );
    // Human readable size
    json_print_string( fp, "size_text", c->device_size_text, 1 );
    fprintf( fp, "  },\n" );

    // -- Wipe Results --
    fprintf( fp, "  \"wipe_result\": {\n" );

    // Status: 0 usually indicates success in nwipe context logic, but check specific flags if available
    // Assuming wipe_status is tracked in context or check pass_errors
    int success = ( c->wipe_status == 0 );
    json_print_string( fp, "status", success ? "success" : "failure", 0 );

    fprintf( fp, "    \"rounds_requested\": %d,\n", nwipe_options.rounds );
    fprintf( fp, "    \"pass_errors\": %llu,\n", (unsigned long long) c->pass_errors );
    fprintf( fp, "    \"verify_errors\": %llu,\n", (unsigned long long) c->verify_errors );

    // Timestamps
    fprintf( fp, "    \"start_time\": %ld,\n", (long) c->start_time );
    fprintf( fp, "    \"end_time\": %ld,\n", (long) c->end_time );

    long duration = (long) ( c->end_time - c->start_time );
    fprintf( fp, "    \"duration_seconds\": %ld\n", duration > 0 ? duration : 0 );

    fprintf( fp, "  }\n" );  // End wipe_result

    // End JSON Object
    fprintf( fp, "}\n" );

    fclose( fp );
    return 0;
}
