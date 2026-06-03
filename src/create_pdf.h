#ifndef CREATE_PDF_H_
#define CREATE_PDF_H_
/*.
 *  create_pdf.h: The header file for the pdf creation routines
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

#define MAX_PDF_FOOTER_TEXT_LENGTH 100
#define MAX_PDF_TAG_LENGTH 40

#define MODEL_HEADER_LENGTH 55
#define SERIAL_HEADER_LENGTH 35
#define BARCODE_LENGTH 100

#define STATUS_ICON_GREEN_TICK 1
#define STATUS_ICON_YELLOW_EXCLAMATION 2
#define STATUS_ICON_RED_CROSS 3

#define PDF_TYPE_SINGLE_DISC 1
#define PDF_TYPE_MULTI_DISC 2

#define PDF_PAGE_SMART_DATA 1
#define PDF_PAGE_ERASURE_DATA 2

#define LEFT_MARGIN_TEXT 60
#define LEFT_MARGIN_SMART_DATA 50

#define INTENTIONALLY_BLANK_X 150
#define INTENTIONALLY_BLANK_Y 400
#define INTENTIONALLY_BLANK_TEXT_SIZE 18

#define TOP_OF_TEXT_WINDOW_Y 630
#define START_OF_SMART_DATA_TEXT_Y_MULTIDISC 608

#define TEXT_SIZE_DATA 10

/* Additional colors that supplement the standard colors in pdfgen.h
 */
/*! Utility macro to provide gray */
#define PDF_DARK_GREEN PDF_RGB( 0, 0x64, 0 )

/*! Utility macro to provide gray */
#define PDF_GRAY PDF_RGB( 0x5A, 0x5A, 0x5A )

/*! Utility macro to provide gray */
#define PDF_YELLOW PDF_RGB( 0xFF, 0xFF, 0x5A )

/**
 * Create the disk erase report in PDF format
 * @param pointer to a drive context
 * @return returns 0 on success < 1 on error
 */
int create_single_disc_pdf( nwipe_thread_data_ptr_t*, nwipe_context_t* ptr );

int nwipe_get_smart_data( nwipe_misc_thread_data_t*, size_t, size_t*, nwipe_context_t* );

void pdf_header_footer_text( nwipe_misc_thread_data_t*, nwipe_context_t*, char*, size_t, size_t );

/**
 * Create the disk erase report for system/multiple disk
 * (all disks being wiped in one in PDF format
 * @param pointer to a drive context
 * @return returns 0 on success < 1 on error
 */
int create_system_multi_disc_pdf( nwipe_thread_data_ptr_t* ptrx );

/**
 * Size (Apparent)
 * Determines whether the text that shows the apparent disc size
 * should be red or green. The text is red if hidden sectors are
 * detected, green if no hidden sectors or the device doesn't support
 * HPA such as NVMe.
 * @param pointer to a drive context
 * @return returns to the RGB color, red or green
 */
uint32_t determine_color_for_size_apparent( nwipe_context_t* );

void pdf_add_text_size_real( float xoff, float yoff, nwipe_context_t* c );

void pdf_add_text_bytes_erased( float xoff, float yoff, nwipe_context_t* c );

void pdf_add_text_prng_type( float xoff, float yoff, uint32_t colour );
/**
 *  Print status of erasure text and ellipse
 *  Automatically determines text and ellipse color
 *  based on the status of erasure.
 *  @param text x offset
 *  @param text y offset
 *  @param ellipse x offset
 *  @param ellipse y offset
 *  @param ellipse x radius
 *  @param ellipse y radius
 *  @param text rotation angle of text in radians
 *  @param pointer to a drive context
 *  @return
 */
void pdf_add_text_status_of_erasure( float, float, float, float, float, float, float, nwipe_context_t* c );

/**
 * Display the wipe status icon, green tick, red cross, yellow exclamation.
 * @param flag indicating whether this is for a single disc PDF or a system multidisc PDF.
 * flag defined by PDF_TYPE_SINGLE_DISC and PDF_TYPE_MULTI_DISC. The icon displayed differs
 * depending on the type of PDF. A multi disc PDF requires all drives to have been
 * succesfully erased before a green tick is displayed in the top right corner.
 */
void pdf_display_status_icon( size_t, void* );

void pdf_add_text_blanking( float, float, float );

void pdf_add_text_verify( float, float, float );

void pdf_add_text_rounds( float, float, float, nwipe_context_t* );

void pdf_add_text_hpa_size( float, float, float, nwipe_context_t* );

void pdf_add_text_hpa_status( float, float, float, nwipe_context_t* );

struct pdf_object* pdf_append_page_and_update_index( void*, size_t );

/**
 * write the SMBIOS/DMI information to a new page.
 * @param pointer to PDF document
 * @param pointer to page number
 * @param text xoffset
 * @param text yoffset
 * @param PDF type, PDF_TYPE_SINGLE_DISC or PDF_TYPE_MULTI_DISC
 * @param pointer to drive context structure
 * @param pointer to miscellaneous data structure
 * @return
 */
void pdf_add_text_host_info_page( void*,
                                  size_t*,
                                  float,
                                  float,
                                  size_t,
                                  nwipe_context_t* c,
                                  nwipe_misc_thread_data_t* d );
/**
 * Insert an intenionally blank page for duplex printing
 * @param pointer to PDF document
 * @param pointer to page number
 * @param text xoffset
 * @param text yoffset
 * @param PDF type, PDF_TYPE_SINGLE_DISC or PDF_TYPE_MULTI_DISC
 * @param pointer to drive context structure
 * @param pointer to miscellaneous data structure
 * @return
 */
void pdf_add_blank_page( void*, size_t*, float, float, size_t, nwipe_context_t* c, nwipe_misc_thread_data_t* d );

#endif /* CREATE_PDF_H_ */
