/*
 *  create_pdf.c: Common functions that create the PDF erasure certificates
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

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include "stdarg.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "nwipe.h"
#include "context.h"
#include "create_pdf.h"
#include "PDFGen/pdfgen.h"
#include "version.h"
#include "method.h"
#include "embedded_images/shred_db.jpg.h"
#include "embedded_images/tick_erased.jpg.h"
#include "embedded_images/redcross.h"
#include "embedded_images/nwipe_exclamation.jpg.h"
#include "logging.h"
#include "options.h"
#include "prng.h"
#include "hpa_dco.h"
#include "miscellaneous.h"
#include <libconfig.h>
#include "conf.h"

#define text_size_data 10

struct pdf_doc* pdf;
struct pdf_object* page;
struct pdf_object** pdf_page_array;

char model_header[MODEL_HEADER_LENGTH] = ""; /* Model text in the header */
char serial_header[SERIAL_HEADER_LENGTH] = ""; /* Serial number text in the header */
char hostid_header[DMIDECODE_RESULT_LENGTH + 15] = ""; /* host identification, UUID, serial number, system tag */
char barcode[BARCODE_LENGTH] = ""; /* Contents of the barcode, i.e model:serial */
char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
char tag_header[MAX_PDF_TAG_LENGTH];
float height;
float page_width;
size_t status_icon;  // Relevant for single disc PDF only
size_t status_icon_green = FALSE;  // used by multidisc system PDF
size_t status_icon_yellow = FALSE;  // used by multidisc system PDF
size_t status_icon_red = FALSE;  // used by multidisc system PDF

