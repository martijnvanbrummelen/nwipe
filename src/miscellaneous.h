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

void Determine_C_B_nomenclature( u64, char*, int );
void convert_seconds_to_hours_minutes_seconds( u64, int*, int*, int* );
int nwipe_strip_path( char*, char* );

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

#endif /* HPA_DCO_H_ */
