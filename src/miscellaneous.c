/*
 *  miscellaneous.c: functions that may be generally used throughout nwipes code,
 *  mainly string processing functions but also time related functions.
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

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <stdio.h>
#include "nwipe.h"
#include "context.h"
#include "logging.h"
#include "miscellaneous.h"

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

void strip_CR_LF( char* str )
{
    /* In the specified string, replace any CR or LF with a space */
    int idx = 0;
    int len = strlen( str );
    while( idx < len )
    {
        if( str[idx] == 0x0A || str[idx] == 0x0D )
        {
            str[idx] = ' ';
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

void Determine_C_B_nomenclature( u64 qty, char* result, int result_array_size )
{

    /* C_B ? Determine Capacity or Bandwidth nomenclature
     *
     * A pointer to a result character string with a minimum of 13 characters in length
     * should be provided.
     *
     * Outputs a string of the form xxxTB, xxxGB, xxxMB, xxxKB B depending on the value of 'qty'
     */

    /* Initialise the output array */
    int idx = 0;

    while( idx < result_array_size )
    {
        result[idx++] = 0;
    }

    /* Determine the size of throughput so that the correct nomenclature can be used */
    if( qty >= INT64_C( 1000000000000 ) )
    {
        snprintf( result, result_array_size, "%3llu TB", qty / INT64_C( 1000000000000 ) );
    }
    else if( qty >= INT64_C( 1000000000 ) )
    {
        snprintf( result, result_array_size, "%3llu GB", qty / INT64_C( 1000000000 ) );
    }
    else if( qty >= INT64_C( 1000000 ) )
    {
        snprintf( result, result_array_size, "%3llu MB", qty / INT64_C( 1000000 ) );
    }
    else if( qty >= INT64_C( 1000 ) )
    {
        snprintf( result, result_array_size, "%3llu KB", qty / INT64_C( 1000 ) );
    }
    else
    {
        snprintf( result, result_array_size, "%3llu B", qty / INT64_C( 1 ) );
    }
}

void convert_seconds_to_hours_minutes_seconds( u64 total_seconds, int* hours, int* minutes, int* seconds )
{
    /* Convert binary seconds into binary hours, minutes and seconds */

    if( total_seconds % 60 )
    {
        *minutes = total_seconds / 60;

        *seconds = total_seconds - ( *minutes * 60 );
    }
    else
    {
        *minutes = total_seconds / 60;

        *seconds = 0;
    }
    if( *minutes > 59 )
    {
        *hours = *minutes / 60;
        if( *minutes % 60 )
        {
            *minutes = *minutes - ( *hours * 60 );
        }
        else
        {
            *minutes = 0;
        }
    }
}

int nwipe_strip_path( char* output, char* input )
{
    /* Take the input string, say "/dev/sda" and remove the "/dev/", prefix the result
     * with 'length' spaces. So if length=8 and input=/dev/sda, output will
     * be "     sda", a string 8 characters long right justified with spaces.
     */
    int idx_dest;
    int idx_src;
    idx_dest = 8;
    // idx_dest = length;
    output[idx_dest--] = 0;
    idx_src = strlen( input );
    idx_src--;

    while( idx_dest >= 0 )
    {
        /* if the device name contains a / start prefixing spaces */
        if( input[idx_src] == '/' )
        {
            output[idx_dest--] = ' ';
            continue;
        }
        if( idx_src >= 0 )
        {
            output[idx_dest--] = input[idx_src--];
        }
    }
    return 0;
}

void replace_non_alphanumeric( char* str, char replacement_char )
{
    int i = 0;
    while( str[i] != 0 )
    {
        if( str[i] < '0' || ( str[i] > '9' && str[i] < 'A' ) || ( str[i] > 'Z' && str[i] < 'a' ) || str[i] > 'z' )
        {
            str[i] = replacement_char;
        }
        i++;
    }
}

void convert_double_to_string( char* output_str, double value )
{
    int idx = 0;
    int idx2;
    int idx3 = 0;

    char percstr[512] = "";

    snprintf( percstr, sizeof( percstr ), "%5.32lf", value );

    while( percstr[idx] != 0 )
    {
        if( percstr[idx] == '.' )
        {
            for( idx2 = 0; idx2 < 3; idx2++ )
            {
                output_str[idx3++] = percstr[idx++];
            }
            break;
        }
        output_str[idx3++] = percstr[idx++];
    }
    output_str[idx3] = 0;
}

int read_system_datetime( char* year, char* month, char* day, char* hours, char* minutes, char* seconds )
{
    /* Reads system date & time and populates the caller provided strings.
     * Each string is null terminated by this function. The calling program
     * must provide the minimum string sizes as shown below.
     *
     * year 5 bytes (4 numeric digits plus NULL terminator)
     * month 3 bytes (2 numeric digits plus NULL terminator)
     * day 3 bytes (2 numeric digits plus NULL terminator)
     * hours 3 bytes (2 numeric digits plus NULL terminator)
     * minutes 3 bytes (2 numeric digits plus NULL terminator)
     * seconds 3 bytes (2 numeric digits plus NULL terminator)
     *
     * return value:
     * 0 = success
     * -1 = Failure, see nwipe log for detail.
     */
    FILE* fp;
    int r;  // A result buffer.
    int idx;  // general index
    int status = 0;

    /**
     * Obtain the year
     */
    fp = popen( "date +%Y", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system year using commmand = date +%Y" );
    }
    else
    {
        /* Read the first line and validate it. Should be 4 numeric digits */
        if( fgets( year, FOUR_DIGITS + 1, fp ) != NULL )
        {
            idx = 0;
            while( idx < 4 )
            {
                if( year[idx] >= '0' && year[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log error */
                    year[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system year using command = date +%Y, but result appears invalid = %s",
                               year );
                    status = -1;
                    break;
                }
            }
            year[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    /**
     * Obtain the month
     */
    fp = popen( "date +%m", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system month using the command = date +%m" );
    }
    else
    {
        /* Read the first line and validate it. Should be 2 numeric digits */
        if( fgets( month, TWO_DIGITS + 1, fp ) != NULL )
        {
            idx = 0;
            while( idx < 2 )
            {
                if( month[idx] >= '0' && month[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log error */
                    month[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system month using command = date +%m, but result appears invalid = %s",
                               month );
                    status = -1;
                    break;
                }
            }
            month[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    /**
     * Obtain the day
     */
    fp = popen( "date +\%d", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system day using the command = date +\%d" );
    }
    else
    {
        /* Read the first line and validate it. Should be 2 numeric digits */
        if( fgets( day, TWO_DIGITS + 1, fp ) != NULL )
        {
            idx = 0;
            while( idx < 2 )
            {
                if( day[idx] >= '0' && day[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log error */
                    day[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system day using command = date +\%d, but result appears invalid = %s",
                               day );
                    status = -1;
                    break;
                }
            }
            day[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    /**
     * Obtain the hours
     */
    fp = popen( "date +%H", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system hour using the command = date +%H" );
    }
    else
    {
        /* Read the first line and validate it. Should be 2 numeric digits */
        if( fgets( hours, TWO_DIGITS + 1, fp ) != NULL )
        {
            // nwipe_log( NWIPE_LOG_INFO, "Seconds = %s, Year = %s", seconds, year);
            idx = 0;
            while( idx < 2 )
            {
                if( hours[idx] >= '0' && hours[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log error */
                    hours[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system hours using command = date +%H, but result appears invalid = %s",
                               hours );
                    status = -1;
                    break;
                }
            }
            hours[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    /**
     * Obtain the minutes
     */
    fp = popen( "date +%M", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system minutes using the command = date +%M" );
    }
    else
    {
        /* Read the first line and validate it. Should be 2 numeric digits */
        if( fgets( minutes, TWO_DIGITS + 1, fp ) != NULL )
        {
            // nwipe_log( NWIPE_LOG_INFO, "Seconds = %s, Year = %s", seconds, year);
            idx = 0;
            while( idx < 2 )
            {
                if( minutes[idx] >= '0' && minutes[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log the error */
                    minutes[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system minutes using command = date +%H, but result appears invalid = %s",
                               minutes );
                    status = -1;
                    break;
                }
            }
            minutes[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    /**
     * Obtain the seconds
     */
    fp = popen( "date +%S", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to obtain system seconds using the command = date +%S" );
    }
    else
    {
        /* Read the first line and validate it. Should be 2 numeric digits */
        if( fgets( seconds, TWO_DIGITS + 1, fp ) != NULL )
        {
            // nwipe_log( NWIPE_LOG_INFO, "Seconds = %s, Year = %s", seconds, year);
            idx = 0;
            while( idx < 2 )
            {
                if( seconds[idx] >= '0' && seconds[idx] <= '9' )
                {
                    idx++;
                }
                else
                {
                    /* if we haven't reached the correct number of digits due to invalid data, log error */
                    seconds[++idx] = 0; /* terminate the string, prior to using in nwipe_log */
                    nwipe_log( NWIPE_LOG_ERROR,
                               "Obtained system seconds using command = date +%S, but result appears invalid = %s",
                               seconds );
                    status = -1;
                    break;
                }
            }
            seconds[idx] = 0; /* terminate the string */
        }
        r = pclose( fp );
    }

    return status;
}
