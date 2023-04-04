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
 * @param char* pointer to the string to be processed
 * @param char the character used to replace non alpha-numeric characters
 * @return void
 */
void replace_non_alphanumeric( char*, char );

/**
 * I found this function necessary when converting a double of say
 * 99.999999999999999999 to text using printf. I only wanted 99.99
 * printed but if you specified a precision of %.2f in printf i.e 2 digits
 * after the decimal point you get 100.00 and not 99.99 If you increase
 * the precision to %.10f then you get 99.9999999999 but I only want
 * two significant digits displayed.i.e 99.99% not 100%
 * So this function converts to double retaining sufficient precision
 * so that a 30TB disc with one hidden sector will display as 99.99% erased
 * As an example if the double value to be converted is 99.999999999999999987
 * this function will always output 99.99 unlike printf which outputs 100.00
 * @param char* pointer to the string we write our percentage to. Needs to be
 * a minimum of 7 bytes, i.e 100.00 plus null terminator.
 * @param double the floating point value to be converted to a string.
 * @return void
 */
void convert_double_to_string( char*, double );

#endif /* HPA_DCO_H_ */
