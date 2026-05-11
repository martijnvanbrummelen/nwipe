
/*
 *  create_system_multi_disk_pdf.c: create PDF certificate with a single disk per PDF
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

extern struct pdf_doc* pdf;
extern struct pdf_object* page;

extern char model_header[MODEL_HEADER_LENGTH]; /* Model text in the header */
extern char serial_header[SERIAL_HEADER_LENGTH]; /* Serial number text in the header */
extern char hostid_header[DMIDECODE_RESULT_LENGTH + 15]; /* host identification, UUID, serial number, system tag */
extern char barcode[BARCODE_LENGTH]; /* Contents of the barcode, i.e model:serial */
extern char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
extern char tag_header[MAX_PDF_TAG_LENGTH];
extern float height;
extern float page_width;
extern int status_icon;
extern struct pdf_object** pdf_page_array;

void append_page_array( struct pdf_object***, struct pdf_object* );
void pdf_display_status_icon( size_t, void* );

int create_system_multi_disc_pdf( nwipe_thread_data_ptr_t* ptrx )
{

    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;
    extern nwipe_prng_t nwipe_xorshift128plus_prng;
    extern nwipe_prng_t nwipe_aes_ctr_prng;
    extern nwipe_prng_t nwipe_chacha20_prng;

    /* Used by libconfig functions to retrieve data from nwipe.conf defined in conf.c */
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    // extern char dmidecode_system_uuid[];

    /* Set up the structs we will use for the data required. */
    nwipe_thread_data_ptr_t* nwipe_thread_data_ptr;
    nwipe_context_t** c;
    nwipe_misc_thread_data_t* nwipe_misc_thread_data;
    nwipe_misc_thread_data_t* d;

    /* Retrieve from the pointer passed to the function. */
    nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t*) ptrx;
    c = nwipe_thread_data_ptr->c;
    nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;
    d = nwipe_misc_thread_data;

    size_t i;  // general index
    uint32_t text_color_size_apparent;  // local use of color

    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */
    char start_time_text[50] = "";
    char end_time_text[50] = "";
    char errors[50] = "";
    char throughput_txt[50] = "";
    char page_title[50];

    size_t yoffset;
    size_t line_spacing;
    size_t page_number;
    int result;

    struct pdf_info info = { .creator = "https://github.com/PartialVolume/shredos.x86_64",
                             .producer = "https://github.com/martijnvanbrummelen/nwipe",
                             .title = "PDF Disk Erasure Certificate",
                             .author = "Nwipe",
                             .subject = "Disk Erase Certificate",
                             .date = "Today" };

    /* A pointer to the system time struct. */
    struct tm* p;

    /* variables used by libconfig */
    config_setting_t* setting;
    const char *business_name, *business_address, *contact_name, *contact_phone, *op_tech_name, *customer_name,
        *customer_address, *customer_contact_name, *customer_contact_phone;

    /* ------------------ */
    /* Initialise Various */

    /* Used to display correct icon on page 2 */
    status_icon = 0;  // zero don't display icon, see header STATUS_ICON_..

    // nwipe_log( NWIPE_LOG_NOTICE, "Create the PDF disk erasure certificate" );
    // struct pdf_doc* pdf = pdf_create( PDF_A4_WIDTH, PDF_A4_HEIGHT, &info );
    pdf = pdf_create( PDF_A4_WIDTH, PDF_A4_HEIGHT, &info );

    /* Create footer text string and append the version */
    snprintf( pdf_footer, sizeof( pdf_footer ), "Disc Erasure by NWIPE version %s", version_string );

    pdf_set_font( pdf, "Helvetica" );
    struct pdf_object* page_1 = pdf_append_page( pdf );

    /* Obtain page page_width */
    page_width = pdf_page_width( page_1 );

    /* create a single element array for the pointers to each page object,
     * this array will be expanded for each new page pointer added */
    pdf_page_array = malloc( sizeof( struct pdf_object* ) );

    /* Save the pointer for the first page */
    pdf_page_array[0] = page_1;

    /*********************************************************************
     * Create header and footer on page 1, with the exception of the green
     * tick/red icon which is set from the 'status' section below
     */

    pdf_header_footer_text( d, c[0], "Page 1 - Erasure Status", PDF_TYPE_MULTI_DISC, PDF_PAGE_ERASURE_DATA );

    /* ------------------------ */
    /* Organisation Information */

    pdf_add_line( pdf, NULL, 50, 550, 550, 550, 1, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Organisation Performing The Disk Erasure", 12, 50, 630, PDF_BLUE );
    pdf_add_text( pdf, NULL, "Business Name:", 12, 60, 610, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Business Address:", 12, 60, 590, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Name:", 12, 60, 570, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Phone:", 12, 300, 570, PDF_GRAY );

    /* Obtain organisational details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( setting != NULL )
    {
        pdf_set_font( pdf, "Helvetica-Bold" );
        if( config_setting_lookup_string( setting, "Business_Name", &business_name ) )
        {
            pdf_add_text( pdf, NULL, business_name, TEXT_SIZE_DATA, 153, 610, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Business_Address", &business_address ) )
        {
            pdf_add_text( pdf, NULL, business_address, TEXT_SIZE_DATA, 165, 590, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
        {
            pdf_add_text( pdf, NULL, contact_name, TEXT_SIZE_DATA, 145, 570, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
        {
            pdf_add_text( pdf, NULL, contact_phone, TEXT_SIZE_DATA, 390, 570, PDF_BLACK );
        }
        pdf_set_font( pdf, "Helvetica" );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Cannot locate group [Organisation_Details] in %s", nwipe_config_file );
    }

    /* -------------------- */
    /* Customer Information */
    pdf_add_line( pdf, NULL, 50, 450, 550, 450, 1, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Customer Details", 12, 50, 530, PDF_BLUE );
    pdf_add_text( pdf, NULL, "Name:", 12, 60, 510, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Address:", 12, 60, 490, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Name:", 12, 60, 470, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Phone:", 12, 300, 470, PDF_GRAY );

    /* Obtain current customer details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Selected_Customer" );
    if( setting != NULL )
    {
        pdf_set_font( pdf, "Helvetica-Bold" );
        if( config_setting_lookup_string( setting, "Customer_Name", &customer_name ) )
        {
            pdf_add_text( pdf, NULL, customer_name, TEXT_SIZE_DATA, 100, 510, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Customer_Address", &customer_address ) )
        {
            pdf_add_text( pdf, NULL, customer_address, TEXT_SIZE_DATA, 110, 490, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &customer_contact_name ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_name, TEXT_SIZE_DATA, 145, 470, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &customer_contact_phone ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_phone, TEXT_SIZE_DATA, 390, 470, PDF_BLACK );
        }
        pdf_set_font( pdf, "Helvetica" );
    }
    else
    {
        nwipe_log( NWIPE_LOG_ERROR, "Cannot locate group [Selected_Customer] in %s", nwipe_config_file );
    }

    /******************
     * Disk Information
     */
    pdf_add_text( pdf, NULL, "Disk Erasure status", 12, 50, 430, PDF_BLUE );

    /************************
     * Technician/Operator ID
     */
    pdf_add_line( pdf, NULL, 50, 120, 550, 120, 1, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Technician/Operator ID", 12, 50, 100, PDF_BLUE );
    pdf_add_text( pdf, NULL, "Name/ID:", 12, 60, 80, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Signature:", 12, 300, 100, PDF_BLUE );
    pdf_add_line( pdf, NULL, 360, 65, 550, 65, 1, PDF_GRAY );

    pdf_set_font( pdf, "Helvetica-Bold" );
    /* Obtain organisational details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( config_setting_lookup_string( setting, "Op_Tech_Name", &op_tech_name ) )
    {
        pdf_add_text( pdf, NULL, op_tech_name, TEXT_SIZE_DATA, 120, 80, PDF_BLACK );
    }
    pdf_set_font( pdf, "Helvetica" );

    yoffset = 410;  // start y offset of disc details
    line_spacing = 10;  // vertical distance between lines
    page_number = 1;

    /*************************************
     * For each disc wiped, print an entry
     */
    for( i = 0; i < nwipe_misc_thread_data->nwipe_selected; i++ )
    {
        // create a new page if we have already reached the bottom of the page.
        if( yoffset < 210 )
        {
            yoffset = 630;
            page_number++;

            /* create a new page and save it's page pointer to the page index */
            page = pdf_append_page_and_update_index( pdf, page_number );
            if( page == NULL )
            {
                nwipe_log( NWIPE_LOG_INFO, "Failed to allocate memory when adding new page = %zu", page_number );
                return -1;
            }

            /* Create the header and footer for page 2 */
            snprintf( page_title, sizeof( page_title ), "Page %zu - Erasure Status", page_number );
            pdf_header_footer_text( d, c[i], page_title, PDF_TYPE_MULTI_DISC, PDF_PAGE_ERASURE_DATA );
        }
        if( c[i]->device_serial_no[0] == 0 )
        {
            snprintf( c[i]->device_serial_no, sizeof( c[i]->device_serial_no ), "Unknown" );
        }

        /************
         * Make/model/serial number
         */
        pdf_set_font( pdf, "Courier-Bold" );
        pdf_add_text( pdf, NULL, "Make:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text( pdf, NULL, c[i]->device_model, TEXT_SIZE_DATA, 90, yoffset, PDF_DARK_GREEN );
        pdf_add_text( pdf, NULL, "S/N:", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        pdf_add_text( pdf, NULL, c[i]->device_serial_no, TEXT_SIZE_DATA, 330, yoffset, PDF_DARK_GREEN );
        snprintf( device_size, sizeof( device_size ), "%s,%llib", c[i]->device_size_text, c[i]->device_size );

        /************
         * Size (apparent)
         */
        yoffset = yoffset - line_spacing;  // next line
        pdf_add_text( pdf, NULL, "Size(Apparent): ", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        snprintf( device_size, sizeof( device_size ), "%s,%llib", c[i]->device_size_text, c[i]->device_size );
        text_color_size_apparent =
            determine_color_for_size_apparent( c[i] );  // RED hidden sectors detected, GREEN actual size
        pdf_add_text( pdf, NULL, device_size, TEXT_SIZE_DATA, 150, yoffset, text_color_size_apparent );

        /************
         * Size (real)
         */
        pdf_add_text( pdf, NULL, "Size(Real): ", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        pdf_add_text_size_real( 370, yoffset, c[i] );

        /************
         * start time
         */
        yoffset = yoffset - line_spacing;  // next line
        pdf_add_text( pdf, NULL, "Start time:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        p = localtime( &c[i]->start_time );
        snprintf( start_time_text,
                  sizeof( start_time_text ),
                  "%i/%02i/%02i %02i:%02i:%02i",
                  1900 + p->tm_year,
                  1 + p->tm_mon,
                  p->tm_mday,
                  p->tm_hour,
                  p->tm_min,
                  p->tm_sec );
        pdf_add_text( pdf, NULL, start_time_text, TEXT_SIZE_DATA, 150, yoffset, PDF_BLACK );

        /*************
         * end time
         */
        pdf_add_text( pdf, NULL, "End time:", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        p = localtime( &c[i]->end_time );
        snprintf( end_time_text,
                  sizeof( end_time_text ),
                  "%i/%02i/%02i %02i:%02i:%02i",
                  1900 + p->tm_year,
                  1 + p->tm_mon,
                  p->tm_mday,
                  p->tm_hour,
                  p->tm_min,
                  p->tm_sec );
        pdf_add_text( pdf, NULL, end_time_text, TEXT_SIZE_DATA, 360, yoffset, PDF_BLACK );

        /*************
         * Duration
         */
        yoffset = yoffset - line_spacing;  // next line
        pdf_add_text( pdf, NULL, "Duration:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text( pdf, NULL, c[i]->duration_str, TEXT_SIZE_DATA, 150, yoffset, PDF_BLACK );

        /********
         * Errors
         */
        pdf_add_text( pdf, NULL, "Errors(pass/sync/verify):", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        snprintf( errors,
                  sizeof( errors ),
                  "%llu/%llu/%llu",
                  c[i]->pass_errors,
                  c[i]->fsyncdata_errors,
                  c[i]->verify_errors );
        if( c[i]->pass_errors != 0 || c[i]->fsyncdata_errors != 0 || c[i]->verify_errors != 0 )
        {
            pdf_add_text( pdf, NULL, errors, TEXT_SIZE_DATA, 450, yoffset, PDF_RED );
        }
        else
        {
            pdf_add_text( pdf, NULL, errors, TEXT_SIZE_DATA, 450, yoffset, PDF_DARK_GREEN );
        }

        /* ************
         * bytes erased
         */
        yoffset = yoffset - line_spacing;  // next line
        pdf_add_text( pdf, NULL, "*Bytes Erased:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text_bytes_erased( 150, yoffset, c[i] );

        /************
         * Throughput
         */
        pdf_add_text( pdf, NULL, "Throughput:", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        snprintf( throughput_txt, sizeof( throughput_txt ), "%s/sec", c[i]->throughput_txt );
        pdf_add_text( pdf, NULL, throughput_txt, TEXT_SIZE_DATA, 360, yoffset, PDF_BLACK );

        /********
         * Method
         */
        yoffset = yoffset - line_spacing;  // next line
        pdf_add_text( pdf, NULL, "Method:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text( pdf, NULL, nwipe_method_label( nwipe_options.method ), TEXT_SIZE_DATA, 150, yoffset, PDF_BLACK );

        /***********
         * prng type
         */
        pdf_add_text( pdf, NULL, "PRNG algorithm:", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        pdf_add_text_prng_type( 395, yoffset, PDF_BLACK );

        /***********
         * Blanking pass
         */
        yoffset = yoffset - line_spacing;  // next
        pdf_add_text( pdf, NULL, "Blanking Pass:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text_blanking( TEXT_SIZE_DATA, 150, yoffset );

        /***********
         * Verify
         */
        pdf_add_text( pdf, NULL, "Verify(Last/All/None):", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        pdf_add_text_verify( TEXT_SIZE_DATA, 450, yoffset );

        /**********
         * HPA, DCO status
         */
        yoffset = yoffset - line_spacing;  // next
        pdf_add_text( pdf, NULL, "HPA/DCO:", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text_hpa_status( TEXT_SIZE_DATA, 150, yoffset, c[i] );

        /************************
         * HPA, DCO size
         */
        pdf_add_text( pdf, NULL, "HPA/DCO Size:", TEXT_SIZE_DATA, 300, yoffset, PDF_GRAY );
        pdf_add_text_hpa_size( TEXT_SIZE_DATA, 390, yoffset, c[i] );

        /***********
         * Rounds
         */
        yoffset = yoffset - line_spacing;  // next
        pdf_add_text( pdf, NULL, "Rounds(completed/requested):", TEXT_SIZE_DATA, LEFT_MARGIN_TEXT, yoffset, PDF_GRAY );
        pdf_add_text_rounds( TEXT_SIZE_DATA, 230, yoffset, c[i] );

        /***********
         * Status of Erasure
         */
        pdf_add_text_status_of_erasure(
            LEFT_MARGIN_TEXT - 12, yoffset + 20, LEFT_MARGIN_TEXT - 15, yoffset + 40, 10, 35, 1.5707, c[i] );

        /* ****************
         * Display warning if hidden sectors found or if HPA/DCO status cannot be determined
         */
        if( !strcmp( c[i]->wipe_status_txt, "ERASED" )
            && ( c[i]->HPA_status == HPA_ENABLED || c[i]->HPA_status == HPA_UNKNOWN ) )
        {
            pdf_add_text( pdf, NULL, "See Warning !", TEXT_SIZE_DATA, 300, yoffset, PDF_RED );
        }

        yoffset = yoffset - ( line_spacing * 2 );  // insert a blank line between individual disc details
    }

    /***************************************
     * Add SMBIOS/DMI host data page
     */
    pdf_add_text_host_info_page(
        pdf, &page_number, LEFT_MARGIN_TEXT, TOP_OF_TEXT_WINDOW_Y, PDF_TYPE_MULTI_DISC, NULL, d );

    /***************************************
     * Populate subsequent pages with smart data for each drive
     */
    for( i = 0; i < nwipe_misc_thread_data->nwipe_selected; i++ )
    {
        result = nwipe_get_smart_data( d, PDF_TYPE_MULTI_DISC, &page_number, c[i] );
        if( result != 0 )
        {
            // fatal error, don't bother trying to save the file'
            nwipe_log( NWIPE_LOG_ERROR, "Function nwipe_get_smart_data() returned an error %u", result );
            goto cleanup;
        }
    }

    /************************************************************************************
     * Display the appropriate status icon (green tick, red cross, tick with exclamation).
     * On a multidisc pdf we may not know the status of all the drives until we
     * have written the individual drive entries, so prior to the following function, we
     * have created all the drive erasure pages first plus all the smart data pages. Once
     * we know the overall status of ALL drives, then we can go back and write the overall
     * status icon that represents the overall system, to each individual page.
     */
    for( i = 0; i < page_number; i++ )
    {
        pdf_display_status_icon( PDF_TYPE_MULTI_DISC, pdf_page_array[i] );
    }

    /*****************************
     * Create the reports filename
     *
     * Sanitize the strings that we are going to use to create the report filename
     * by converting any non alphanumeric characters to an underscore or hyphen
     */
    char PDF_filename[FILENAME_MAX];  // The filename of the PDF certificate/report.
    replace_non_alphanumeric( end_time_text, '-' );
    replace_non_alphanumeric( d->dmidecode_system_uuid, '-' );
    replace_non_alphanumeric( d->dmidecode_system_serial_number, '-' );
    snprintf( PDF_filename,
              sizeof( PDF_filename ),
              "%s/nwipe_system_report_%s_host-UUID-%s_host-SN-%s.pdf",
              nwipe_options.PDFreportpath,
              end_time_text,
              d->dmidecode_system_uuid,
              d->dmidecode_system_serial_number );

    /***********************
     * Write the PDF to disk
     */
    pdf_save( pdf, PDF_filename );

/**********************
 * Clean up and free memory
 */
cleanup:
    pdf_destroy( pdf );
    free( pdf_page_array );
    return 0;
}