int nwipe_get_smart_data( nwipe_misc_thread_data_t* d, size_t pdf_type, size_t* page_number, nwipe_context_t* c )
{
    extern struct pdf_object** pdf_page_array;

    FILE* fp;

    char* pdata;
    char page_title[50];

    char smartctl_command[] = "smartctl -a %s";
    char smartctl_command2[] = "/sbin/smartctl -a %s";
    char smartctl_command3[] = "/usr/bin/smartctl -a %s";
    char smartctl_command4[] = "/usr/sbin/smartctl -a %s";
    char final_cmd_smartctl[sizeof( smartctl_command3 ) + 256];
    char result[512];
    char buffer[512];
    char smartctl_labels_to_anonymize[][18] = {
        "serial number:", "lu wwn device id:", "logical unit id:", "" /* Don't remove this empty string !, important */
    };

    int idx, idx2, idx3;
    int x, y;
    int set_return_value;

    final_cmd_smartctl[0] = 0;

    /* Determine whether we can access smartctl, required if the PATH environment is not setup ! (Debian sid 'su' as
     * opposed to 'su -' */
    if( system( "which smartctl > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/smartctl > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/smartctl > /dev/null 2>&1" ) )
            {
                if( system( "which /usr/sbin/smartctl > /dev/null 2>&1" ) )
                {
                    nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install smartmontools !" );
                }
                else
                {
                    sprintf( final_cmd_smartctl, smartctl_command4, c->device_name );
                }
            }
            else
            {
                sprintf( final_cmd_smartctl, smartctl_command3, c->device_name );
            }
        }
        else
        {
            sprintf( final_cmd_smartctl, smartctl_command2, c->device_name );
        }
    }
    else
    {
        sprintf( final_cmd_smartctl, smartctl_command, c->device_name );
    }

    if( final_cmd_smartctl[0] != 0 )
    {
        fp = popen( final_cmd_smartctl, "r" );

        if( fp == NULL )
        {
            nwipe_log( NWIPE_LOG_WARNING, "nwipe_get_smart_data(): Failed to create stream to %s", smartctl_command );

            set_return_value = 3;
        }
        else
        {
            x = LEFT_MARGIN_SMART_DATA;  // left side of page

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

            ( *page_number )++;

            /* Create the next page of the report. This shows the drives smart data
             */
            page = pdf_append_page_and_update_index( pdf, *page_number );
            if( page == NULL )
            {
                nwipe_log( NWIPE_LOG_INFO, "Failed to allocate memory when adding new page = %zu", *page_number );
                return -1;
            }

            /* Create the header and footer for page 2, the start of the smart data */
            snprintf( page_title, sizeof( page_title ), "Page %zu - Smart Data", *page_number );
            pdf_header_footer_text( d, c, page_title, pdf_type, PDF_PAGE_SMART_DATA );

            /* Display the appropriate status icon (green tick, red cross, tick with exclamation) for
             * the single disk PDF. For multi disc PDFs the status icon is written upon completion
             * of the entire PDF within the create_system_multidisc_pdf() function.
             */
            if( pdf_type == PDF_TYPE_SINGLE_DISC )
            {
                pdf_display_status_icon( PDF_TYPE_SINGLE_DISC, NULL );
            }

            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                if( pdf_type == PDF_TYPE_MULTI_DISC && y == START_OF_SMART_DATA_TEXT_Y_MULTIDISC )
                {
                    /* Write the erasure status of this drive at the top of each smart data page
                     * for system multi disc pdf only
                     */
                    pdf_set_font( pdf, "Helvetica-Bold" );
                    snprintf( buffer, sizeof( buffer ), "Erasure Status of this disk: S/N %s", c->device_serial_no );
                    pdf_add_text( pdf, NULL, buffer, 10, 160, TOP_OF_TEXT_WINDOW_Y + 2, PDF_BLACK );
                    pdf_add_text_status_of_erasure( LEFT_MARGIN_SMART_DATA + 25,
                                                    TOP_OF_TEXT_WINDOW_Y,
                                                    LEFT_MARGIN_SMART_DATA + 50,
                                                    TOP_OF_TEXT_WINDOW_Y + 5,
                                                    45,
                                                    10,
                                                    0,
                                                    c );
                    pdf_set_font( pdf, "Courier" );
                }

                /* Convert the label, i.e everything before the ':' to lower case, it's required to
                 * convert to lower case as smartctl seems to use inconsistent case when labeling
                 * for serial number, i.e mostly it produces labels "Serial Number:" but occasionally
                 * it produces a label "Serial number:" */

                idx = 0;

                while( result[idx] != 0 && result[idx] != ':' )
                {
                    /* If upper case alpha character, change to lower case */
                    if( result[idx] >= 'A' && result[idx] <= 'Z' )
                    {
                        result[idx] += 32;
                    }
                    idx++;
                }

                if( nwipe_options.quiet == 1 )
                {
                    for( idx2 = 0; idx2 < 3; idx2++ )
                    {
                        if( strstr( result, &smartctl_labels_to_anonymize[idx2][0] ) )
                        {
                            if( ( pdata = strstr( result, ":" ) ) )
                            {
                                idx3 = 1;
                                while( pdata[idx3] != 0 )
                                {
                                    if( pdata[idx3] != ' ' )
                                    {
                                        pdata[idx3] = 'X';
                                    }
                                    idx3++;
                                }
                            }
                        }
                    }
                }

                pdf_set_font( pdf, "Courier" );
                pdf_add_text( pdf, NULL, result, 8, x, y, PDF_BLACK );
                y -= 9;

                /* Have we reached the bottom of the page yet */
                if( y < 60 )
                {
                    /* Append an extra page */
                    ( *page_number )++;
                    page = pdf_append_page_and_update_index( pdf, *page_number );
                    if( page == NULL )
                    {
                        nwipe_log(
                            NWIPE_LOG_INFO, "Failed to allocate memory when adding new page = %zu", *page_number );
                        return -1;
                    }
                    if( pdf_type == PDF_TYPE_SINGLE_DISC )
                    {
                        y = TOP_OF_TEXT_WINDOW_Y;
                    }
                    else
                    {
                        y = START_OF_SMART_DATA_TEXT_Y_MULTIDISC;
                    }
                    /* create the header and footer for the next page */
                    snprintf( page_title, sizeof( page_title ), "Page %zu - Smart Data", *page_number );
                    pdf_header_footer_text( d, c, page_title, pdf_type, PDF_PAGE_SMART_DATA );
                    /* Display the appropriate status icon (green tick, red cross, tick with exclamation) */
                    pdf_display_status_icon( PDF_TYPE_SINGLE_DISC, NULL );
                }
            }
            set_return_value = 0;
        }
    }
    else
    {
        set_return_value = 1;
    }
    return set_return_value;
}

