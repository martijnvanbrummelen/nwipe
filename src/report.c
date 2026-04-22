#include "report.h"

#include <stdio.h>
#include <string.h>

#include "hpa_dco.h"

static void nwipe_report_format_quantity( u64 qty, char* result, size_t result_size )
{
    if( result == NULL || result_size == 0 )
    {
        return;
    }

    memset( result, 0, result_size );

    if( qty >= INT64_C( 10000000000000 ) )
    {
        snprintf( result, result_size, "%4llu TB", qty / INT64_C( 1000000000000 ) );
    }
    else if( qty >= INT64_C( 10000000000 ) )
    {
        snprintf( result, result_size, "%4llu GB", qty / INT64_C( 1000000000 ) );
    }
    else if( qty >= INT64_C( 10000000 ) )
    {
        snprintf( result, result_size, "%4llu MB", qty / INT64_C( 1000000 ) );
    }
    else if( qty >= INT64_C( 10000 ) )
    {
        snprintf( result, result_size, "%4llu KB", qty / INT64_C( 1000 ) );
    }
    else
    {
        snprintf( result, result_size, "%4llu B", qty );
    }
}

static void nwipe_report_convert_seconds( u64 total_seconds, int* hours, int* minutes, int* seconds )
{
    int h = 0;
    int m = 0;
    int s = 0;

    if( total_seconds % 60 )
    {
        m = total_seconds / 60;
        s = total_seconds - ( m * 60 );
    }
    else
    {
        m = total_seconds / 60;
    }

    if( m > 59 )
    {
        h = m / 60;
        if( m % 60 )
        {
            m = m - ( h * 60 );
        }
        else
        {
            m = 0;
        }
    }

    if( hours != NULL )
    {
        *hours = h;
    }
    if( minutes != NULL )
    {
        *minutes = m;
    }
    if( seconds != NULL )
    {
        *seconds = s;
    }
}

static void nwipe_report_format_percent( char* output_str, size_t output_size, double value )
{
    char percent_str[64] = "";
    size_t input_idx = 0;
    size_t output_idx = 0;
    int decimal_digits = 0;

    if( output_str == NULL || output_size == 0 )
    {
        return;
    }

    output_str[0] = '\0';
    snprintf( percent_str, sizeof( percent_str ), "%5.32lf", value );

    while( percent_str[input_idx] != '\0' && output_idx + 1 < output_size )
    {
        output_str[output_idx++] = percent_str[input_idx];
        if( percent_str[input_idx++] == '.' )
        {
            while( percent_str[input_idx] != '\0' && decimal_digits < 2 && output_idx + 1 < output_size )
            {
                output_str[output_idx++] = percent_str[input_idx++];
                decimal_digits++;
            }
            break;
        }
    }

    output_str[output_idx] = '\0';
}

static void nwipe_report_sanitize_component( char* value, char replacement_char )
{
    size_t i = 0;

    if( value == NULL )
    {
        return;
    }

    while( value[i] != '\0' )
    {
        if( value[i] < '0' || ( value[i] > '9' && value[i] < 'A' ) || ( value[i] > 'Z' && value[i] < 'a' )
            || value[i] > 'z' )
        {
            value[i] = replacement_char;
        }
        i++;
    }
}

static u64 nwipe_report_expected_size( const nwipe_context_t* ctx )
{
    if( ctx->device_type == NWIPE_DEVICE_NVME || ctx->device_type == NWIPE_DEVICE_VIRT
        || ctx->HPA_status == HPA_NOT_APPLICABLE )
    {
        return ctx->device_size;
    }

    return ctx->Calculated_real_max_size_in_bytes;
}

