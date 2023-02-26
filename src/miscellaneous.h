/*
 *  miscellaneous.h: header file for miscellaneous.c ..
 *
 *  functions that may be generally used throughout nwipes code,
 *  mainly string processing related functions.
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

#ifndef MISCELLANEOUS_H_
#define MISCELLANEOUS_H_

/**
 * Convert the string from lower to upper case
 * @param pointer to a null terminated string
 * @return void
 */
void strupper( char* );

/**
 * Convert the string from upper to lower case
 * @param pointer to a null terminated string
 * @return void
 */
void strlower( char* str );

/**
 * Search a string for a positive number, convert the first
 * number found to binary and return the binary number.
 * returns the number or -1 if number too large or -2 if
 * no number found.
 *
 * @param pointer to a null terminated string
 * @return longlong returns:
 * the number
 * -1 = number too large
 * -2 = no number found.
 */
u64 str_ascii_number_to_ll( char* );

#endif /* HPA_DCO_H_ */