void pdf_header_footer_text( nwipe_misc_thread_data_t* d,
                             nwipe_context_t* c,
                             char* page_title,
                             size_t pdf_type,
                             size_t pdf_page_type )
{
    const char* user_defined_tag;

    char disk_erasure_report[] = "Disk Erasure Report";
    char system_erasure_report[] = "System Erasure Report";
    char* erasure_report_title;

    /* variables used by libconfig for extracting data from nwipe.conf */
    config_setting_t* setting;
    extern config_t nwipe_cfg;

    pdf_add_text_wrap( pdf, NULL, pdf_footer, 12, 0, 30, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_line( pdf, NULL, 50, 50, 550, 50, 3, PDF_BLACK );  // Footer full width Line
    pdf_add_line( pdf, NULL, 50, 650, 550, 650, 3, PDF_BLACK );  // Header full width Line
    pdf_add_line( pdf, NULL, 175, 734, 425, 734, 3, PDF_BLACK );  // Header Page number, disk model divider line
    pdf_add_image_data( pdf, NULL, 45, 665, 100, 100, bin2c_shred_db_jpg, 27063 );
    pdf_set_font( pdf, "Helvetica-Bold" );

    if( nwipe_options.PDFtag || nwipe_options.PDF_toggle_host_info )
    {
        /* Always display disk info on single disc pdf or if a multi disc PDF,
         * only display on the smart data pages and not on erasure pages.
         */
        if( pdf_type == PDF_TYPE_SINGLE_DISC
            || ( pdf_type == PDF_TYPE_MULTI_DISC && pdf_page_type == PDF_PAGE_SMART_DATA ) )
        {
            snprintf( model_header, sizeof( model_header ), " %s: %s ", "Disk Model", c->device_model );
            pdf_add_text_wrap(
                pdf, NULL, model_header, 11, 0, 718, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
            snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "Disk S/N", c->device_serial_no );
            pdf_add_text_wrap(
                pdf, NULL, serial_header, 11, 0, 703, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
        }

        /* Display host UUID & S/N is host visibility is enabled in PDF or always if system multi-disc PDF */
        if( nwipe_options.PDF_toggle_host_info || pdf_type == PDF_TYPE_MULTI_DISC )
        {
            snprintf(
                hostid_header, sizeof( hostid_header ), " %s: %s ", "System S/N", d->dmidecode_system_serial_number );
            pdf_add_text_wrap(
                pdf, NULL, hostid_header, 11, 0, 688, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
            snprintf( hostid_header, sizeof( hostid_header ), " %s: %s ", "System uuid", d->dmidecode_system_uuid );
            pdf_add_text_wrap(
                pdf, NULL, hostid_header, 11, 0, 673, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
        }

        /* libconfig: Obtain PDF_Certificate.User_Defined_Tag from nwipe.conf */
        setting = config_lookup( &nwipe_cfg, "PDF_Certificate" );

        if( config_setting_lookup_string( setting, "User_Defined_Tag", &user_defined_tag ) )
        {
            if( user_defined_tag[0] != 0 )
            {
                snprintf( tag_header, sizeof( tag_header ), " %s: %s ", "Tag", user_defined_tag );
                pdf_add_text_wrap(
                    pdf, NULL, tag_header, 11, 0, 658, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
            }
        }
        else
        {
            snprintf( tag_header, sizeof( tag_header ), " %s: %s ", "Tag", "libconfig:tag error" );
            pdf_add_text_wrap( pdf, NULL, tag_header, 11, 0, 658, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
        }
    }
    else
    {
        /* Always display disk info on single disc pdf or if a multi disc PDF,
         * only display on the smart data pages and not on erasure pages.
         */
        if( pdf_type == PDF_TYPE_SINGLE_DISC
            || ( pdf_type == PDF_TYPE_MULTI_DISC && pdf_page_type == PDF_PAGE_SMART_DATA ) )
        {
            snprintf( model_header, sizeof( model_header ), " %s: %s ", "Disk Model", c->device_model );
            pdf_add_text_wrap(
                pdf, NULL, model_header, 11, 0, 696, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
            snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "Disk S/N", c->device_serial_no );
            pdf_add_text_wrap(
                pdf, NULL, serial_header, 11, 0, 681, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
        }
    }
    pdf_set_font( pdf, "Helvetica" );

    switch( pdf_type )
    {
        case PDF_TYPE_SINGLE_DISC:
            erasure_report_title = disk_erasure_report;
            break;

        case PDF_TYPE_MULTI_DISC:
            erasure_report_title = system_erasure_report;
            break;

        default:
            erasure_report_title = "Sanity: Unknown PDF type";
    }

    pdf_add_text_wrap(
        pdf, NULL, erasure_report_title, 24, 0, 765, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    snprintf( barcode, sizeof( barcode ), "%s:%s", c->device_model, c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, page_title, 14, 0, 745, 0, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_barcode( pdf, NULL, PDF_BARCODE_128A, 100, 790, 400, 25, barcode, PDF_BLACK );
}

uint32_t determine_color_for_size_apparent( nwipe_context_t* c )
{
    /* -----------------
     * Size (Apparent)
     * Determines whether the text that shows the apparent disc size should be red or green.
     * The text is red if hidden sectors are detected, green if no hidden sectors or the device
     * doesn't support HPA such as NVMe
     */

    if( ( c->device_size == c->Calculated_real_max_size_in_bytes ) || c->device_type == NWIPE_DEVICE_NVME
        || c->device_type == NWIPE_DEVICE_VIRT || c->HPA_status == HPA_NOT_APPLICABLE || c->HPA_status != HPA_UNKNOWN )
    {
        return PDF_DARK_GREEN;
    }
    else
    {
        return PDF_RED;
    }
}

void pdf_add_text_size_real( float xoff, float yoff, nwipe_context_t* c )
{
    extern struct pdf_doc* pdf;
    extern struct pdf_object* page;

    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */

    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( device_size, sizeof( device_size ), "%s,%llib", c->device_size_text, c->device_size );
        pdf_add_text( pdf, NULL, device_size, text_size_data, xoff, yoff, PDF_DARK_GREEN );
    }
    else
    {
        /* If the calculared real max size as determined from HPA/DCO and libata data is larger than
         * or equal to the apparent device size then display that value in green.
         */
        if( c->Calculated_real_max_size_in_bytes >= c->device_size )
        {
            /* displays the real max size of the disc from the DCO displayed in Green */
            snprintf( device_size,
                      sizeof( device_size ),
                      "%s,%llib",
                      c->Calculated_real_max_size_in_bytes_text,
                      c->Calculated_real_max_size_in_bytes );
            pdf_add_text( pdf, NULL, device_size, text_size_data, xoff, yoff, PDF_DARK_GREEN );
        }
        else
        {
            /* If there is no real max size either because the drive or adapter doesn't support it */
            if( c->HPA_status == HPA_UNKNOWN )
            {
                snprintf( device_size, sizeof( device_size ), "Unknown" );
                pdf_add_text( pdf, NULL, device_size, text_size_data, xoff, yoff, PDF_RED );
            }
            else
            {
                /* we are already here because c->DCO_reported_real_max_size < 1 so if HPA enabled then use the
                 * value we determine from whether HPA set, HPA real exist and if not assume libata's value*/
                if( c->HPA_status == HPA_ENABLED )
                {
                    snprintf( device_size,
                              sizeof( device_size ),
                              "%s, %lli bytes",
                              c->device_size_text,
                              c->Calculated_real_max_size_in_bytes );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, xoff, yoff, PDF_DARK_GREEN );
                }
                else
                {
                    /* Sanity check, should never get here! */
                    snprintf( device_size, sizeof( device_size ), "Sanity: HPA_status = %i", c->HPA_status );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, xoff, yoff, PDF_RED );
                }
            }
        }
    }
}

/*******************
 * Bytes Erased
 */
void pdf_add_text_bytes_erased( float xoff, float yoff, nwipe_context_t* c )
{
    char bytes_erased[50] = "";
    char bytes_percent_str[7] = "";

    /* Bytes erased is not applicable when user only requested a verify */
    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero )
    {
        snprintf( bytes_erased, sizeof( bytes_erased ), "Not applicable to method" );
        pdf_add_text( pdf, NULL, bytes_erased, text_size_data, xoff, yoff, PDF_BLACK );
    }
    else
    {
        if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
            || c->HPA_status == HPA_NOT_APPLICABLE )
        {
            convert_double_to_string( bytes_percent_str,
                                      (double) ( (double) c->bytes_erased / (double) c->device_size ) * 100 );

            snprintf( bytes_erased, sizeof( bytes_erased ), "%lli, (%s%%)", c->bytes_erased, bytes_percent_str );

            if( c->bytes_erased == c->device_size )
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, xoff, yoff, PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, xoff, yoff, PDF_RED );
            }
        }
        else
        {

            convert_double_to_string(
                bytes_percent_str,
                (double) ( (double) c->bytes_erased / (double) c->Calculated_real_max_size_in_bytes ) * 100 );

            snprintf( bytes_erased, sizeof( bytes_erased ), "%lli, (%s%%)", c->bytes_erased, bytes_percent_str );

            if( c->bytes_erased == c->Calculated_real_max_size_in_bytes )
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, xoff, yoff, PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, xoff, yoff, PDF_RED );
            }
        }
    }
}

