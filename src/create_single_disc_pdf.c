/*
 *  create_single_disc_pdf.c: create PDF certificate with a single disc per PDF
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

int create_single_disc_pdf( nwipe_context_t* ptr )
{
    /* Used by libconfig functions to retrieve data from nwipe.conf defined in conf.c */
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    uint32_t text_color_size_apparent;  // local use of color

    //    char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
    nwipe_context_t* c;
    c = ptr;
    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */
    char start_time_text[50] = "";
    char end_time_text[50] = "";
    char errors[50] = "";
    char throughput_txt[50] = "";

    size_t page_number = 1;

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

    /**********************************************
     * Initialise serial no. to unknown if empty
     */
    if( c->device_serial_no[0] == 0 )
    {
        snprintf( c->device_serial_no, sizeof( c->device_serial_no ), "Unknown" );
    }

    /*********************************************************************
     * Create header and footer on page 1, with the exception of the green
     * tick/red icon which is set from the 'status' section below
     */

    pdf_header_footer_text( c, "Page 1 - Erasure Status", PDF_TYPE_SINGLE_DISC, PDF_PAGE_ERASURE_DATA );

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
            pdf_add_text( pdf, NULL, business_name, text_size_data, 153, 610, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Business_Address", &business_address ) )
        {
            pdf_add_text( pdf, NULL, business_address, text_size_data, 165, 590, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
        {
            pdf_add_text( pdf, NULL, contact_name, text_size_data, 145, 570, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
        {
            pdf_add_text( pdf, NULL, contact_phone, text_size_data, 390, 570, PDF_BLACK );
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
            pdf_add_text( pdf, NULL, customer_name, text_size_data, 100, 510, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Customer_Address", &customer_address ) )
        {
            pdf_add_text( pdf, NULL, customer_address, text_size_data, 110, 490, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Name", &customer_contact_name ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_name, text_size_data, 145, 470, PDF_BLACK );
        }
        if( config_setting_lookup_string( setting, "Contact_Phone", &customer_contact_phone ) )
        {
            pdf_add_text( pdf, NULL, customer_contact_phone, text_size_data, 390, 470, PDF_BLACK );
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
    pdf_add_line( pdf, NULL, 50, 350, 550, 350, 1, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Disk Information", 12, 50, 430, PDF_BLUE );

    /************
     * Make/model
     */
    pdf_add_text( pdf, NULL, "Make/Model:", 12, 60, 410, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->device_model, text_size_data, 135, 410, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /************
     * Serial no.
     */
    pdf_add_text( pdf, NULL, "Serial:", 12, 340, 410, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->device_serial_no, text_size_data, 380, 410, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /******************************
     * Bus type, ATA, USB, NVME etc
     */
    pdf_add_text( pdf, NULL, "Bus:", 12, 340, 390, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->device_type_str, text_size_data, 370, 390, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /*************************
     * Capacity (Size) of disk
     */

    /* Size (Apparent) */
    pdf_add_text( pdf, NULL, "Size(Apparent): ", 12, 60, 390, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( device_size, sizeof( device_size ), "%s, %lli bytes", c->device_size_text, c->device_size );
    text_color_size_apparent =
        determine_color_for_size_apparent( c );  // RED hidden sectors detected, GREEN actual size
    pdf_add_text( pdf, NULL, device_size, text_size_data, 145, 390, text_color_size_apparent );
    pdf_set_font( pdf, "Helvetica" );

    /* Size (Real) */
    pdf_add_text( pdf, NULL, "Size(Real):", 12, 60, 370, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_size_real( 125, 370, c );
    pdf_set_font( pdf, "Helvetica" );

    /* --------------- */
    /* Erasure Details */
    pdf_add_text( pdf, NULL, "Disk Erasure Details", 12, 50, 330, PDF_BLUE );

    /* start time */
    pdf_add_text( pdf, NULL, "Start time:", 12, 60, 310, PDF_GRAY );
    p = localtime( &c->start_time );
    snprintf( start_time_text,
              sizeof( start_time_text ),
              "%i/%02i/%02i %02i:%02i:%02i",
              1900 + p->tm_year,
              1 + p->tm_mon,
              p->tm_mday,
              p->tm_hour,
              p->tm_min,
              p->tm_sec );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, start_time_text, text_size_data, 120, 310, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* end time */
    pdf_add_text( pdf, NULL, "End time:", 12, 300, 310, PDF_GRAY );
    p = localtime( &c->end_time );
    snprintf( end_time_text,
              sizeof( end_time_text ),
              "%i/%02i/%02i %02i:%02i:%02i",
              1900 + p->tm_year,
              1 + p->tm_mon,
              p->tm_mday,
              p->tm_hour,
              p->tm_min,
              p->tm_sec );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, end_time_text, text_size_data, 360, 310, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* Duration */
    pdf_add_text( pdf, NULL, "Duration:", 12, 60, 290, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, c->duration_str, text_size_data, 115, 290, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /*******************
     * Status of erasure
     */
    pdf_add_text( pdf, NULL, "Status:", 12, 300, 290, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_status_of_erasure( 365, 290, 390, 295, 45, 10, 0, c );
    pdf_set_font( pdf, "Helvetica" );

    /* ****************
     * Display warning if hidden sectors found or if HPA/DCO status cannot be determined
     */
    if( !strcmp( c->wipe_status_txt, "ERASED" ) && ( c->HPA_status == HPA_ENABLED || c->HPA_status == HPA_UNKNOWN ) )
    {
        pdf_add_text( pdf, NULL, "See Warning !", 12, 450, 290, PDF_RED );
    }
    /********
     * Display the appropriate status icon (green tick, red cross, tick with exclamation)
     */
    pdf_display_status_icon( PDF_TYPE_SINGLE_DISC, NULL );

    /********
     * Method
     */
    pdf_add_text( pdf, NULL, "Method:", 12, 60, 270, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, nwipe_method_label( nwipe_options.method ), text_size_data, 110, 270, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /***********
     * prng type
     */
    pdf_add_text( pdf, NULL, "PRNG algorithm:", 12, 300, 270, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_prng_type( 395, 270, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /******************************************************
     * Final blanking pass if selected, none, zeros or ones
     */
    pdf_add_text( pdf, NULL, "Final Pass(Zeros/Ones/None):", 12, 60, 250, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_blanking( text_size_data, 230, 250 );
    pdf_set_font( pdf, "Helvetica" );

    /* ***********************************************************************
     * Verification
     */
    pdf_add_text( pdf, NULL, "Verify Pass(Last/All/None):", 12, 300, 250, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_verify( text_size_data, 450, 250 );
    pdf_set_font( pdf, "Helvetica" );

    /* ************
     * bytes erased
     */
    pdf_add_text( pdf, NULL, "*Bytes Erased:", 12, 60, 230, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_bytes_erased( 150, 230, c );
    pdf_set_font( pdf, "Helvetica" );

    /************************************************
     * rounds - How many times the method is repeated
     */
    pdf_add_text( pdf, NULL, "Rounds(completed/requested):", 12, 300, 230, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_rounds( text_size_data, 470, 230, c );
    pdf_set_font( pdf, "Helvetica" );

    /*******************
     * HPA, DCO - LABELS
     */
    pdf_add_text( pdf, NULL, "HPA/DCO:", 12, 60, 210, PDF_GRAY );
    pdf_add_text( pdf, NULL, "HPA/DCO Size:", 12, 300, 210, PDF_GRAY );

    /*******************
     * Populate HPA size
     */
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_hpa_size( text_size_data, 390, 210, c );
    pdf_set_font( pdf, "Helvetica" );

    /*********************
     * Populate HPA status (and size if not applicable, NVMe and VIRT)
     */
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text_hpa_status( text_size_data, 130, 210, c );
    pdf_set_font( pdf, "Helvetica" );

    /************
     * Throughput
     */
    pdf_add_text( pdf, NULL, "Throughput:", 12, 300, 190, PDF_GRAY );
    snprintf( throughput_txt, sizeof( throughput_txt ), "%s/sec", c->throughput_txt );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, throughput_txt, text_size_data, 370, 190, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /********
     * Errors
     */
    pdf_add_text( pdf, NULL, "Errors(pass/sync/verify):", 12, 60, 190, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( errors, sizeof( errors ), "%llu/%llu/%llu", c->pass_errors, c->fsyncdata_errors, c->verify_errors );
    if( c->pass_errors != 0 || c->fsyncdata_errors != 0 || c->verify_errors != 0 )
    {
        pdf_add_text( pdf, NULL, errors, text_size_data, 195, 190, PDF_RED );
    }
    else
    {
        pdf_add_text( pdf, NULL, errors, text_size_data, 195, 190, PDF_DARK_GREEN );
    }
    pdf_set_font( pdf, "Helvetica" );

    /*************
     * Information
     */
    pdf_add_text( pdf, NULL, "Information:", 12, 60, 170, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );

    if( !strcmp( c->wipe_status_txt, "ERASED" ) && c->HPA_status == HPA_ENABLED )
    {
        pdf_add_ellipse( pdf, NULL, 160, 173, 30, 9, 2, PDF_RED, PDF_BLACK );
        pdf_add_text( pdf, NULL, "Warning", text_size_data, 140, 170, PDF_YELLOW );

        pdf_add_text( pdf,
                      NULL,
                      "Visible sectors erased as requested, however hidden sectors NOT erased",
                      text_size_data,
                      200,
                      170,
                      PDF_RED );
    }
    else
    {
        if( c->HPA_status == HPA_UNKNOWN )
        {
            pdf_add_ellipse( pdf, NULL, 160, 173, 30, 9, 2, PDF_RED, PDF_BLACK );
            pdf_add_text( pdf, NULL, "Warning", text_size_data, 140, 170, PDF_YELLOW );

            pdf_add_text( pdf,
                          NULL,
                          "HPA/DCO data unavailable, can not determine hidden sector status.",
                          text_size_data,
                          200,
                          170,
                          PDF_RED );
        }
    }

    /* info descripting what bytes erased actually means */
    pdf_add_text( pdf,
                  NULL,
                  "* bytes erased: The amount of drive that's been erased at least once",
                  text_size_data,
                  60,
                  137,
                  PDF_BLACK );

    /* meaning of abreviation DDNSHPA */
    if( c->HPA_status == HPA_NOT_SUPPORTED_BY_DRIVE )
    {
        pdf_add_text(
            pdf, NULL, "** DDNSHPA = Drive does not support HPA/DCO", text_size_data, 60, 125, PDF_DARK_GREEN );
    }
    pdf_set_font( pdf, "Helvetica" );

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
        pdf_add_text( pdf, NULL, op_tech_name, text_size_data, 120, 80, PDF_BLACK );
    }
    pdf_set_font( pdf, "Helvetica" );

    /***************************************
     * Populate subsequent pages with smart data
     */
    nwipe_get_smart_data( PDF_TYPE_SINGLE_DISC, &page_number, c );

    /*****************************
     * Create the reports filename
     *
     * Sanitize the strings that we are going to use to create the report filename
     * by converting any non alphanumeric characters to an underscore or hyphen
     */
    replace_non_alphanumeric( end_time_text, '-' );
    replace_non_alphanumeric( c->device_model, '_' );
    replace_non_alphanumeric( c->device_serial_no, '_' );
    snprintf( c->PDF_filename,
              sizeof( c->PDF_filename ),
              "%s/nwipe_report_%s_Model_%s_Serial_%s_device_%s.pdf",
              nwipe_options.PDFreportpath,
              end_time_text,
              c->device_model,
              c->device_serial_no,
              c->device_name_terse );

    pdf_save( pdf, c->PDF_filename );
    pdf_destroy( pdf );
    return 0;
}
