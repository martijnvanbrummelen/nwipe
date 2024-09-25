/*
 *  create_pdf.h: The header file for the PDF creation routines
 *
 *  Copyright https://github.com/PartialVolume/shredos.x86_64
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

#ifndef CREATE_PDF_H_
#define CREATE_PDF_H_

#include "nwipe.h" /* Include nwipe context */
#include "PDFGen/pdfgen.h" /* Include PDFGen structures */

/* OpenSSL headers */
#include <openssl/evp.h>
#include <openssl/x509.h>

#define MAX_PDF_FOOTER_TEXT_LENGTH 100

#define STATUS_ICON_GREEN_TICK 1
#define STATUS_ICON_YELLOW_EXCLAMATION 2
#define STATUS_ICON_RED_CROSS 3

/* Additional colors that supplement the standard colors in pdfgen.h */
#define PDF_DARK_GREEN PDF_RGB( 0, 0x64, 0 )
#define PDF_GRAY PDF_RGB( 0x5A, 0x5A, 0x5A )
#define PDF_YELLOW PDF_RGB( 0xFF, 0xFF, 0x5A )

/**
 * Create the disk erase report in PDF format
 * @param ptr Pointer to a drive context
 * @return Returns 0 on success, < 1 on error
 */
int create_pdf( nwipe_context_t* ptr );

/**
 * Get SMART data and add it to the PDF
 * @param c Pointer to nwipe context
 * @return Returns 0 on success, non-zero on error
 */
int nwipe_get_smart_data( nwipe_context_t* c );

/**
 * Create header and footer for the PDF pages
 * @param c Pointer to nwipe context
 * @param page_title Title of the page
 */
void create_header_and_footer( nwipe_context_t* c, char* page_title );

/* New functions for OpenSSL integration */
/**
 * Generate a key pair and a self-signed certificate
 * @param pkey Pointer to store the generated EVP_PKEY
 * @param x509 Pointer to store the generated X509 certificate
 * @return Returns 1 on success, -1 on error
 */
int generate_key_and_certificate( EVP_PKEY** pkey, X509** x509 );

/**
 * Sign the PDF with the generated private key
 * @param pdf_filename Path to the PDF file
 * @param pkey Private key used for signing
 * @param signature Pointer to store the signature
 * @param signature_len Pointer to store the length of the signature
 * @return Returns 1 on success, -1 on error
 */
int sign_pdf( const char* pdf_filename, EVP_PKEY* pkey, unsigned char** signature, size_t* signature_len );

/**
 * Add the signature to the PDF
 * @param pdf Pointer to the PDF document
 * @param c Pointer to nwipe context
 * @param signature The signature data
 * @param signature_len Length of the signature data
 */
void add_signature_to_pdf( struct pdf_doc* pdf, nwipe_context_t* c, unsigned char* signature, size_t signature_len );

#endif /* CREATE_PDF_H_ */