/*******************
 * PRNG type
 */

void pdf_add_text_prng_type( float xoff, float yoff, uint32_t colour )
{
    char prng_type[50] = ""; /* type of prng, twister, isaac, isaac64 */

    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;
    extern nwipe_prng_t nwipe_splitmix64_prng;
    extern nwipe_prng_t nwipe_aes_ctr_prng;
    extern nwipe_prng_t nwipe_chacha20_prng;

    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero
        || nwipe_options.method == &nwipe_zero || nwipe_options.method == &nwipe_one )
    {
        snprintf( prng_type, sizeof( prng_type ), "N/A to method" );
    }
    else
    {
        if( nwipe_options.prng == &nwipe_twister )
            snprintf( prng_type, sizeof( prng_type ), "Twister" );
        else if( nwipe_options.prng == &nwipe_isaac )
            snprintf( prng_type, sizeof( prng_type ), "Isaac (CSPRNG)" );
        else if( nwipe_options.prng == &nwipe_isaac64 )
            snprintf( prng_type, sizeof( prng_type ), "Isaac64 (CSPRNG)" );
        else if( nwipe_options.prng == &nwipe_add_lagg_fibonacci_prng )
            snprintf( prng_type, sizeof( prng_type ), "Fibonacci" );
        else if( nwipe_options.prng == &nwipe_xoroshiro256_prng )
            snprintf( prng_type, sizeof( prng_type ), "XORoshiro256" );
        else if( nwipe_options.prng == &nwipe_splitmix64_prng )
            snprintf( prng_type, sizeof( prng_type ), "SplitMix64" );
        else if( nwipe_options.prng == &nwipe_chacha20_prng )
            snprintf( prng_type, sizeof( prng_type ), "ChaCha20 (CSPRNG)" );
        else if( nwipe_options.prng == &nwipe_aes_ctr_prng )
            snprintf( prng_type, sizeof( prng_type ), "AES-CTR (CSPRNG)" );
        else
            snprintf( prng_type, sizeof( prng_type ), "Unknown" );
    }
    pdf_add_text( pdf, NULL, prng_type, text_size_data, xoff, yoff, colour );
}

