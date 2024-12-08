/*
 *  create_pdf.c: Routines that create the PDF erasure certificate
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

/* OpenSSL Headers */
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <qrencode.h>
#include <openssl/bio.h>

/* Misc Headers */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb/stb_image_write.h"

#define text_size_data 10

struct pdf_doc* pdf;
struct pdf_object* page;

char model_header[50] = ""; /* Model text in the header */
char serial_header[30] = ""; /* Serial number text in the header */
char barcode[100] = ""; /* Contents of the barcode, i.e., model:serial */
char pdf_footer[MAX_PDF_FOOTER_TEXT_LENGTH];
float height;
float page_width;
int status_icon;

/* Prototypes for new functions */
int generate_key_and_certificate( EVP_PKEY** pkey, X509** x509 );
int sign_pdf( const char* pdf_filename, EVP_PKEY* pkey, unsigned char** signature, size_t* signature_len );
void add_signature_to_pdf( struct pdf_doc* pdf, nwipe_context_t* c, unsigned char* signature, size_t signature_len );

int create_pdf( nwipe_context_t* ptr )
{
    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;

    /* Used by libconfig functions to retrieve data from nwipe.conf defined in conf.c */
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    nwipe_context_t* c;
    c = ptr;

    char device_size[100] = ""; /* Device size in the form xMB (xxxx bytes) */
    char verify[20] = ""; /* Verify option text */
    char blank[10] = ""; /* Blanking pass: none, zeros, ones */
    char rounds[50] = ""; /* Rounds ASCII numeric */
    char prng_type[50] = ""; /* Type of PRNG */
    char start_time_text[50] = "";
    char end_time_text[50] = "";
    char bytes_erased[50] = "";
    char HPA_status_text[50] = "";
    char HPA_size_text[50] = "";
    char errors[50] = "";
    char throughput_txt[50] = "";
    char bytes_percent_str[7] = "";

    struct pdf_info info = { .creator = "https://github.com/PartialVolume/shredos.x86_64",
                             .producer = "https://github.com/martijnvanbrummelen/nwipe",
                             .title = "PDF Disk Erasure Certificate",
                             .author = "Nwipe",
                             .subject = "Disk Erase Certificate",
                             .date = "Today" };

    /* A pointer to the system time struct. */
    struct tm* p;

    /* Variables used by libconfig */
    config_setting_t* setting;
    const char *business_name, *business_address, *contact_name, *contact_phone, *op_tech_name, *customer_name,
        *customer_address, *customer_contact_name, *customer_contact_phone;

    /* Initialize variables */

    /* Used to display correct icon on page 2 */
    status_icon = 0;  // zero means don't display icon, see header STATUS_ICON_...

    pdf = pdf_create( PDF_A4_WIDTH, PDF_A4_HEIGHT, &info );

    /* Create footer text string and append the version */
    snprintf( pdf_footer, sizeof( pdf_footer ), "Disc Erasure by Nwipe version %s", version_string );

    pdf_set_font( pdf, "Helvetica" );
    struct pdf_object* page_1 = pdf_append_page( pdf );

    /* Obtain page width */
    page_width = pdf_page_width( page_1 );

    /*********************************************************************
     * Create header and footer on page 1, with the exception of the green
     * tick/red icon which is set from the 'status' section below
     */
    pdf_add_text_wrap( pdf, NULL, pdf_footer, 12, 0, 30, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_line( pdf, NULL, 50, 50, 550, 50, 3, PDF_BLACK );
    pdf_add_line( pdf, NULL, 50, 650, 550, 650, 3, PDF_BLACK );
    pdf_add_image_data( pdf, NULL, 45, 665, 100, 100, bin2c_shred_db_jpg, 27063 );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( model_header, sizeof( model_header ), " %s: %s ", "Model", c->device_model );
    pdf_add_text_wrap( pdf, NULL, model_header, 14, 0, 755, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "S/N", c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, serial_header, 14, 0, 735, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_set_font( pdf, "Helvetica" );

    pdf_add_text_wrap( pdf, NULL, "Disk Erasure Report", 24, 0, 695, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    snprintf( barcode, sizeof( barcode ), "%s:%s", c->device_model, c->device_serial_no );
    pdf_add_text_wrap(
        pdf, NULL, "Page 1 - Erasure Status", 14, 0, 670, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_barcode( pdf, NULL, PDF_BARCODE_128A, 100, 790, 400, 25, barcode, PDF_BLACK );

    /* ------------------------ */
    /* Organization Information */

    pdf_add_line( pdf, NULL, 50, 550, 550, 550, 1, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Organization Performing The Disk Erasure", 12, 50, 630, PDF_BLUE );
    pdf_add_text( pdf, NULL, "Business Name:", 12, 60, 610, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Business Address:", 12, 60, 590, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Name:", 12, 60, 570, PDF_GRAY );
    pdf_add_text( pdf, NULL, "Contact Phone:", 12, 300, 570, PDF_GRAY );

    /* Obtain organizational details from nwipe.conf - See conf.c */
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
    if( c->device_serial_no[0] == 0 )
    {
        snprintf( c->device_serial_no, sizeof( c->device_serial_no ), "Unknown" );
    }
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
    if( ( c->device_size == c->Calculated_real_max_size_in_bytes ) || c->device_type == NWIPE_DEVICE_NVME
        || c->device_type == NWIPE_DEVICE_VIRT || c->HPA_status == HPA_NOT_APPLICABLE || c->HPA_status != HPA_UNKNOWN )
    {
        pdf_add_text( pdf, NULL, device_size, text_size_data, 145, 390, PDF_DARK_GREEN );
    }
    else
    {
        pdf_add_text( pdf, NULL, device_size, text_size_data, 145, 390, PDF_RED );
    }
    pdf_set_font( pdf, "Helvetica" );

    /* Size (Real) */
    pdf_add_text( pdf, NULL, "Size(Real):", 12, 60, 370, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( device_size, sizeof( device_size ), "%s, %lli bytes", c->device_size_text, c->device_size );
        pdf_add_text( pdf, NULL, device_size, text_size_data, 125, 370, PDF_DARK_GREEN );
    }
    else
    {
        /* If the calculated real max size as determined from HPA/DCO and libata data is larger than
         * or equal to the apparent device size then display that value in green.
         */
        if( c->Calculated_real_max_size_in_bytes >= c->device_size )
        {
            /* Displays the real max size of the disk from the DCO displayed in Green */
            snprintf( device_size,
                      sizeof( device_size ),
                      "%s, %lli bytes",
                      c->Calculated_real_max_size_in_bytes_text,
                      c->Calculated_real_max_size_in_bytes );
            pdf_add_text( pdf, NULL, device_size, text_size_data, 125, 370, PDF_DARK_GREEN );
        }
        else
        {
            /* If there is no real max size either because the drive or adapter doesn't support it */
            if( c->HPA_status == HPA_UNKNOWN )
            {
                snprintf( device_size, sizeof( device_size ), "Unknown" );
                pdf_add_text( pdf, NULL, device_size, text_size_data, 125, 370, PDF_RED );
            }
            else
            {
                /* We are already here because c->Calculated_real_max_size_in_bytes < 1 so if HPA enabled then use the
                 * value we determine from whether HPA set, HPA real exist and if not assume libata's value */
                if( c->HPA_status == HPA_ENABLED )
                {
                    snprintf( device_size,
                              sizeof( device_size ),
                              "%s, %lli bytes",
                              c->device_size_text,
                              c->Calculated_real_max_size_in_bytes );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, 125, 370, PDF_DARK_GREEN );
                }
                else
                {
                    /* Sanity check, should never get here! */
                    snprintf( device_size, sizeof( device_size ), "Sanity: HPA_status = %i", c->HPA_status );
                    pdf_add_text( pdf, NULL, device_size, text_size_data, 125, 370, PDF_RED );
                }
            }
        }
    }

    pdf_set_font( pdf, "Helvetica" );

    /* --------------- */
    /* Erasure Details */
    pdf_add_text( pdf, NULL, "Disk Erasure Details", 12, 50, 330, PDF_BLUE );

    /* Start time */
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

    /* End time */
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

    if( !strcmp( c->wipe_status_txt, "ERASED" )
        && ( c->HPA_status == HPA_DISABLED || c->HPA_status == HPA_NOT_APPLICABLE || c->device_type == NWIPE_DEVICE_NVME
             || c->device_type == NWIPE_DEVICE_VIRT ) )
    {
        pdf_add_text( pdf, NULL, c->wipe_status_txt, 12, 365, 290, PDF_DARK_GREEN );
        pdf_add_ellipse( pdf, NULL, 390, 295, 45, 10, 2, PDF_DARK_GREEN, PDF_TRANSPARENT );

        /* Display the green tick icon in the header */
        pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_te_jpg, 54896 );
        status_icon = STATUS_ICON_GREEN_TICK;  // used later on page 2
    }
    else
    {
        if( !strcmp( c->wipe_status_txt, "ERASED" )
            && ( c->HPA_status == HPA_ENABLED || c->HPA_status == HPA_UNKNOWN ) )
        {
            pdf_add_ellipse( pdf, NULL, 390, 295, 45, 10, 2, PDF_RED, PDF_BLACK );
            pdf_add_text( pdf, NULL, c->wipe_status_txt, 12, 365, 290, PDF_YELLOW );
            pdf_add_text( pdf, NULL, "See Warning !", 12, 450, 290, PDF_RED );

            /* Display the yellow exclamation icon in the header */
            pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_nwipe_exclamation_jpg, 65791 );
            status_icon = STATUS_ICON_YELLOW_EXCLAMATION;  // used later on page 2
        }
        else
        {
            if( !strcmp( c->wipe_status_txt, "FAILED" ) )
            {
                // Text shifted left slightly in ellipse due to extra character
                pdf_add_text( pdf, NULL, c->wipe_status_txt, 12, 370, 290, PDF_RED );

                // Display the red cross in the header
                pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_redcross_jpg, 60331 );
                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2
            }
            else
            {
                pdf_add_text( pdf, NULL, c->wipe_status_txt, 12, 360, 290, PDF_RED );

                // Print the red cross
                pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_redcross_jpg, 60331 );
                status_icon = STATUS_ICON_RED_CROSS;  // used later on page 2
            }
            pdf_add_ellipse( pdf, NULL, 390, 295, 45, 10, 2, PDF_RED, PDF_TRANSPARENT );
        }
    }
    pdf_set_font( pdf, "Helvetica" );

    /********
     * Method
     */
    pdf_add_text( pdf, NULL, "Method:", 12, 60, 270, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, nwipe_method_label( nwipe_options.method ), text_size_data, 110, 270, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /***********
     * PRNG type
     */
    pdf_add_text( pdf, NULL, "PRNG algorithm:", 12, 300, 270, PDF_GRAY );
    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero
        || nwipe_options.method == &nwipe_zero || nwipe_options.method == &nwipe_one )
    {
        snprintf( prng_type, sizeof( prng_type ), "Not applicable to method" );
    }
    else
    {
        if( nwipe_options.prng == &nwipe_twister )
        {
            snprintf( prng_type, sizeof( prng_type ), "Twister" );
        }
        else if( nwipe_options.prng == &nwipe_isaac )
        {
            snprintf( prng_type, sizeof( prng_type ), "Isaac" );
        }
        else if( nwipe_options.prng == &nwipe_isaac64 )
        {
            snprintf( prng_type, sizeof( prng_type ), "Isaac64" );
        }
        else if( nwipe_options.prng == &nwipe_add_lagg_fibonacci_prng )
        {
            snprintf( prng_type, sizeof( prng_type ), "Fibonacci" );
        }
        else if( nwipe_options.prng == &nwipe_xoroshiro256_prng )
        {
            snprintf( prng_type, sizeof( prng_type ), "XORshiro256" );
        }
        else
        {
            snprintf( prng_type, sizeof( prng_type ), "Unknown" );
        }
    }
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, prng_type, text_size_data, 395, 270, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /******************************************************
     * Final blanking pass if selected: none, zeros, or ones
     */
    if( nwipe_options.noblank )
    {
        strcpy( blank, "None" );
    }
    else
    {
        strcpy( blank, "Zeros" );
    }
    pdf_add_text( pdf, NULL, "Final Pass(Zeros/Ones/None):", 12, 60, 250, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, blank, text_size_data, 230, 250, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* ***********************************************************************
     * Create suitable text based on the numeric value of type of verification
     */
    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            strcpy( verify, "Verify None" );
            break;

        case NWIPE_VERIFY_LAST:
            strcpy( verify, "Verify Last" );
            break;

        case NWIPE_VERIFY_ALL:
            strcpy( verify, "Verify All" );
            break;
    }
    pdf_add_text( pdf, NULL, "Verify Pass(Last/All/None):", 12, 300, 250, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, verify, text_size_data, 450, 250, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );

    /* ************
     * Bytes erased
     */
    pdf_add_text( pdf, NULL, "*Bytes Erased:", 12, 60, 230, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );

    /* Bytes erased is not applicable when user only requested a verify */
    if( nwipe_options.method == &nwipe_verify_one || nwipe_options.method == &nwipe_verify_zero )
    {
        snprintf( bytes_erased, sizeof( bytes_erased ), "Not applicable to method" );
        pdf_add_text( pdf, NULL, bytes_erased, text_size_data, 145, 230, PDF_BLACK );
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
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, 145, 230, PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, 145, 230, PDF_RED );
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
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, 145, 230, PDF_DARK_GREEN );
            }
            else
            {
                pdf_add_text( pdf, NULL, bytes_erased, text_size_data, 145, 230, PDF_RED );
            }
        }
    }
    pdf_set_font( pdf, "Helvetica" );

    /************************************************
     * Rounds - How many times the method is repeated
     */
    pdf_add_text( pdf, NULL, "Rounds(completed/requested):", 12, 300, 230, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    if( !strcmp( c->wipe_status_txt, "ERASED" ) )
    {
        snprintf( rounds, sizeof( rounds ), "%i/%i", c->round_working, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size_data, 470, 230, PDF_DARK_GREEN );
    }
    else
    {
        snprintf( rounds, sizeof( rounds ), "%i/%i", c->round_working - 1, nwipe_options.rounds );
        pdf_add_text( pdf, NULL, rounds, text_size_data, 470, 230, PDF_RED );
    }
    pdf_set_font( pdf, "Helvetica" );

    /*******************
     * HPA, DCO - Labels
     */
    pdf_add_text( pdf, NULL, "HPA/DCO:", 12, 60, 210, PDF_GRAY );
    pdf_set_font( pdf, "Helvetica-Bold" );
    pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 155, 210, PDF_BLACK );
    pdf_set_font( pdf, "Helvetica" );
    pdf_add_text( pdf, NULL, "HPA/DCO Size:", 12, 300, 210, PDF_GRAY );

    /*******************
     * Populate HPA size
     */

    pdf_set_font( pdf, "Helvetica-Bold" );
    if( c->HPA_status == HPA_ENABLED )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "%lli sectors", c->HPA_sectors );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, 390, 210, PDF_RED );
    }
    else if( c->HPA_status == HPA_DISABLED )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "No hidden sectors" );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, 390, 210, PDF_DARK_GREEN );
    }
    else if( c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "Not Applicable" );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, 390, 210, PDF_DARK_GREEN );
    }
    else if( c->HPA_status == HPA_UNKNOWN )
    {
        snprintf( HPA_size_text, sizeof( HPA_size_text ), "Unknown" );
        pdf_add_text( pdf, NULL, HPA_size_text, text_size_data, 390, 210, PDF_RED );
    }

    pdf_set_font( pdf, "Helvetica" );

    /*********************
     * Populate HPA status (and size if not applicable, NVMe and VIRT)
     */
    if( c->device_type == NWIPE_DEVICE_NVME || c->device_type == NWIPE_DEVICE_VIRT
        || c->HPA_status == HPA_NOT_APPLICABLE )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "Not applicable" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 130, 210, PDF_DARK_GREEN );
        pdf_set_font( pdf, "Helvetica" );
    }
    else if( c->HPA_status == HPA_ENABLED )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "Hidden sectors found!" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 130, 210, PDF_RED );
        pdf_set_font( pdf, "Helvetica" );
    }
    else if( c->HPA_status == HPA_DISABLED )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 130, 210, PDF_DARK_GREEN );
        pdf_set_font( pdf, "Helvetica" );
    }
    else if( c->HPA_status == HPA_UNKNOWN )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "Unknown" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 130, 210, PDF_RED );
        pdf_set_font( pdf, "Helvetica" );
    }
    else if( c->HPA_status == HPA_NOT_SUPPORTED_BY_DRIVE )
    {
        snprintf( HPA_status_text, sizeof( HPA_status_text ), "No hidden sectors **DDNSHDA" );
        pdf_set_font( pdf, "Helvetica-Bold" );
        pdf_add_text( pdf, NULL, HPA_status_text, text_size_data, 130, 210, PDF_DARK_GREEN );
        pdf_set_font( pdf, "Helvetica" );
    }

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
    else if( c->HPA_status == HPA_UNKNOWN )
    {
        pdf_add_ellipse( pdf, NULL, 160, 173, 30, 9, 2, PDF_RED, PDF_BLACK );
        pdf_add_text( pdf, NULL, "Warning", text_size_data, 140, 170, PDF_YELLOW );

        pdf_add_text( pdf,
                      NULL,
                      "HPA/DCO data unavailable, cannot determine hidden sector status.",
                      text_size_data,
                      200,
                      170,
                      PDF_RED );
    }

    /* Info describing what bytes erased actually means */
    pdf_add_text( pdf,
                  NULL,
                  "* bytes erased: The amount of drive that's been erased at least once",
                  text_size_data,
                  60,
                  137,
                  PDF_BLACK );

    /* Meaning of abbreviation DDNSHPA */
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
    pdf_add_line( pdf, NULL, 360, 65, 550, 66, 1, PDF_GRAY );

    pdf_set_font( pdf, "Helvetica-Bold" );
    /* Obtain organizational details from nwipe.conf - See conf.c */
    setting = config_lookup( &nwipe_cfg, "Organisation_Details" );
    if( config_setting_lookup_string( setting, "Op_Tech_Name", &op_tech_name ) )
    {
        pdf_add_text( pdf, NULL, op_tech_name, text_size_data, 120, 80, PDF_BLACK );
    }
    pdf_set_font( pdf, "Helvetica" );

    /***************************************
     * Populate page 2 and 3 with SMART data
     */
    nwipe_get_smart_data( c );

    /*****************************
     * Create the report's filename
     *
     * Sanitize the strings that we are going to use to create the report filename
     * by converting any non-alphanumeric characters to an underscore or hyphen
     */
    replace_non_alphanumeric( end_time_text, '-' );
    replace_non_alphanumeric( c->device_model, '_' );
    replace_non_alphanumeric( c->device_serial_no, '_' );
    snprintf( c->PDF_filename,
              sizeof( c->PDF_filename ),
              "%s/nwipe_report_%s_Model_%s_Serial_%s.pdf",
              nwipe_options.PDFreportpath,
              end_time_text,
              c->device_model,
              c->device_serial_no );

    /* Save the PDF */
    pdf_save( pdf, c->PDF_filename );

    /* OpenSSL initialization */
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    /* Generate key and certificate */
    EVP_PKEY* pkey = NULL;
    X509* x509 = NULL;
    if( generate_key_and_certificate( &pkey, &x509 ) != 1 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Error generating key and certificate" );
        /* Clean up and exit */
        pdf_destroy( pdf );
        return -1;
    }

    /* Sign the PDF */
    unsigned char* signature = NULL;
    size_t signature_len = 0;
    if( sign_pdf( c->PDF_filename, pkey, &signature, &signature_len ) != 1 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Error signing the PDF" );
        /* Clean up and exit */
        EVP_PKEY_free( pkey );
        X509_free( x509 );
        pdf_destroy( pdf );
        return -1;
    }

    /* Add signature to the PDF */
    add_signature_to_pdf( pdf, c, signature, signature_len );

    /* Save the updated PDF */
    pdf_save( pdf, c->PDF_filename );

    /* Free resources */
    free( signature );
    EVP_PKEY_free( pkey );
    X509_free( x509 );
    pdf_destroy( pdf );

    /* OpenSSL cleanup */
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();

    return 0;
}

