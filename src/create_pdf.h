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

int create_pdf( nwipe_context_t* ptr );

#endif /* CREATE_PDF_H_ */
