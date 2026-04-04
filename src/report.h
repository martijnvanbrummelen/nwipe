#ifndef REPORT_H_
#define REPORT_H_

#include <stddef.h>
#include <time.h>

#include "context.h"

typedef struct
{
    char exclamation_flag[2];
    char display_status[9];
    char status_text[10];
    char throughput_txt[13];
    char duration_str[20];
    char percent_erased[8];
    u64 expected_size;
    u64 total_duration_seconds;
    double duration;
    time_t effective_end_time;
    int hours;
    int minutes;
    int seconds;
} nwipe_report_summary_t;

void nwipe_report_build_summary( const nwipe_context_t* ctx,
                                 int user_abort,
                                 time_t now,
                                 nwipe_report_summary_t* out );
void nwipe_report_apply_summary( nwipe_context_t* ctx, const nwipe_report_summary_t* summary );
void nwipe_report_build_pdf_filename( const nwipe_context_t* ctx,
                                      const char* report_path,
                                      char* out,
                                      size_t out_size );

#endif /* REPORT_H_ */
