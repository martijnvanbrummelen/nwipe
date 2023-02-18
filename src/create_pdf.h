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

#ifndef CREATE_PDF_H_
#define CREATE_PDF_H_

#define MAX_PDF_FOOTER_TEXT_LENGTH 50

/* Additional colors that supplement the standard colors in pdfgen.h
 */
/*! Utility macro to provide gray */
#define PDF_DARK_GREEN PDF_RGB( 0, 0x64, 0 )

/*! Utility macro to provide gray */
#define PDF_GRAY PDF_RGB( 0x5A, 0x5A, 0x5A )

/**
 * Create the disk erase report in PDF format
 * @param pointer to a drive context
 * @return returns 0 on success < 1 on error
 */
int create_pdf( nwipe_context_t* ptr );

/**
 * Scan a string and replace any characters that are not alpha-numeric with
 * the character_char.
 * Example:
 * char str[] = 18:21:56;
 * calling the function replace_non_alphanumeric( &str, '_' )
 * would result in str changing from 18:21:56 to 18_21_56
 * @param character pointer to the string to be processed
 * @param replacement_char the character used to replace non alpha-numeric characters
 * @return void
 */
void replace_non_alphanumeric( char* str, char replacement_char );

#endif /* CREATE_PDF_H_ */