/*******************
 * Status of erasure
 */

void pdf_add_text_status_of_erasure( float text_xoff,
                                     float text_yoff,
                                     float ellipse_xoff,
                                     float ellipse_yoff,
                                     float ellipse_xradius,
                                     float ellipse_yradius,
                                     float angle,
                                     nwipe_context_t* c )
{
    if( !strcmp( c->wipe_status_txt, "ERASED" )
        && ( c->HPA_status == HPA_DISABLED || c->HPA_status == HPA_NOT_APPLICABLE || c->device_type == NWIPE_DEVICE_NVME
             || c->device_type == NWIPE_DEVICE_VIRT ) )
    {
        pdf_add_text_rotate( pdf, NULL, c->wipe_status_txt, 12, text_xoff, text_yoff, angle, PDF_DARK_GREEN );
        pdf_add_ellipse( pdf,
                         NULL,
                         ellipse_xoff,
                         ellipse_yoff,
                         ellipse_xradius,
                         ellipse_yradius,
                         2,
                         PDF_DARK_GREEN,
                         PDF_TRANSPARENT );

        status_icon = STATUS_ICON_GREEN_TICK;  // used later on page 2
        status_icon_green = TRUE;  // Used later for multidisc system PDF
    }
    else
    {
        if( !strcmp( c->wipe_status_txt, "ERASED" )
            && ( c->HPA_status == HPA_ENABLED || c->HPA_status == HPA_UNKNOWN ) )
        {
            pdf_add_ellipse(
                pdf, NULL, ellipse_xoff, ellipse_yoff, ellipse_xradius, ellipse_yradius, 2, PDF_RED, PDF_BLACK );
            pdf_add_text_rotate( pdf, NULL, c->wipe_status_txt, 12, text_xoff, text_yoff, angle, PDF_YELLOW );

            status_icon = STATUS_ICON_YELLOW_EXCLAMATION;  // used later on page 2 for single disk PDF
            status_icon_yellow = TRUE;  // Used later for multidisc system PDF
        }
        else
        {
            if( !strcmp( c->wipe_status_txt, "FAILED" ) )
            {
                /* Re:angle == 0 ? text_xoff + 5 : text_xoff. Required as the text needs to be
                 * shifted left slightly in ellipse due to extra character for 0 degree angle ellipse only */

                pdf_add_text_rotate( pdf,
                                     NULL,
                                     c->wipe_status_txt,
                                     12,
                                     angle == 0 ? text_xoff + 5 : text_xoff,
                                     text_yoff,
                                     angle,
                                     PDF_RED );

                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2 for single disk PDF
                status_icon_red = TRUE;  // Used later for multidisc system PDF
            }
            else
            {
                pdf_add_text_rotate( pdf, NULL, c->wipe_status_txt, 12, text_xoff, text_yoff, angle, PDF_RED );

                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2 for single disk PDF
                status_icon_red = TRUE;  // Used later for multidisc system PDF
            }
            pdf_add_ellipse(
                pdf, NULL, ellipse_xoff, ellipse_yoff, ellipse_xradius, ellipse_yradius, 2, PDF_RED, PDF_TRANSPARENT );
        }
    }
}

