/*
 *  create_pdf_graph.c: create a speed profile graph on the PDF erasure certificates
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "create_pdf.h"
#include "PDFGen/pdfgen.h"
#include "logging.h"

// --- Global Page Geometry Setup (A4 Portrait Layout) ---
const float PAGE_WIDTH = 595.27f;  // A4 width in points
const float PAGE_HEIGHT = 841.89f;  // A4 height in points

#define DATA_POINTS 400  // Fixed sizing requirement for dataset array length

extern struct pdf_doc* pdf;
extern struct pdf_object* page;

size_t
create_pdf_speed_profile_page( nwipe_misc_thread_data_t* d, size_t pdf_type, size_t* page_number, nwipe_context_t* c )
{
    extern struct pdf_object** pdf_page_array;

    // --- Global Page Geometry Setup (A4 Portrait Layout) ---
    const float PAGE_WIDTH = 595.27f;  // A4 width in points
    const float PAGE_HEIGHT = 841.89f;  // A4 height in point
    char speed_profile_page_title[100];
    char buffer[512];
    int y;

    // Add a new page for the graph
    page = pdf_append_page_and_update_index( pdf, ++( *page_number ) );

    // Fill Page Background
    const uint32_t COLOR_BG = 0xFFFFFF;
    pdf_add_filled_rectangle( pdf, NULL, 0, 0, PAGE_WIDTH, PAGE_HEIGHT, 0, COLOR_BG, COLOR_BG );

    /* Create the header and footer for the speed profile page */
    snprintf( speed_profile_page_title, sizeof( speed_profile_page_title ), "Page %zu - Speed Profile", *page_number );
    pdf_header_footer_text( d, c, speed_profile_page_title, pdf_type, PDF_PAGE_SMART_DATA );

    /* Display the appropriate status icon (green tick, red cross, tick with exclamation) for
     * the single disk PDF. For multi disc PDFs the status icon is written upon completion
     * of the entire PDF within the create_system_multidisc_pdf() function.
     */
    if( pdf_type == PDF_TYPE_SINGLE_DISC )
    {
        pdf_display_status_icon( PDF_TYPE_SINGLE_DISC, NULL );
    }

    /* For multidisc the smart data starts slighlty lower to accomodate
     * the erasure status ellipse & text
     */
    if( pdf_type == PDF_TYPE_SINGLE_DISC )
    {
        y = TOP_OF_TEXT_WINDOW_Y;  // top row of page
    }
    else
    {
        y = START_OF_SMART_DATA_TEXT_Y_MULTIDISC;  // start of smart data
    }

    if( pdf_type == PDF_TYPE_MULTI_DISC && y == START_OF_SMART_DATA_TEXT_Y_MULTIDISC )
    {
        /* Write the erasure status of this drive at the top of each smart data page
         * for system multi disc pdf only
         */
        pdf_set_font( pdf, "Helvetica-Bold" );
        snprintf( buffer, sizeof( buffer ), "Erasure Status of this disk: S/N %s", c->device_serial_no );
        pdf_add_text( pdf, NULL, buffer, 10, 160, TOP_OF_TEXT_WINDOW_Y - 3, PDF_BLACK );
        pdf_add_text_status_of_erasure( LEFT_MARGIN_SMART_DATA + 25,
                                        TOP_OF_TEXT_WINDOW_Y - 4,
                                        LEFT_MARGIN_SMART_DATA + 50,
                                        TOP_OF_TEXT_WINDOW_Y + 1,
                                        45,
                                        10,
                                        0,
                                        c );
        pdf_set_font( pdf, "Courier" );
    }

    // Place the graph above the help text
    float graph_y_start = 340.0f;

    /* ======================================================================
     * GRAPH DATA POLISHING: High-Res Fallback for under 10 second wipes
     * If the wipe completed without errors and was under 10 seconds in
     * duration, this will produce a flat line graph specifically for
     * loop devices. We still see the speed but without the plot
     * details.
     * ====================================================================== */
    if( c->pass_errors == 0 && c->verify_errors == 0 && c->fsyncdata_errors == 0 && c->wipe_status == 0
        && c->exact_duration < 10.0 && c->exact_duration > 0.0 && c->round_done > 0 )
    {
        int max_allowed_bucket = 399;

        // Fallback for sub 10 second wipes, to avoid using buckets

        // Calculate actual burst speed straight from our protected high-res duration
        double final_speed = 0.0;

        if( c->exact_duration > 0.0 )
        {
            final_speed = (double) c->round_done / c->exact_duration;
        }
        else
        {
            // Absolute microsecond safety floor to prevent division-by-zero
            final_speed = 0.0;
        }

        // Plot a flat line across the graph
        for( int b = 0; b <= max_allowed_bucket; b++ )
        {
            c->max_throughput[b] = final_speed;
            c->min_throughput[b] = final_speed;
        }
    }
    /* ====================================================================== */

    // Generate Graph Vector Elements
    generate_graph_pdf( graph_y_start,
                        c->min_throughput,
                        c->max_throughput,
                        DATA_POINTS,
                        "",
                        "Erasure Completion - %",
                        "Speed",
                        100.0f );

    // --- Interpretation Help & Disclaimer Text Block ---
    const float PLOT_X = LEFT_MARGIN_TEXT + 20;

    // 1. Interpretation Content Lines
    const char* help_title = "Interpretation Guide:";
    const char* help_body1 = "Monitor the speed vectors closely for unusual anomalies or performance bottlenecks.";
    const char* help_body2 = "Look for sharp, sudden downward spikes where operational speed differs substantially";
    const char* help_body3 = "from typical baseline values. These spikes can indicate a drive slowing down over";
    const char* help_body4 = "specificphysical sectors, signaling potential disk failure. Such degradation may not";
    const char* help_body5 = "trigger standard drive or I/O errors, meaning it won't always be obvious in SMART data.";
    const char* help_body6 = "The SMART health status may even report the drive as 'Good' when,in fact, this graph ";
    const char* help_body7 = "highlights a localized failure on the platter surface.";

    // 2. Disclaimer Content Lines
    const char* disc_title = "Disclaimer:";
    const char* disc_body1 = "If your system is suffering from controller bottlenecks or CPU constraints due to too";
    const char* disc_body2 = "many discs being simultaneously erased due to a non-optimal setup, the disk profile may";
    const char* disc_body3 = "not represent the true speed profile. If in doubt, test using ShredOS with only the";
    const char* disc_body4 = "single disk you suspect is behaving oddly in regards to its speed profile.";
    const char* disc_body5 = "See https://github.com/martijnvanbrummelen/nwipe/discussions/775 for examples of speed";
    const char* disc_body6 = "profiles for both good and failing discs.";

    // Render Interpretation Text Vector Elements (Y positions spaced down progressively)
    size_t PLOT_Y = 260;
    size_t PLOT_Y_DIFF = 11;
    pdf_set_font( pdf, "Courier" );
    pdf_add_text( pdf, NULL, help_title, 10.0f, PLOT_X, PLOT_Y, PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body1, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body2, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body3, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body4, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body5, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body6, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );
    pdf_add_text( pdf, NULL, help_body7, 8.0f, PLOT_X, ( PLOT_Y -= PLOT_Y_DIFF ), PDF_BLACK );

    // Render Disclaimer Text Vector Elements
    PLOT_Y -= 20;
    pdf_add_text( pdf, NULL, disc_title, 10.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    pdf_add_text( pdf, NULL, disc_body1, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    pdf_add_text( pdf, NULL, disc_body2, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    pdf_add_text( pdf, NULL, disc_body3, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    pdf_add_text( pdf, NULL, disc_body4, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    PLOT_Y -= PLOT_Y_DIFF;
    pdf_add_text( pdf, NULL, disc_body5, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );
    pdf_add_text( pdf, NULL, disc_body6, 8.0f, PLOT_X, PLOT_Y -= PLOT_Y_DIFF, PDF_BLACK );

    return 0;
}

/**
 * Generates a stylized dual-line graph on an existing PDF document page with peak annotations,
 * an overall duration average line, and an expanded left margin padding for clean axis label layout.
 */
int generate_graph_pdf( float plot_y_start,
                        const float* min_values,
                        const float* max_values,
                        int data_count,
                        const char* title,
                        const char* x_label,
                        const char* y_label,
                        float x_scale_max )
{
    // page excluded from NULL check as NULL is a valid value
    if( !pdf || !min_values || !max_values || data_count < 2 )
    {
        return -1;
    }

    // --- Color Palette (Hex format: 0xRRGGBB) ---
    const uint32_t COLOR_PLOT_BG = 0xF8F9FA;
    const uint32_t COLOR_AXIS = 0x212529;
    const uint32_t COLOR_GRID = 0xCED4DA;
    const uint32_t COLOR_LINE_MAX = 0x1F77B4;
    const uint32_t COLOR_LINE_MIN = 0xFF7F0E;
    const uint32_t COLOR_LINE_AVG = 0x2CA02C;
    const uint32_t COLOR_TEXT_MUTED = 0x6C757D;

    // --- Graph Geometry Setup ---
    const float PLOT_W = 400.0f;
    const float PLOT_X = 115.0f;  // Opened up extra space on the left margin
    const float PLOT_H = 200.0f;
    const float PLOT_Y = plot_y_start;

    // Scan intergrity of dataset, data must be >= 0 and less than 150GB (150000000000.0)

    const double MAX_LIMIT = 150000000000.0;
    const double MIN_LIMIT = 0.0;
    bool is_valid = true;

    for( int i = 0; i < 400; i++ )
    {
        // Check max_throughput bounds
        if( max_values[i] < MIN_LIMIT || max_values[i] > MAX_LIMIT )
        {
            nwipe_log( NWIPE_LOG_DEBUG,
                       "Error: Speed Profile data set max_values[%d] value (%f) out of bounds.",
                       max_values[i] );
            is_valid = false;
        }

        // Check min_throughput bounds
        if( min_values[i] < MIN_LIMIT || min_values[i] > MAX_LIMIT )
        {
            nwipe_log( NWIPE_LOG_DEBUG,
                       "Error: Speed Profile data set min_values[%d] value (%f) out of bounds.",
                       min_values[i] );
            is_valid = false;
        }

        if( is_valid == false )
        {
            nwipe_log( NWIPE_LOG_ERROR, "Graph min/max dataset is out of bounds. < 0.0 or > 150000000000.0 (150GB)" );
            return -1;
        }
    }

    // 1. Scan dataset to find absolute min, max, and active average sum
    float abs_min = min_values[0];
    int min_idx = 0;
    float abs_max = max_values[0];
    int max_idx = 0;
    float running_avg_sum = 0.0f;
    int active_count = 0;

    for( int i = 0; i < data_count; i++ )
    {
        if( min_values[i] < abs_min )
        {
            abs_min = min_values[i];
            min_idx = i;
        }
        if( max_values[i] > abs_max )
        {
            abs_max = max_values[i];
            max_idx = i;
        }

        // Only factor in buckets where an active speed was recorded
        if( max_values[i] > 0.0f )
        {
            running_avg_sum += ( min_values[i] + max_values[i] ) / 2.0f;
            active_count++;
        }
    }

    // Calculate average using only active slots; guard against division by zero
    float overall_duration_average = 0.0f;
    if( active_count > 0 )
    {
        overall_duration_average = running_avg_sum / (float) active_count;
    }
    else if( data_count > 0 )
    {
        // Fallback if the entire array is filled with zeroes
        overall_duration_average = ( min_values[0] + max_values[0] ) / 2.0f;
    }

    // 2. AUTOMATIC UNIT & SCALE ENGINE
    float divisor = 1.0f;
    const char* unit = "bytes/s";

    if( abs_max >= 1000000000.0f )
    {
        divisor = 1000000000.0f;
        unit = "GB/s";
    }
    else if( abs_max >= 1000000.0f )
    {
        divisor = 1000000.0f;
        unit = "MB/s";
    }
    else if( abs_max >= 1000.0f )
    {
        divisor = 1000.0f;
        unit = "KB/s";
    }

    float max_in_unit = abs_max / divisor;

    // Dynamic Step & Ceiling Selector Engine
    float step = 1.0f;
    if( max_in_unit <= 5.0f )
        step = 1.0f;
    else if( max_in_unit <= 10.0f )
        step = 2.0f;
    else if( max_in_unit <= 25.0f )
        step = 5.0f;
    else if( max_in_unit <= 50.0f )
        step = 10.0f;
    else if( max_in_unit <= 100.0f )
        step = 20.0f;
    else if( max_in_unit <= 250.0f )
        step = 50.0f;
    else
        step = 100.0f;

    float y_scale_max_unit = step;
    while( y_scale_max_unit < max_in_unit * 1.20f )
    {
        y_scale_max_unit += step;
    }

    int y_grid_divisions = (int) ( y_scale_max_unit / step );
    float y_scale_max = y_scale_max_unit * divisor;

    // Fill Plot Area Background
    pdf_add_filled_rectangle( pdf, page, PLOT_X, PLOT_Y, PLOT_W, PLOT_H, 0, COLOR_PLOT_BG, COLOR_PLOT_BG );

    // --- Grid Lines & Axis Ticks Setup ---
    const float dotted_pattern[] = { 3.0f, 3.0f };
    const int pattern_len = 2;

    // 1. Draw Dynamic Horizontal Grid Lines & Custom Y-Axis Labels
    for( int i = 0; i <= y_grid_divisions; i++ )
    {
        float ratio = (float) i / (float) y_grid_divisions;
        float curr_y = PLOT_Y + ( ratio * PLOT_H );
        float val_y = ratio * y_scale_max;

        if( i > 0 )
        {
            pdf_add_line_pattern( pdf,
                                  page,
                                  PLOT_X,
                                  curr_y,
                                  PLOT_X + PLOT_W,
                                  curr_y,
                                  0.75f,
                                  COLOR_GRID,
                                  dotted_pattern,
                                  pattern_len,
                                  0.0f );
        }

        char label_buf[64];
        snprintf( label_buf, sizeof( label_buf ), "%.0f%s", val_y / divisor, unit );

        float text_width = 0;
        pdf_get_font_text_width( pdf, "Helvetica", label_buf, 9.0f, &text_width );
        pdf_add_text( pdf, page, label_buf, 9.0f, PLOT_X - text_width - 8.0f, curr_y - 3.0f, COLOR_TEXT_MUTED );
    }

    // 2. Draw Vertical Grid Lines & X-Axis Labels (Fixed at 5 horizontal blocks)
    const int X_GRID_DIVISIONS = 5;

    for( int i = 0; i <= X_GRID_DIVISIONS; i++ )
    {
        float ratio = (float) i / X_GRID_DIVISIONS;
        float curr_x = PLOT_X + ( ratio * PLOT_W );
        float val_x = ratio * x_scale_max;

        if( i > 0 && i < X_GRID_DIVISIONS )
        {
            pdf_add_line_pattern( pdf,
                                  page,
                                  curr_x,
                                  PLOT_Y,
                                  curr_x,
                                  PLOT_Y + PLOT_H,
                                  0.75f,
                                  COLOR_GRID,
                                  dotted_pattern,
                                  pattern_len,
                                  0.0f );
        }

        char label_buf[32];
        snprintf( label_buf, sizeof( label_buf ), "%.0f%%", val_x );
        float text_width = 0;
        pdf_get_font_text_width( pdf, "Helvetica", label_buf, 9.0f, &text_width );
        pdf_add_text( pdf, page, label_buf, 9.0f, curr_x - ( text_width / 2.0f ), PLOT_Y - 15.0f, COLOR_TEXT_MUTED );
    }

    // --- Draw Axis Outer Frame Lines ---
    pdf_add_line( pdf, page, PLOT_X, PLOT_Y, PLOT_X + PLOT_W, PLOT_Y, 1.2f, COLOR_AXIS );
    pdf_add_line( pdf, page, PLOT_X, PLOT_Y, PLOT_X, PLOT_Y + PLOT_H, 1.2f, COLOR_AXIS );

    // --- Plot Line 1: Minimum Speed Data Vector ---
    for( int i = 0; i < data_count - 1; i++ )
    {
        float ratio_x1 = (float) i / ( data_count - 1 );
        float ratio_x2 = (float) ( i + 1 ) / ( data_count - 1 );
        float x1 = PLOT_X + ( ratio_x1 * PLOT_W );
        float x2 = PLOT_X + ( ratio_x2 * PLOT_W );

        float val_y1 = min_values[i] > y_scale_max ? y_scale_max : min_values[i];
        float val_y2 = min_values[i + 1] > y_scale_max ? y_scale_max : min_values[i + 1];
        float y1 = PLOT_Y + ( ( val_y1 / y_scale_max ) * PLOT_H );
        float y2 = PLOT_Y + ( ( val_y2 / y_scale_max ) * PLOT_H );

        pdf_add_line( pdf, page, x1, y1, x2, y2, 1.5f, COLOR_LINE_MIN );
    }

    // --- Plot Line 2: Maximum Speed Data Vector ---
    for( int i = 0; i < data_count - 1; i++ )
    {
        float ratio_x1 = (float) i / ( data_count - 1 );
        float ratio_x2 = (float) ( i + 1 ) / ( data_count - 1 );
        float x1 = PLOT_X + ( ratio_x1 * PLOT_W );
        float x2 = PLOT_X + ( ratio_x2 * PLOT_W );

        float val_y1 = max_values[i] > y_scale_max ? y_scale_max : max_values[i];
        float val_y2 = max_values[i + 1] > y_scale_max ? y_scale_max : max_values[i + 1];
        float y1 = PLOT_Y + ( ( val_y1 / y_scale_max ) * PLOT_H );
        float y2 = PLOT_Y + ( ( val_y2 / y_scale_max ) * PLOT_H );

        pdf_add_line( pdf, page, x1, y1, x2, y2, 1.5f, COLOR_LINE_MAX );
    }

    // --- Plot Line 3: Overall Duration Average Line ---
    float avg_line_y = PLOT_Y + ( ( overall_duration_average / y_scale_max ) * PLOT_H );
    const float avg_dash_pattern[] = { 6.0f, 4.0f };
    pdf_add_line_pattern(
        pdf, page, PLOT_X, avg_line_y, PLOT_X + PLOT_W, avg_line_y, 1.5f, COLOR_LINE_AVG, avg_dash_pattern, 2, 0.0f );

    // --- Fixed Placement Label for the Average Line with Legend Dashes ---
    char avg_label_str[128];
    snprintf( avg_label_str, sizeof( avg_label_str ), "avg %.1f%s", overall_duration_average / divisor, unit );
    float avg_lbl_w = 0;
    pdf_get_font_text_width( pdf, "Helvetica-Bold", avg_label_str, 8.0f, &avg_lbl_w );

    float legend_dash_w = 26.0f;
    float legend_gap = 5.0f;
    float total_avg_lbl_w = legend_dash_w + legend_gap + avg_lbl_w;

    float avg_lbl_x = ( PLOT_X + PLOT_W ) - total_avg_lbl_w - 8.0f;
    float avg_lbl_y = PLOT_Y + PLOT_H - 14.0f;

    pdf_add_line_pattern( pdf,
                          page,
                          avg_lbl_x,
                          avg_lbl_y + 3.0f,
                          avg_lbl_x + legend_dash_w,
                          avg_lbl_y + 3.0f,
                          1.5f,
                          COLOR_LINE_AVG,
                          avg_dash_pattern,
                          2,
                          0.0f );
    pdf_add_text( pdf, page, avg_label_str, 8.0f, avg_lbl_x + legend_dash_w + legend_gap, avg_lbl_y, COLOR_LINE_AVG );

    // --- Data Point Peak Labeling Engine ---

    // 1. Label the absolute minimum point
    char min_label_str[128];
    snprintf( min_label_str, sizeof( min_label_str ), "min %.1f%s", abs_min / divisor, unit );
    float min_lbl_w = 0;
    pdf_get_font_text_width( pdf, "Helvetica-Bold", min_label_str, 8.0f, &min_lbl_w );

    float min_pt_x = PLOT_X + ( ( (float) min_idx / ( data_count - 1 ) ) * PLOT_W );
    float min_pt_y = PLOT_Y + ( ( ( abs_min > y_scale_max ? y_scale_max : abs_min ) / y_scale_max ) * PLOT_H );

    float min_lbl_x = min_pt_x - ( min_lbl_w / 2.0f );
    if( min_lbl_x < PLOT_X )
        min_lbl_x = PLOT_X + 4.0f;
    if( min_lbl_x + min_lbl_w > PLOT_X + PLOT_W )
        min_lbl_x = ( PLOT_X + PLOT_W ) - min_lbl_w - 4.0f;
    pdf_add_text( pdf, page, min_label_str, 8.0f, min_lbl_x, min_pt_y - 12.0f, COLOR_LINE_MIN );

    // 2. Label the absolute maximum point
    char max_label_str[128];
    snprintf( max_label_str, sizeof( max_label_str ), "max %.1f%s", abs_max / divisor, unit );
    float max_lbl_w = 0;
    pdf_get_font_text_width( pdf, "Helvetica-Bold", max_label_str, 8.0f, &max_lbl_w );

    float max_pt_x = PLOT_X + ( ( (float) max_idx / ( data_count - 1 ) ) * PLOT_W );
    float max_pt_y = PLOT_Y + ( ( ( abs_max > y_scale_max ? y_scale_max : abs_max ) / y_scale_max ) * PLOT_H );

    float max_lbl_x = max_pt_x - ( max_lbl_w / 2.0f );
    if( max_lbl_x < PLOT_X )
        max_lbl_x = PLOT_X + 4.0f;
    if( max_lbl_x + max_lbl_w > PLOT_X + PLOT_W )
        max_lbl_x = ( PLOT_X + PLOT_W ) - max_lbl_w - 4.0f;
    pdf_add_text( pdf, page, max_label_str, 8.0f, max_lbl_x, max_pt_y + 5.0f, COLOR_LINE_MAX );

    // --- Global Annotations (Labels & Headings) ---
    float title_width = 0;
    pdf_get_font_text_width( pdf, "Helvetica-Bold", title, 16.0f, &title_width );
    pdf_add_text(
        pdf, page, title, 16.0f, PLOT_X + ( PLOT_W - title_width ) / 2.0f, PLOT_Y + PLOT_H + 30.0f, COLOR_AXIS );

    float x_lbl_width = 0;
    pdf_get_font_text_width( pdf, "Helvetica", x_label, 11.0f, &x_lbl_width );
    pdf_add_text( pdf, page, x_label, 11.0f, PLOT_X + ( PLOT_W - x_lbl_width ) / 2.0f, PLOT_Y - 42.0f, COLOR_AXIS );

    float y_lbl_width = 0;
    pdf_get_font_text_width( pdf, "Helvetica", y_label, 11.0f, &y_lbl_width );
    pdf_add_text_rotate(
        pdf, page, y_label, 11.0f, PLOT_X - 60.0f, PLOT_Y + ( PLOT_H - y_lbl_width ) / 2.0f, 1.57f, COLOR_AXIS );

    return 0;
}
