#include <assert.h>
#include <string.h>
#include <time.h>

#include "report.h"
#include "hpa_dco.h"

static void test_summary_uses_real_max_expected_size( void )
{
    nwipe_context_t ctx;
    nwipe_report_summary_t summary;

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.device_type = NWIPE_DEVICE_SCSI;
    ctx.HPA_status = HPA_DISABLED;
    ctx.device_size = 100;
    ctx.Calculated_real_max_size_in_bytes = 125;
    ctx.bytes_erased = 100;
    ctx.throughput = 2000000;
    ctx.start_time = 10;
    ctx.end_time = 70;
    ctx.wipe_status = 0;

    nwipe_report_build_summary( &ctx, 0, 90, &summary );

    assert( summary.expected_size == 125 );
    assert( strcmp( summary.status_text, "ERASED" ) == 0 );
    assert( strcmp( summary.percent_erased, "80.00" ) == 0 );
    assert( strcmp( summary.duration_str, "00:01:00" ) == 0 );
}

static void test_summary_uses_device_size_for_nvme( void )
{
    nwipe_context_t ctx;
    nwipe_report_summary_t summary;

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.device_type = NWIPE_DEVICE_NVME;
    ctx.HPA_status = HPA_ENABLED;
    ctx.device_size = 200;
    ctx.Calculated_real_max_size_in_bytes = 500;
    ctx.bytes_erased = 100;
    ctx.wipe_status = 0;

    nwipe_report_build_summary( &ctx, 0, 0, &summary );

    assert( summary.expected_size == 200 );
    assert( strcmp( summary.percent_erased, "50.00" ) == 0 );
}

static void test_summary_marks_aborted_and_apply_updates_context( void )
{
    nwipe_context_t ctx;
    nwipe_report_summary_t summary;

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.start_time = 10;
    ctx.end_time = 0;
    ctx.wipe_status = 1;
    ctx.throughput = 123456789;

    nwipe_report_build_summary( &ctx, 1, 100, &summary );

    assert( strcmp( summary.status_text, "ABORTED" ) == 0 );
    assert( strcmp( summary.display_status, "UABORTED" ) == 0 );
    assert( summary.effective_end_time == 100 );
    assert( strcmp( summary.duration_str, "00:01:30" ) == 0 );

    nwipe_report_apply_summary( &ctx, &summary );

    assert( ctx.end_time == 100 );
    assert( strcmp( ctx.wipe_status_txt, "ABORTED" ) == 0 );
    assert( strcmp( ctx.duration_str, "00:01:30" ) == 0 );
}

static void test_summary_marks_failed_on_errors( void )
{
    nwipe_context_t ctx;
    nwipe_report_summary_t summary;

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.pass_errors = 1;
    ctx.wipe_status = 0;

    nwipe_report_build_summary( &ctx, 0, 0, &summary );

    assert( strcmp( summary.exclamation_flag, "!" ) == 0 );
    assert( strcmp( summary.display_status, "-FAILED-" ) == 0 );
    assert( strcmp( summary.status_text, "FAILED" ) == 0 );
}

static void test_pdf_filename_sanitizes_without_mutating_context( void )
{
    nwipe_context_t ctx;
    char model[] = "Loop Model/A";
    char filename[FILENAME_MAX];

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.device_model = model;
    snprintf( ctx.device_serial_no, sizeof( ctx.device_serial_no ), "SER:01 02" );
    snprintf( ctx.device_name_terse, sizeof( ctx.device_name_terse ), "loop0" );
    ctx.end_time = 1704067200; /* 2024-01-01 00:00:00 UTC */

    nwipe_report_build_pdf_filename( &ctx, "/tmp/reports", filename, sizeof( filename ) );

    assert( strcmp( ctx.device_model, "Loop Model/A" ) == 0 );
    assert( strcmp( ctx.device_serial_no, "SER:01 02" ) == 0 );
    assert( strstr( filename, "/tmp/reports/nwipe_report_" ) == filename );
    assert( strstr( filename, "_Model_Loop_Model_A_Serial_SER_01_02_device_loop0.pdf" ) != NULL );
}

int main( void )
{
    test_summary_uses_real_max_expected_size();
    test_summary_uses_device_size_for_nvme();
    test_summary_marks_aborted_and_apply_updates_context();
    test_summary_marks_failed_on_errors();
    test_pdf_filename_sanitizes_without_mutating_context();
    return 0;
}