void pdf_display_status_icon( size_t pdf_type, void* pp )
{
    /**********************************************************
     * Display the appropriate status icon, top right of PDF
     *
     * The pdf_type represents either single disc or multi-disc
     * pdf. For a single disc pdf the status icon chosen represents
     * the status of only that disc, however for a multidisc pdf
     * the icon chosen should only show a green tick if ALL discs
     * are wiped without error. If any disc fails the icon MUST show
     * a failed status (red cross). Individual disc status is noted
     * next to the specific disc wipe details.
     *
     * The data to determine the status is derived from the
     * pdf_add_text_status_of_erasure() function.
     *
     */

    size_t status_icon_local;
    void* page_local;

    status_icon_local = status_icon;  // Initialised but may be changed by following statements

    if( pdf_type == PDF_TYPE_MULTI_DISC )
    {
        page_local = pp;

        /* On the system PDF the status icon on every page must represent a failure or warning
         * icon if ANY drives failed. It's only a green tick if all drives wiped successfully.'
         */
        if( status_icon_red == TRUE )
        {
            status_icon_local = STATUS_ICON_RED_CROSS;
        }
        else if( status_icon_yellow == TRUE )
        {
            status_icon_local = STATUS_ICON_YELLOW_EXCLAMATION;
        }
        else if( status_icon_green == TRUE )
        {
            status_icon_local = STATUS_ICON_GREEN_TICK;
        }
    }
    else if( pdf_type == PDF_TYPE_SINGLE_DISC )
    {
        page_local = NULL;
        status_icon_local = status_icon;
    }

    switch( status_icon_local )
    {
        case STATUS_ICON_GREEN_TICK:

            /* Display the green tick icon in the header */
            pdf_add_image_data( pdf, page_local, 450, 665, 100, 100, bin2c_te_jpg, 54896 );
            break;

        case STATUS_ICON_YELLOW_EXCLAMATION:

            /* Display the yellow exclamation icon in the header */
            pdf_add_image_data( pdf, page_local, 450, 665, 100, 100, bin2c_nwipe_exclamation_jpg, 65791 );
            break;

        case STATUS_ICON_RED_CROSS:

            // Display the red cross in the header
            pdf_add_image_data( pdf, page_local, 450, 665, 100, 100, bin2c_redcross_jpg, 60331 );
            break;

        default:

            break;
    }
}