void nwipe_report_build_summary( const nwipe_context_t* ctx, int user_abort, time_t now, nwipe_report_summary_t* out )
{
    if( ctx == NULL || out == NULL )
    {
        return;
    }

    memset( out, 0, sizeof( *out ) );

    if( ctx->pass_errors != 0 || ctx->verify_errors != 0 || ctx->fsyncdata_errors != 0 )
    {
        snprintf( out->exclamation_flag, sizeof( out->exclamation_flag ), "!" );
        snprintf( out->display_status, sizeof( out->display_status ), "-FAILED-" );
        snprintf( out->status_text, sizeof( out->status_text ), "FAILED" );
    }
    else if( ctx->wipe_status == 0 )
    {
        snprintf( out->exclamation_flag, sizeof( out->exclamation_flag ), " " );
        snprintf( out->display_status, sizeof( out->display_status ), " Erased " );
        snprintf( out->status_text, sizeof( out->status_text ), "ERASED" );
    }
    else if( ctx->wipe_status == 1 && user_abort == 1 )
    {
        snprintf( out->exclamation_flag, sizeof( out->exclamation_flag ), "!" );
        snprintf( out->display_status, sizeof( out->display_status ), "UABORTED" );
        snprintf( out->status_text, sizeof( out->status_text ), "ABORTED" );
    }
    else
    {
        snprintf( out->exclamation_flag, sizeof( out->exclamation_flag ), " " );
        snprintf( out->display_status, sizeof( out->display_status ), "INSANITY" );
        snprintf( out->status_text, sizeof( out->status_text ), "INSANITY" );
    }

    nwipe_report_format_quantity( ctx->throughput, out->throughput_txt, sizeof( out->throughput_txt ) );

    if( ctx->end_time != 0 )
    {
        out->effective_end_time = ctx->end_time;
    }
    else if( ctx->start_time != 0 )
    {
        out->effective_end_time = now;
    }

    if( ctx->start_time != 0 && out->effective_end_time != 0 )
    {
        out->duration = difftime( out->effective_end_time, ctx->start_time );
    }

    out->total_duration_seconds = (u64) out->duration;
    nwipe_report_convert_seconds( out->total_duration_seconds, &out->hours, &out->minutes, &out->seconds );
    snprintf(
        out->duration_str, sizeof( out->duration_str ), "%02i:%02i:%02i", out->hours, out->minutes, out->seconds );

    out->expected_size = nwipe_report_expected_size( ctx );
    if( out->expected_size > 0 )
    {
        nwipe_report_format_percent( out->percent_erased,
                                     sizeof( out->percent_erased ),
                                     ( (double) ctx->bytes_erased / out->expected_size ) * 100.0 );
    }
    else
    {
        snprintf( out->percent_erased, sizeof( out->percent_erased ), "N/A" );
    }
}

void nwipe_report_apply_summary( nwipe_context_t* ctx, const nwipe_report_summary_t* summary )
{
    if( ctx == NULL || summary == NULL )
    {
        return;
    }

    ctx->duration = summary->duration;
    snprintf( ctx->duration_str, sizeof( ctx->duration_str ), "%s", summary->duration_str );
    snprintf( ctx->throughput_txt, sizeof( ctx->throughput_txt ), "%s", summary->throughput_txt );
    snprintf( ctx->wipe_status_txt, sizeof( ctx->wipe_status_txt ), "%s", summary->status_text );

    if( ctx->end_time == 0 && summary->effective_end_time != 0 )
    {
        ctx->end_time = summary->effective_end_time;
    }
}

void nwipe_report_build_pdf_filename( const nwipe_context_t* ctx, const char* report_path, char* out, size_t out_size )
{
    char end_time_text[50] = "Unknown";
    char model[128] = "Unknown";
    char serial[NWIPE_SERIALNUMBER_LENGTH + 1] = "Unknown";
    char device_name[DEVICE_NAME_MAX_SIZE] = "device";
    struct tm* tm_info;
    time_t timestamp;

    if( out == NULL || out_size == 0 )
    {
        return;
    }

    out[0] = '\0';

    if( ctx == NULL )
    {
        return;
    }

    timestamp = ctx->end_time != 0 ? ctx->end_time : ctx->start_time;
    if( timestamp != 0 )
    {
        tm_info = localtime( &timestamp );
        if( tm_info != NULL )
        {
            snprintf( end_time_text,
                      sizeof( end_time_text ),
                      "%i/%02i/%02i %02i:%02i:%02i",
                      1900 + tm_info->tm_year,
                      1 + tm_info->tm_mon,
                      tm_info->tm_mday,
                      tm_info->tm_hour,
                      tm_info->tm_min,
                      tm_info->tm_sec );
        }
    }

    if( ctx->device_model != NULL && ctx->device_model[0] != '\0' )
    {
        snprintf( model, sizeof( model ), "%s", ctx->device_model );
    }
    if( ctx->device_serial_no[0] != '\0' )
    {
        snprintf( serial, sizeof( serial ), "%s", ctx->device_serial_no );
    }
    if( ctx->device_name_terse[0] != '\0' )
    {
        snprintf( device_name, sizeof( device_name ), "%s", ctx->device_name_terse );
    }

    nwipe_report_sanitize_component( end_time_text, '-' );
    nwipe_report_sanitize_component( model, '_' );
    nwipe_report_sanitize_component( serial, '_' );
    nwipe_report_sanitize_component( device_name, '_' );

    snprintf( out,
              out_size,
              "%s/nwipe_report_%s_Model_%s_Serial_%s_device_%s.pdf",
              report_path != NULL ? report_path : ".",
              end_time_text,
              model,
              serial,
              device_name );
}