int nwipe_get_smart_data( nwipe_context_t* c )
{
    FILE* fp;

    char* pdata;
    char page_title[50];

    char smartctl_command[] = "smartctl -a %s";
    char smartctl_command2[] = "/sbin/smartctl -a %s";
    char smartctl_command3[] = "/usr/bin/smartctl -a %s";
    char final_cmd_smartctl[sizeof( smartctl_command3 ) + 256];
    char result[512];
    char smartctl_labels_to_anonymize[][18] = {
        "serial number:", "lu wwn device id:", "logical unit id:", "" /* Don't remove this empty string! Important */
    };

    int idx, idx2, idx3;
    int x, y;
    int set_return_value;
    int page_number;

    final_cmd_smartctl[0] = 0;

    /* Determine whether we can access smartctl */
    if( system( "which smartctl > /dev/null 2>&1" ) )
    {
        if( system( "which /sbin/smartctl > /dev/null 2>&1" ) )
        {
            if( system( "which /usr/bin/smartctl > /dev/null 2>&1" ) )
            {
                nwipe_log( NWIPE_LOG_WARNING, "Command not found. Install smartmontools!" );
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
            x = 50;  // Left side of page
            y = 630;  // Top row of page
            page_number = 2;

            /* Create Page 2 of the report. This shows the drive's SMART data */
            page = pdf_append_page( pdf );

            /* Create the header and footer for page 2, the start of the SMART data */
            snprintf( page_title, sizeof( page_title ), "Page %i - SMART Data", page_number );
            create_header_and_footer( c, page_title );

            /* Read the output a line at a time - output it. */
            while( fgets( result, sizeof( result ) - 1, fp ) != NULL )
            {
                /* Convert the label (everything before ':') to lowercase */
                idx = 0;

                while( result[idx] != 0 && result[idx] != ':' )
                {
                    /* If uppercase alpha character, change to lowercase */
                    if( result[idx] >= 'A' && result[idx] <= 'Z' )
                    {
                        result[idx] += 32;
                    }
                    idx++;
                }

                if( nwipe_options.quiet == 1 )
                {
                    for( idx2 = 0; smartctl_labels_to_anonymize[idx2][0] != '\0'; idx2++ )
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
                    page = pdf_append_page( pdf );
                    page_number++;
                    y = 630;

                    /* Create the header and footer for the next page */
                    snprintf( page_title, sizeof( page_title ), "Page %i - SMART Data", page_number );
                    create_header_and_footer( c, page_title );
                }
            }
            pclose( fp );
            set_return_value = 0;
        }
    }
    else
    {
        set_return_value = 1;
    }
    return set_return_value;
}

void create_header_and_footer( nwipe_context_t* c, char* page_title )
{
    /**************************************************************************
     * Create header and footer on most recently added page, with the exception
     * of the status icon which is set from the 'status' section below.
     */
    pdf_add_text_wrap( pdf, NULL, pdf_footer, 12, 0, 30, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_line( pdf, NULL, 50, 50, 550, 50, 3, PDF_BLACK );
    pdf_add_line( pdf, NULL, 50, 650, 550, 650, 3, PDF_BLACK );
    pdf_add_image_data( pdf, NULL, 45, 665, 100, 100, bin2c_shred_db_jpg, 27063 );
    pdf_set_font( pdf, "Helvetica-Bold" );
    snprintf( model_header, sizeof( model_header ), " %s: %s ", "Model", c->device_model );
    pdf_add_text_wrap( pdf, NULL, model_header, 14, 0, 755, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    snprintf( serial_header, sizeof( serial_header ), " %s: %s ", "S/N", c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, serial_header, 14, 0, 735, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_set_font( pdf, "Helvetica" );

    pdf_add_text_wrap( pdf, NULL, "Disk Erasure Report", 24, 0, 695, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    snprintf( barcode, sizeof( barcode ), "%s:%s", c->device_model, c->device_serial_no );
    pdf_add_text_wrap( pdf, NULL, page_title, 14, 0, 670, PDF_BLACK, page_width, PDF_ALIGN_CENTER, &height );
    pdf_add_barcode( pdf, NULL, PDF_BARCODE_128A, 100, 790, 400, 25, barcode, PDF_BLACK );

    /**********************************************************
     * Display the appropriate status icon at the top right on
     * the most recently added page.
     */
    switch( status_icon )
    {
        case STATUS_ICON_GREEN_TICK:

            /* Display the green tick icon in the header */
            pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_te_jpg, 54896 );
            break;

        case STATUS_ICON_YELLOW_EXCLAMATION:

            /* Display the yellow exclamation icon in the header */
            pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_nwipe_exclamation_jpg, 65791 );
            break;

        case STATUS_ICON_RED_CROSS:

            /* Display the red cross in the header */
            pdf_add_image_data( pdf, NULL, 450, 665, 100, 100, bin2c_redcross_jpg, 60331 );
            break;

        default:

            break;
    }
}

/* Function to generate a key pair and a self-signed certificate */
int generate_key_and_certificate( EVP_PKEY** pkey, X509** x509 )
{
    int ret = 0;
    EVP_PKEY_CTX* pkey_ctx = NULL;
    X509_NAME* name = NULL;

    /* Generate key */
    pkey_ctx = EVP_PKEY_CTX_new_id( EVP_PKEY_RSA, NULL );
    if( !pkey_ctx )
    {
        ret = -1;
        goto cleanup;
    }

    if( EVP_PKEY_keygen_init( pkey_ctx ) <= 0 )
    {
        ret = -1;
        goto cleanup;
    }

    if( EVP_PKEY_CTX_set_rsa_keygen_bits( pkey_ctx, 2048 ) <= 0 )
    {
        ret = -1;
        goto cleanup;
    }

    if( EVP_PKEY_keygen( pkey_ctx, pkey ) <= 0 )
    {
        ret = -1;
        goto cleanup;
    }

    /* Generate certificate */
    *x509 = X509_new();
    ASN1_INTEGER_set( X509_get_serialNumber( *x509 ), 1 );
    X509_gmtime_adj( X509_get_notBefore( *x509 ), 0 );
    X509_gmtime_adj( X509_get_notAfter( *x509 ), 31536000L ); /* Validity: 1 year */
    X509_set_pubkey( *x509, *pkey );

    /* Set name */
    name = X509_get_subject_name( *x509 );
    X509_NAME_add_entry_by_txt( name, "C", MBSTRING_ASC, (unsigned char*) "DE", -1, -1, 0 );
    X509_NAME_add_entry_by_txt( name, "O", MBSTRING_ASC, (unsigned char*) "Nwipe", -1, -1, 0 );
    X509_NAME_add_entry_by_txt( name, "CN", MBSTRING_ASC, (unsigned char*) "Nwipe Certificate", -1, -1, 0 );

    /* Self-signed */
    X509_set_issuer_name( *x509, name );

    /* Sign certificate */
    if( !X509_sign( *x509, *pkey, EVP_sha256() ) )
    {
        ret = -1;
        goto cleanup;
    }

    ret = 1; /* Success */

cleanup:
    if( pkey_ctx )
        EVP_PKEY_CTX_free( pkey_ctx );
    if( ret != 1 )
    {
        if( *pkey )
            EVP_PKEY_free( *pkey );
        if( *x509 )
            X509_free( *x509 );
    }
    return ret;
}

/* Function to sign the PDF */
int sign_pdf( const char* pdf_filename, EVP_PKEY* pkey, unsigned char** signature, size_t* signature_len )
{
    FILE* pdf_file = fopen( pdf_filename, "rb" );
    if( !pdf_file )
    {
        perror( "Cannot open PDF file" );
        return -1;
    }

    /* Read PDF file and compute hash */
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if( !md_ctx )
    {
        perror( "Failed to create EVP_MD_CTX" );
        fclose( pdf_file );
        return -1;
    }

    if( EVP_DigestSignInit( md_ctx, NULL, EVP_sha256(), NULL, pkey ) <= 0 )
    {
        perror( "EVP_DigestSignInit failed" );
        EVP_MD_CTX_free( md_ctx );
        fclose( pdf_file );
        return -1;
    }

    unsigned char buffer[4096];
    size_t bytes_read;
    while( ( bytes_read = fread( buffer, 1, sizeof( buffer ), pdf_file ) ) > 0 )
    {
        if( EVP_DigestSignUpdate( md_ctx, buffer, bytes_read ) <= 0 )
        {
            perror( "EVP_DigestSignUpdate failed" );
            EVP_MD_CTX_free( md_ctx );
            fclose( pdf_file );
            return -1;
        }
    }
    fclose( pdf_file );

    /* Finalize signature */
    if( EVP_DigestSignFinal( md_ctx, NULL, signature_len ) <= 0 )
    {
        perror( "EVP_DigestSignFinal (get length) failed" );
        EVP_MD_CTX_free( md_ctx );
        return -1;
    }

    *signature = malloc( *signature_len );
    if( !*signature )
    {
        perror( "Memory allocation failed for signature" );
        EVP_MD_CTX_free( md_ctx );
        return -1;
    }

    if( EVP_DigestSignFinal( md_ctx, *signature, signature_len ) <= 0 )
    {
        perror( "EVP_DigestSignFinal failed" );
        EVP_MD_CTX_free( md_ctx );
        free( *signature );
        return -1;
    }

    EVP_MD_CTX_free( md_ctx );
    return 1; /* Success */
}

void add_signature_to_pdf( struct pdf_doc* pdf, nwipe_context_t* c, unsigned char* signature, size_t signature_len )
{
    /* Convert signature to Base64 */
    BIO *bio, *b64;
    BUF_MEM* bufferPtr;

    /* Create a Base64 filter */
    b64 = BIO_new( BIO_f_base64() );
    bio = BIO_new( BIO_s_mem() ); /* Create a memory BIO */
    bio = BIO_push( b64, bio ); /* Chain the Base64 filter and memory BIO */

    /* Write the signature into the BIO (this will be Base64 encoded) */
    BIO_write( bio, signature, signature_len );
    BIO_flush( bio ); /* Ensure all data is written */

    /* Get a pointer to the memory buffer that contains the Base64-encoded data */
    BIO_get_mem_ptr( bio, &bufferPtr );

    /* Allocate memory for the Base64-encoded text and copy it */
    char* b64text = (char*) malloc( bufferPtr->length + 1 );
    memcpy( b64text, bufferPtr->data, bufferPtr->length );
    b64text[bufferPtr->length] = '\0'; /* Null-terminate the string */

    /* Free the BIOs */
    BIO_free_all( bio );

    /* Generate a QR code from the Base64-encoded signature */
    QRcode* qrcode = QRcode_encodeString8bit( b64text, 0, QR_ECLEVEL_L );

    if( !qrcode )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to generate QR code" );
        free( b64text );
        return;
    }

    /* Scale the QR code */
    int qr_width = qrcode->width;
    int scale = 10; /* Increase scaling factor to improve resolution */
    int img_width = qr_width * scale;
    int img_height = qr_width * scale;

    /* Allocate memory for the image buffer */
    unsigned char* img_data = malloc( img_width * img_height );
    if( !img_data )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to allocate memory for QR code image" );
        QRcode_free( qrcode );
        free( b64text );
        return;
    }

    /* Initialize the image data (white background) */
    memset( img_data, 255, img_width * img_height );

    /* Copy QR code data into the image buffer */
    for( int y = 0; y < qr_width; y++ )
    {
        for( int x = 0; x < qr_width; x++ )
        {
            if( qrcode->data[y * qr_width + x] & 1 )
            {
                /* Set the corresponding pixels to black */
                for( int dy = 0; dy < scale; dy++ )
                {
                    for( int dx = 0; dx < scale; dx++ )
                    {
                        int img_x = x * scale + dx;
                        int img_y = y * scale + dy;
                        img_data[img_y * img_width + img_x] = 0;
                    }
                }
            }
        }
    }

    /* Write the image data to PNG format in memory */
    int png_len;
    unsigned char* png_data = stbi_write_png_to_mem( img_data, img_width, img_width, img_height, 1, &png_len );

    if( !png_data )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write QR code image to PNG format" );
        free( img_data );
        QRcode_free( qrcode );
        free( b64text );
        return;
    }

    /* Add a new page to display the signature */
    struct pdf_object* signature_page = pdf_append_page( pdf );

    /* Create header and footer for the signature page */
    char page_title[50];
    snprintf( page_title, sizeof( page_title ), "Signature" );
    create_header_and_footer( c, page_title );

    /* Add the QR code image to the PDF */
    pdf_add_image_data( pdf, NULL, 50, 200, 200, 200, png_data, png_len );

    /* Optionally: Add the Base64-encoded signature as text */
    pdf_set_font( pdf, "Courier" );
    pdf_add_text( pdf, NULL, "Digital Signature (Base64):", 12, 50, 600, PDF_BLACK );
    pdf_add_text_wrap( pdf, NULL, b64text, 10, 50, 580, PDF_BLACK, 500, PDF_ALIGN_LEFT, &height );

    /* Free resources */
    free( png_data );
    free( img_data );
    QRcode_free( qrcode );
    free( b64text );
}