void pdf_add_text_blanking( float text_size, float xoff, float yoff )
{
    /******************************************************
     * Final blanking pass if selected, none, zeros or ones
     */

    char blank[10] = ""; /* blanking pass, none, zeros, ones */

    if( nwipe_options.noblank )
    {
        strcpy( blank, "None" );
    }
    else
    {
        strcpy( blank, "Zeros" );
    }
    pdf_add_text( pdf, NULL, blank, text_size, xoff, yoff, PDF_BLACK );
}

void pdf_add_text_verify( float text_size, float xoff, float yoff )
{
    /* ***********************************************************************
     * Create suitable text based on the numeric value of type of verification
     */

    char verify[20] = ""; /* Verify option text */

    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            strcpy( verify, "None" );
            break;

        case NWIPE_VERIFY_LAST:
            strcpy( verify, "Last" );
            break;

        case NWIPE_VERIFY_ALL:
            strcpy( verify, "All" );
            break;
    }
    pdf_add_text( pdf, NULL, verify, text_size, xoff, yoff, PDF_BLACK );
}

void pdf_add_text_rounds( float text_size, float xoff, float yoff, nwipe_context_t* c )
{
    /************************************************
     * rounds - How many times the method is repeated
     */

    char rounds[50] = ""; /* rounds ASCII numeric */

    if( !strcmp( c->wipe_status_txt, "ERASED" ) )
    {
        snprintf( rounds, sizeof( rounds ), "%i/%i", c->round_working, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size, xoff, yoff, PDF_DARK_GREEN );
    }
    else
    {
        snprintf( rounds, sizeof( rounds ), "%i/%i", c->round_working - 1, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size, xoff, yoff, PDF_RED );
    }
}

