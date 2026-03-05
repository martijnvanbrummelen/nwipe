
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

    //    char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];

    /* Set up the structs we will use for the data required. */
    nwipe_thread_data_ptr_t* nwipe_thread_data_ptr;
    nwipe_context_t** c;
    nwipe_misc_thread_data_t* nwipe_misc_thread_data;

    /* Retrieve from the pointer passed to the function. */
    nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t*) ptrx;
    c = nwipe_thread_data_ptr->c;
    nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;

    int i;  // general index

    //    char model_header[50] = ""; /* Model text in the header */
    //    char serial_header[30] = ""; /* Serial number text in the header */
    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */
    //    char barcode[100] = ""; /* Contents of the barcode, i.e model:serial */
    char verify[20] = ""; /* Verify option text */
    char blank[10] = ""; /* blanking pass, none, zeros, ones */
    char rounds[50] = ""; /* rounds ASCII numeric */
    char prng_type[50] = ""; /* type of prng, twister, isaac, isaac64 */
    char start_time_text[50] = "";
    char end_time_text[50] = "";
    char bytes_erased[50] = "";
    char HPA_status_text[50] = "";
    char HPA_size_text[50] = "";
    char errors[50] = "";
    char throughput_txt[50] = "";
    char bytes_percent_str[7] = "";

    //    int status_icon;

    //    float height;
    //    float page_width;

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

    /*********************************************************************
     * Create header and footer on page 1, with the exception of the green
     * tick/red icon which is set from the 'status' section below
     */

    pdf_header_footer_text( c[0], "Page 1 - Erasure Status" );

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

    /* For each disc wiped, print an entry */
    for( i = 0; i < nwipe_misc_thread_data->nwipe_enumerated; i++ )
    {
        if( c[0]->device_serial_no[0] == 0 )
        {
            snprintf( c[0]->device_serial_no, sizeof( c[0]->device_serial_no ), "Unknown" );
        }
        // WARNING DELETE THIS NWIPE_LOG command
        nwipe_log( NWIPE_LOG_WARNING, "Model: %s", c[i]->device_name_without_path );

        /************
         * Make/model/serial number
         */
        pdf_set_font( pdf, "Courier-Bold" );
        pdf_add_text( pdf, NULL, "Make:", text_size_data, 60, 410, PDF_DARK_GREEN );
        pdf_add_text( pdf, NULL, c[i]->device_model, text_size_data, 90, 410, PDF_DARK_GREEN );
        pdf_add_text( pdf, NULL, "S/N:", text_size_data, 200, 410, PDF_DARK_GREEN );
        pdf_add_text( pdf, NULL, c[i]->device_serial_no, text_size_data, 230, 410, PDF_DARK_GREEN );
        snprintf( device_size, sizeof( device_size ), "%s, %lli bytes", c[i]->device_size_text, c[i]->device_size );
        if( ( c[i]->device_size == c[i]->Calculated_real_max_size_in_bytes ) || c[i]->device_type == NWIPE_DEVICE_NVME
            || c[i]->device_type == NWIPE_DEVICE_VIRT || c[i]->HPA_status == HPA_NOT_APPLICABLE
            || c[i]->HPA_status != HPA_UNKNOWN )
        {
            pdf_add_text( pdf, NULL, device_size, text_size_data, 350, 410, PDF_DARK_GREEN );
        }
        else
        {
            pdf_add_text( pdf, NULL, device_size, text_size_data, 350, 410, PDF_RED );
        }
    }

    /*****************************
     * Create the reports filename
     *
     * Sanitize the strings that we are going to use to create the report filename
     * by converting any non alphanumeric characters to an underscore or hyphen
     */
    char PDF_filename[FILENAME_MAX];  // The filename of the PDF certificate/report.
    replace_non_alphanumeric( end_time_text, '-' );
    snprintf( PDF_filename, sizeof( PDF_filename ), "nwipe_report_system.pdf" );

    pdf_save( pdf, PDF_filename );
    pdf_destroy( pdf );
    return 0;
}
