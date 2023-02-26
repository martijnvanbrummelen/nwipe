/*
 *  miscellaneous.c: functions that may be generally used throughout nwipes code,
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

#include "nwipe.h"

/* Convert string to upper case
 */
void strupper( char* str )
{
    int idx;

    idx = 0;
    while( str[idx] != 0 )
    {
        /* If upper case alpha character, change to lower case */
        if( str[idx] >= 'A' && str[idx] <= 'Z' )
        {
            str[idx] -= 32;
        }

        idx++;
    }
}

/* Convert string to lower case
 */
void strlower( char* str )
{
    int idx;

    idx = 0;
    while( str[idx] != 0 )
    {
        /* If upper case alpha character, change to lower case */
        if( str[idx] >= 'A' && str[idx] <= 'Z' )
        {
            str[idx] += 32;
        }

        idx++;
    }
}

/* Search a string for a positive number, convert the first
 * number found to binary and return the binary number.
 * returns the number or -1 if number too large or -2 if
 * no number found.
 */

u64 str_ascii_number_to_ll( char* str )
{
    int idx;
    int idx2;
    char number_copy[20];

    idx = 0;  // index into the main string we are searching
    idx2 = 0;  // index used for backup copy of ascii number

    while( str[idx] != 0 )
    {
        /* Find the start of the number */
        if( str[idx] >= '0' && str[idx] <= '9' )
        {
            while( str[idx] != 0 )
            {
                /* Find the end of the number */
                if( str[idx] >= '0' && str[idx] <= '9' )
                {
                    if( idx2 < sizeof( number_copy ) - 1 )
                    {
                        number_copy[idx2++] = str[idx++];
                    }
                    else
                    {
                        /* Number is too large ! */
                        return -1;
                    }
                }
                else
                {
                    /* end found */
                    number_copy[idx2] = 0;  // terminate our copy

                    /* convert ascii number to longlong */
                    return atoll( number_copy );
                }
            }
        }
        else
        {
            idx++;
        }
    }
    return -2; /* no number found */
}