void pdf_add_text_hpa_size( float text_size, float xoff, float yoff, nwipe_context_t* c )
{
    /*******************
     * Populate HPA size
     */
    char HPA_size_text[50] = "";

    if( c->HPA_status == HPA_ENABLED )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "%lli sectors", c->HPA_sectors );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size, xoff, yoff, PDF_RED );
    }
    else
    {
        if( c->HPA_status == HPA_DISABLED )
        {
            snprintf( HPA_size_text, sizeof( HPA_size_text ), "No hidden sectors" );
            pdf_add_text( pdf, NULL, HPA_size_text, text_size, xoff, yoff, PDF_DARK_GREEN );
        }
        else
        {
            if( c->HPA_status == HPA_NOT_APPLICABLE )
            {
                snprintf( HPA_size_text, sizeof( HPA_size_text ), "Not Applicable" );
                pdf_add_text( pdf, NULL, HPA_size_text, text_size, xoff, yoff, PDF_DARK_GREEN );
            }
            else
            {
                if( c->HPA_status == HPA_UNKNOWN )
                {
                    snprintf( HPA_size_text, sizeof( HPA_size_text ), "Unknown" );
                    pdf_add_text( pdf, NULL, HPA_size_text, text_size, xoff, yoff, PDF_RED );
                }
            }
        }
    }
}

void pdf_add_text_hpa_status( float text_size, float xoff, float yoff, nwipe_context_t* c )
{
    /*********************
     * Populate HPA status (and size if not applicable, NVMe and VIRT)
     */

    char HPA_status_text[50] = "";

    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "Not applicable" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size, xoff, yoff, PDF_DARK_GREEN );
    }
    else
    {
        if( c->HPA_status == HPA_ENABLED )
        {
            snprintf( HPA_status_text, sizeof( HPA_status_text ), "Hidden sectors found!" );
            pdf_add_text( pdf, NULL, HPA_status_text, text_size, xoff, yoff, PDF_RED );
        }
        else
        {
            if( c->HPA_status == HPA_DISABLED )
            {
                snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors" );
                pdf_add_text( pdf, NULL, HPA_status_text, text_size, xoff, yoff, PDF_DARK_GREEN );
            }
            else
            {
                if( c->HPA_status == HPA_UNKNOWN )
                {
                    snprintf( HPA_status_text, sizeof( HPA_status_text ), "Unknown" );
                    pdf_add_text( pdf, NULL, HPA_status_text, text_size, xoff, yoff, PDF_RED );
                }
                else
                {
                    if( c->HPA_status == HPA_NOT_SUPPORTED_BY_DRIVE )
                    {
                        snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors **DDNSHDA" );
                        pdf_add_text( pdf, NULL, HPA_status_text, text_size, xoff, yoff, PDF_DARK_GREEN );
                    }
                }
            }
        }
    }
}

struct pdf_object* pdf_append_page_and_update_index( void* pdf, size_t page_number )
{
    /* We append a new PDF page here and update the page index.
     *
     * The page index, which is an array of pointers to each PDF page is used to
     * write the status icon to each page after all disc info has been written
     * to the pages including smart data. After all discs are processed and we
     * know whether each individual disc was erased or failed then we can write
     * the appropriate status icon to every page on a multidisc system pdf.
     */
    struct pdf_object* page;

    page = pdf_append_page( pdf );

    /* expand page array size by one pointer */
    struct pdf_object** temp = realloc( pdf_page_array, ( page_number + 1 ) * sizeof( struct pdf_object* ) );

    if( temp == NULL )
    {
        fprintf( stderr, "Memory allocation failed!\n" );
        return NULL;
    }

    pdf_page_array = temp;

    /* Append the pdf page pointer to the array */
    pdf_page_array[page_number - 1] = page;
    return page;
}
