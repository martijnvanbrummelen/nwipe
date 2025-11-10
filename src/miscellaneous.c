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
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>  // for isspace()
#include <stddef.h>  // for NULL

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
    if( qty >= INT64_C( 10000000000000 ) )
    {
        snprintf( result, result_array_size, "%4llu TB", qty / INT64_C( 1000000000000 ) );
    }
    else if( qty >= INT64_C( 10000000000 ) )
    {
        snprintf( result, result_array_size, "%4llu GB", qty / INT64_C( 1000000000 ) );
    }
    else if( qty >= INT64_C( 10000000 ) )
    {
        snprintf( result, result_array_size, "%4llu MB", qty / INT64_C( 1000000 ) );
    }
    else if( qty >= INT64_C( 10000 ) )
    {
        snprintf( result, result_array_size, "%4llu KB", qty / INT64_C( 1000 ) );
    }
    else
    {
        snprintf( result, result_array_size, "%4llu B", qty / INT64_C( 1 ) );
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

int write_system_datetime( char* year, char* month, char* day, char* hours, char* minutes, char* seconds )
{
    /* Writes the system date & time using data from the caller provided strings.
     * The calling program must provide the minimum string sizes as shown below
     * populated with current date and time data.
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
    int strIdx;  // Index into each string
    int bufferIdx;  // Index into the buffer
    char buffer[5];

    /**
     * Basic validation that confirms the input strings are numeric and of the correct length, we do this
     * by first constructing three arrays. The first are the names of the variables in order
     * year, month, day, hours, minutes and seconds. The second array contains the address of
     * each of those strings. The third array are the lengths.
     * This allows us to create a single loop to validate all fields.
     */

    char* names[] = { "year", "month", "day", "hours", "minutes", "seconds" };
    char* pdata[] = { year, month, day, hours, minutes, seconds };
    int lengths[] = { 4, 2, 2, 2, 2, 2 };
    char cmd_format[] = "date %s%s%s%s%s.%s >/dev/null 2>&1";
    char cmd[256];

    for( idx = 0; idx < 6; idx++ )
    {
        strIdx = 0;  // initialise string index

        /* check each characters is numeric */
        while( strIdx < lengths[idx] )
        {
            if( pdata[idx][strIdx] >= '0' && pdata[idx][strIdx] <= '9' )
            {
                strIdx++;
            }
            else
            {
                /* if we haven't reached the correct number of digits due to invalid data, log error,
                 * but first we read the valid data acquired so far into a buffer, this is done to avoid
                 * writing to the user provided string because if they did not size the string correctly
                 * writing a zero at the end could cause a segfault.
                 */

                for( bufferIdx = 0; bufferIdx < strIdx + 1; bufferIdx++ )
                {
                    buffer[bufferIdx] = pdata[idx][bufferIdx];
                }
                buffer[bufferIdx] = 0; /* terminate the string, prior to using in nwipe_log */

                /* A typical error will look like ..
                 * "User provided year data that appear invalid = 202£" */
                nwipe_log( NWIPE_LOG_ERROR, "User provided %s data that appears invalid = %s", names[idx], buffer );
                return -1;
            }
        }
    }

    /**
     * Now using the validated strings construct the date command that we will use to write the system date/time
     */
    sprintf( cmd, cmd_format, month, day, hours, minutes, year, seconds );

    /**
     * Run the date command to write the new date/time
     */

    fp = popen( cmd, "w" );
    r = pclose( fp );

    if( fp == NULL || r != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write system date/time using command = %s", cmd );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "Date/time succesfully writen to system using command = %s", cmd );
    }

    return 0;
}

void fix_endian_model_names( char* model )
{
    /* Some IDE USB adapters get the endian wrong, we can't determine the endian from the drive
     * as the drive standard doesn't provide that information, so we have to resort to matching
     * the model name against known strings with the endian incorrect, then reverse the endian.
     */

    int idx = 0;
    int idx2 = 0;
    unsigned int length = 0;
    char* tmp_string;
    char* model_lower_case;
    int swap_endian_flag = 0;

    length = strlen( model );

    tmp_string = calloc( length + 1, 1 );
    model_lower_case = calloc( length + 1, 1 );

    strncpy( model_lower_case, model, length );
    model_lower_case[length] = 0; /* makesure it's terminated */
    strlower( model_lower_case ); /* convert to lower case for comparison */

    /* "ASSMNU G" = "SAMSUNG ", tested against model Samsung HM160HC so that
     * "ASSMNU G MH61H0 C" becomes "SAMSUNG HM160HC ")
     */

    if( !( strncmp( model_lower_case, "assmnu g", 8 ) ) )
    {
        swap_endian_flag = 1;
    }
    else
    {
        /* Hitachi */
        if( !( strncmp( model_lower_case, "ihathc i", 8 ) ) )
        {
            swap_endian_flag = 1;
        }
        else
        {
            /* Toshiba */
            if( !( strncmp( model_lower_case, "othsbi a", 8 ) ) )
            {
                swap_endian_flag = 1;
            }
            else
            {
                /* WDC (Western Digital Corporation) */
                if( !( strncmp( model_lower_case, "dw c", 4 ) ) )
                {
                    swap_endian_flag = 1;
                }
                else
                {
                    /* Seagate */
                    if( !( strncmp( model_lower_case, "esgata e", 8 ) ) )
                    {
                        swap_endian_flag = 1;
                    }
                    else
                    {
                        /* Seagate models starting ST */
                        if( !( strncmp( model_lower_case, "ts", 2 ) ) )
                        {
                            swap_endian_flag = 1;
                        }
                        else
                        {
                            /* Sundisk - Sandisk SSD from Sun AXI */
                            if( !( strncmp( model_lower_case, "usdnsi k", 8 ) ) )
                            {
                                swap_endian_flag = 1;
                            }
                            else
                            {
                                /* Sandisk */
                                if( !( strncmp( model_lower_case, "asdnsi k", 8 ) ) )
                                {
                                    swap_endian_flag = 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if( swap_endian_flag == 1 )
    {
        while( model[idx] != 0 && idx < length )
        {
            if( model[idx + 1] != 0 )
            {
                /* Swap the bytes */
                tmp_string[idx2 + 1] = model[idx];
                tmp_string[idx2] = model[idx + 1];
            }
            else
            {
                /* Copy the last odd byte and exit while loop */
                tmp_string[idx2] = model[idx];
                break;
            }

            if( tmp_string[idx2 + 1] == ' ' && model[idx + 2] == ' ' )
            {
                idx++;
            }

            idx += 2;
            idx2 += 2;
        }

        tmp_string[length] = 0; /* terminate */
        strncpy( model, tmp_string, length );
        model[length] = 0; /* terminate */
    }
    free( tmp_string );
    free( model_lower_case );
}

/*
 * get_device_uuid - Find the UUID corresponding to a given block device
 * @device_path: e.g., "/dev/sda1"
 * @uuid_buf: pointer to a 40-byte buffer where the UUID string will be stored
 *
 * Returns 0 on success, -1 on failure.
 */
int get_device_uuid( const char* device_path, char* uuid_buf )
{
    const char* uuid_dir = "/dev/disk/by-uuid";
    struct dirent* entry;
    DIR* dir;
    char link_path[PATH_MAX];
    char resolved_path[PATH_MAX];
    ssize_t len;

    if( !device_path || !uuid_buf )
    {
        errno = EINVAL;
        return -1;
    }

    dir = opendir( uuid_dir );
    if( !dir )
    {
        perror( "opendir" );
        return -1;
    }

    while( ( entry = readdir( dir ) ) != NULL )
    {
        // Skip "." and ".."
        if( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 )
            continue;

        snprintf( link_path, sizeof( link_path ), "%s/%s", uuid_dir, entry->d_name );

        // readlink() doesn't null-terminate, so we must handle that
        len = readlink( link_path, resolved_path, sizeof( resolved_path ) - 1 );
        if( len == -1 )
            continue;  // skip unreadable links

        resolved_path[len] = '\0';

        // Construct full path (readlink gives a relative path, usually "../../sda1")
        char full_path[PATH_MAX + strlen( uuid_dir ) + 1];
        if( resolved_path[0] != '/' )
        {
            // Resolve relative symlink path
            snprintf( full_path, sizeof( full_path ), "%s/%s", uuid_dir, resolved_path );
            if( realpath( full_path, full_path ) == NULL )
            {
                closedir( dir );
                return -1;
            }
        }
        else
        {
            strncpy( full_path, resolved_path, sizeof( full_path ) );
            full_path[sizeof( full_path ) - 1] = '\0';
        }

        // Compare with the given device path
        if( strcmp( full_path, device_path ) == 0 )
        {
            strncpy( uuid_buf, entry->d_name, 40 );
            uuid_buf[39] = '\0';
            closedir( dir );
            return 0;
        }
    }

    closedir( dir );
    errno = ENOENT;
    return -1;  // not found
}

/**
 * find_base_device - searches /sys/block for a device that matches
 *                    the prefix of the provided name.
 *
 * @devname: the input device name (e.g. "sda1", "sdb2")
 * @result:  buffer to store the matched base device name
 * @size:    size of the result buffer
 *
 * Returns: 0 on success (match found), -1 on failure (no match or error)
 */
int find_base_device( const char* devname, char* result, size_t size )
{
    if( !devname || !result || size == 0 )
    {
        fprintf( stderr, "Invalid argument(s)\n" );
        return -1;
    }

    DIR* dir = opendir( "/sys/block" );
    if( !dir )
    {
        perror( "opendir" );
        return -1;
    }

    struct dirent* entry;
    int found = -1;

    while( ( entry = readdir( dir ) ) != NULL )
    {
        // Skip "." and ".."
        if( entry->d_name[0] == '.' )
            continue;

        // Check if the given devname starts with the block device name
        size_t len = strlen( entry->d_name );
        if( strncmp( devname, entry->d_name, len ) == 0 )
        {
            // Match found
            strncpy( result, entry->d_name, size - 1 );
            result[size - 1] = '\0';
            found = 0;
            break;
        }
    }

    closedir( dir );
    return found;
}

/**
 * skip_whitespace - Returns a pointer to the first non-whitespace character
 *                   in a given string.
 *
 * @str: input string
 *
 * Returns: pointer to the first non-whitespace character,
 *          or NULL if str is NULL or the string contains only whitespace.
 */
const char* skip_whitespace( const char* str )
{
    if( !str )
        return NULL;

    while( *str && isspace( (unsigned char) *str ) )
        str++;

    return *str ? str : NULL;
}
