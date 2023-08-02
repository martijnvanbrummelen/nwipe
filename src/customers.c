/*
 * ****************************************************************************
 *  customers.c: Functions related to customer processing for the PDF erasure *
 *  certificate.                                                              *
 * ****************************************************************************
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

#include <stdio.h>
#include "nwipe.h"
#include "context.h"
#include "gui.h"
#include "logging.h"
#include "conf.h"
#include "customers.h"
#include <sys/stat.h>

void customer_processes( int mode )
{
    /* This function reads the customers.csv file, counts the number of lines,
     * converts line feeds to NULL and constructs a array of pointers that point
     * to the variable length strings.
     *
     * Depending on the value of mode the pointer array is passed to either
     * the select_customers() or delete_customer() functions.
     */

    int idx;
    int idx2;
    FILE* fptr;

    struct stat st;
    intmax_t size = 0;
    int lines;
    int list_idx;
    int current_list_size;

    size_t result_size;

    extern char nwipe_customers_file[];

    /* Determine size of customers.csv file */
    stat( nwipe_customers_file, &st );
    size = st.st_size;
    current_list_size = 0;

    nwipe_customers_buffer_t raw_buffer = (nwipe_customers_buffer_t) calloc( 1, size + 1 );

    /* Allocate storage for the contents of customers.csv */
    nwipe_customers_buffer_t buffer = (nwipe_customers_buffer_t) calloc( 1, size + 1 );

    /* Allocate storage for the processed version of customers.csv,
     * i.e we convert the csv format to strings without the quotes
     * and semi colon delimiters
     */
    nwipe_customers_pointers_t list = (nwipe_customers_pointers_t) calloc( 1, sizeof( char* ) );
    current_list_size += sizeof( char* );

    /* Open customers.csv */
    if( ( fptr = fopen( nwipe_customers_file, "rb" ) ) == NULL )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Unable to open %s", nwipe_customers_file );
        free( buffer );
        free( *list );
        return;
    }

    /* Read the customers.csv file and populate the list array with the data */
    result_size = fread( raw_buffer, size, 1, fptr );

    fclose( fptr );

    /* Validate csv contents. With the exception of line feeds,
     * remove non printable characters and move to a secondary buffer.
     */
    idx = 0;
    idx2 = 0;
    while( idx < size )
    {
        if( ( raw_buffer[idx] > 0x1F && raw_buffer[idx] < 0x7F ) || raw_buffer[idx] == 0x0A )
        {
            /* copy printable characters and line feeds */
            buffer[idx2++] = raw_buffer[idx];
        }
        idx++;
    }

    /* Construct a array of pointers that point to each line of the csv file
     */
    idx = 0;
    lines = 0;
    list_idx = 0;

    while( idx < size )
    {
        if( buffer[idx] == 0x0A )
        {
            buffer[idx] = 0;

            /* increment the line counter, but don't count
             * the first line as that is the csv header line.
             */
            if( idx != 0 )
            {
                lines++;

                /* Change the line feed to a NULL, string terminator */
                buffer[idx] = 0;

                /* Save the pointer to the first data line of the csv. */
                list[list_idx++] = &buffer[idx + 1];

                current_list_size += sizeof( char* );

                /* Expand allocated memory by the size of one pointer */
                if( ( list = realloc( list, current_list_size ) ) == NULL )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "Unable to realloc customer list array, out of memory?" );
                    break;
                }
                current_list_size += sizeof( char* );
            }
        }
        else
        {
            /* Replace colons with commas */
            if( buffer[idx] == ';' )
            {
                buffer[idx] = ',';
            }
        }
        idx++;
    }

    /* Sync lines variable to actual number of lines */
    if( lines > 0 )
        lines--;

    if( idx == size )
    {
        /* makesure the very last entry is NULL terminated */
        buffer[idx] = 0;
    }

    /* Select the requested mode, customer or delete customer.
     */
    switch( mode )
    {
        case SELECT_CUSTOMER:
            select_customers( lines, list );
            break;

        case DELETE_CUSTOMER:
            delete_customer( lines, list );
            break;
    }

    free( raw_buffer );
    free( buffer );
}

void select_customers( int count, char** customer_list_array )
{
    int selected_entry = 0;
    char window_title[] = " Select Customer For PDF Report ";

    /* Display the customer selection window */
    nwipe_gui_list( count, window_title, customer_list_array, &selected_entry );

    /* Save the selected customer details to nwipe's config file /etc/nwipe/nwipe.conf
     * If selected entry equals 0, then the customer did not select an entry so skip save.
     */
    if( selected_entry != 0 )
    {
        save_selected_customer( &customer_list_array[selected_entry - 1] );
    }
}

void delete_customer( int count, char** customer_list_array )
{
    char window_title[] = " Delete Customer ";
    int selected_entry = 0;

    nwipe_gui_list( count, window_title, customer_list_array, &selected_entry );

    delete_customer_csv_entry( &selected_entry );
}

void write_customer_csv_entry( char* customer_name,
                               char* customer_address,
                               char* customer_contact_name,
                               char* customer_contact_phone )
{
    /**
     * Write the attached strings in csv format to the first
     * line after the header (line 2 of file)
     */

    FILE* fptr = 0;
    FILE* fptr2 = 0;

    size_t result_size;

    /* General index variables */
    int idx1, idx2, idx3;

    /* Length of the new customer line */
    int csv_line_length;

    /* Size of the new buffer that holds old contents plus new entry */
    int new_customers_buffer_size;

    struct stat st;

    extern char nwipe_customers_file[];
    extern char nwipe_customers_file_backup[];
    extern char nwipe_customers_file_backup_tmp[];

    intmax_t existing_file_size = 0;

    /* pointer to the new customer entry in csv format. */
    char* csv_buffer = 0;

    /* pointer to the buffer containing the existing customer file */
    char* customers_buffer = 0;

    /* pointer to the buffer containing the existing customer file plus the new entry */
    char* new_customers_buffer = 0;

    size_t new_customers_buffer_length;

    /* Determine length of all four strings and malloc sufficient storage + 12 = 8 quotes + three colons + null */
    csv_line_length = strlen( customer_name ) + strlen( customer_address ) + strlen( customer_contact_name )
        + strlen( customer_contact_phone ) + 12;
    if( !( csv_buffer = calloc( 1, csv_line_length == 0 ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:nwipe_gui_add_customer:csv_buffer, calloc returned NULL " );
    }
    else
    {
        /* Determine current size of the csv file containing the customers */
        stat( nwipe_customers_file, &st );
        existing_file_size = st.st_size;

        /* calloc sufficient storage to hold the existing customers file */
        if( !( customers_buffer = calloc( 1, existing_file_size + 1 ) ) )
        {
            nwipe_log( NWIPE_LOG_ERROR, "func:nwipe_gui_add_customer:customers_buffer, calloc returned NULL " );
        }
        else
        {
            /* create a third buffer which is the combined size of the previous two, i.e existing file size, plus the
             * new customer entry + 1 (NULL) */
            new_customers_buffer_size = existing_file_size + csv_line_length + 1;

            if( !( new_customers_buffer = calloc( 1, new_customers_buffer_size ) ) )
            {
                nwipe_log( NWIPE_LOG_ERROR, "func:nwipe_gui_add_customer:customers_buffer, calloc returned NULL " );
            }
            else
            {
                /* Read the whole of customers.csv file into customers_buffer */
                if( ( fptr = fopen( nwipe_customers_file, "rb" ) ) == NULL )
                {
                    nwipe_log( NWIPE_LOG_ERROR, "Unable to open %s", nwipe_customers_file );
                }
                else
                {
                    /* Read the customers.csv file and populate the list array with the data */
                    if( ( result_size = fread( customers_buffer, existing_file_size, 1, fptr ) ) != 1 )
                    {
                        nwipe_log(
                            NWIPE_LOG_ERROR,
                            "func:nwipe_gui_add_customer:Error reading customers file, # bytes read not as expected "
                            "%i bytes",
                            result_size );
                    }
                    else
                    {
                        /* --------------------------------------------------------------------
                         * Read the first line which is the csv header from the existing customer
                         * buffer & write to the new buffer.
                         */

                        idx1 = 0;  // Index for the current csv buffer
                        idx2 = 0;  // Index for the new csv buffer
                        idx3 = 0;  // Index for new customer fields

                        while( idx1 < existing_file_size && idx2 < new_customers_buffer_size )
                        {
                            if( customers_buffer[idx1] != LINEFEED )
                            {
                                new_customers_buffer[idx2++] = customers_buffer[idx1++];
                            }
                            else
                            {
                                new_customers_buffer[idx2++] = LINEFEED;
                                break;
                            }
                        }

                        /* --------------------------------------------------------------------------
                         * Copy the new customer name entry so it is immediately after the csv header
                         */

                        /* Start with first entries opening quote */
                        new_customers_buffer[idx2++] = '"';

                        /* Copy the customer_name string */
                        while( idx3 < FIELD_LENGTH && idx2 < new_customers_buffer_size && customer_name[idx3] != 0 )
                        {
                            new_customers_buffer[idx2++] = customer_name[idx3++];
                        }

                        /* Close customer name field with a quote */
                        new_customers_buffer[idx2++] = '"';

                        /* Insert field delimiters, we use a semi-colon, not a comma ';' */
                        new_customers_buffer[idx2++] = ';';

                        /* -----------------------------------------------------------------------------
                         * Copy the new customer address entry so it is immediately after the csv header
                         */

                        idx3 = 0;

                        /* Start with first entries opening quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Copy the customer_name string */
                        while( idx3 < FIELD_LENGTH && idx2 < new_customers_buffer_size && customer_address[idx3] != 0 )
                        {
                            new_customers_buffer[idx2++] = customer_address[idx3++];
                        }

                        /* Close customer name field with a quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Insert field delimiters, we use a semi-colon, not a comma ';' */
                        new_customers_buffer[idx2++] = ';';

                        /* -----------------------------------------------------------------------------
                         * Copy the new customer contact name entry so it is immediately after the csv header
                         */

                        idx3 = 0;

                        /* Start with first entries opening quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Copy the customer_name string */
                        while( idx3 < FIELD_LENGTH && idx2 < new_customers_buffer_size
                               && customer_contact_name[idx3] != 0 )
                        {
                            new_customers_buffer[idx2++] = customer_contact_name[idx3++];
                        }

                        /* Close customer name field with a quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Insert field delimiters, we use a semi-colon, not a comma ';' */
                        new_customers_buffer[idx2++] = ';';

                        /* -----------------------------------------------------------------------------
                         * Copy the new customer contact phone entry so it is immediately after the csv header
                         */

                        idx3 = 0;

                        /* Start with first entries opening quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Copy the customer_name string */
                        while( idx3 < FIELD_LENGTH && idx2 < new_customers_buffer_size
                               && customer_contact_phone[idx3] != 0 )
                        {
                            new_customers_buffer[idx2++] = customer_contact_phone[idx3++];
                        }

                        /* Close customer name field with a quote */
                        new_customers_buffer[idx2++] = '\"';

                        /* Insert a line feed to finish the new entry */
                        new_customers_buffer[idx2++] = LINEFEED;

                        /* skip any LINEFEEDS in the existing customer entry as we just inserted one */
                        while( customers_buffer[idx1] != 0 && customers_buffer[idx1] == LINEFEED )
                        {
                            idx1++;
                        }

                        /* -------------------------------------------------------------------------------
                         * Now copy the existing customer entries, if any, immediately after the new entry
                         */

                        while( idx1 < existing_file_size && idx2 < new_customers_buffer_size )
                        {
                            /* Removes any nulls when copying and pasting, which would break this process? */
                            if( customers_buffer[idx1] == 0 )
                            {
                                while( idx1 < existing_file_size && customers_buffer[idx1] == 0 )
                                {
                                    idx1++;
                                }
                            }
                            new_customers_buffer[idx2++] = customers_buffer[idx1++];
                        }

                        /* Rename the customers.csv file to customers.csv.backup */
                        if( rename( nwipe_customers_file, nwipe_customers_file_backup_tmp ) != 0 )
                        {
                            nwipe_log( NWIPE_LOG_ERROR,
                                       "Unable to rename %s to %s",
                                       nwipe_customers_file,
                                       nwipe_customers_file_backup_tmp );
                        }
                        else
                        {
                            /* Create/open the customers.csv file */
                            if( ( fptr2 = fopen( nwipe_customers_file, "wb" ) ) == NULL )
                            {
                                nwipe_log( NWIPE_LOG_ERROR, "Unable to open %s", nwipe_customers_file );
                            }
                            else
                            {
                                /* write the new customers.csv file */
                                new_customers_buffer_length = strlen( new_customers_buffer );

                                if( ( result_size = fwrite(
                                          new_customers_buffer, sizeof( char ), new_customers_buffer_length, fptr2 ) )
                                    != new_customers_buffer_length )
                                {
                                    nwipe_log(
                                        NWIPE_LOG_ERROR,
                                        "func:write_customer_csv_entry:fwrite: Error result_size = %i not as expected",
                                        result_size );
                                }
                                else
                                {
                                    /* Remove the customer.csv.backup file if it exists */
                                    if( remove( nwipe_customers_file_backup ) != 0 )
                                    {
                                        nwipe_log(
                                            NWIPE_LOG_ERROR, "Unable to remove %s", nwipe_customers_file_backup_tmp );
                                    }
                                    else
                                    {
                                        /* Rename the customers.csv.backup.tmp file to customers.csv.backup */
                                        if( rename( nwipe_customers_file_backup_tmp, nwipe_customers_file_backup )
                                            != 0 )
                                        {
                                            nwipe_log( NWIPE_LOG_ERROR,
                                                       "Unable to rename %s to %s",
                                                       nwipe_customers_file,
                                                       nwipe_customers_file_backup_tmp );
                                        }
                                        nwipe_log( NWIPE_LOG_INFO,
                                                   "Succesfully write new customer entry to %s",
                                                   nwipe_customers_file );
                                    }
                                }
                                fclose( fptr2 );
                            }
                        }
                        fclose( fptr );
                    }
                }
                free( new_customers_buffer );
            }
            free( customers_buffer );
        }
        free( csv_buffer );
    }
}

void delete_customer_csv_entry( int* selected_entry )
{
    /**
     * Deletes a line from the csv file. The line to be deleted is determined
     * by the value of selected_entry
     */
    FILE* fptr = 0;
    FILE* fptr2 = 0;

    size_t result_size;

    struct stat st;

    extern char nwipe_customers_file[];
    extern char nwipe_customers_file_backup[];
    extern char nwipe_customers_file_backup_tmp[];

    intmax_t existing_file_size = 0;

    int linecount;

    /* General index variables */
    int idx1, idx2, idx3;

    /* pointer to the buffer containing the existing customer file */
    char* customers_buffer = 0;

    /* pointer to the buffer containing the existing customer minus the deleted entry */
    char* new_customers_buffer = 0;

    int status_flag = 0;

    size_t new_customers_buffer_length;

    /* Determine current size of the csv file containing the customers */
    stat( nwipe_customers_file, &st );
    existing_file_size = st.st_size;

    /* calloc sufficient storage to hold the existing customers file */
    if( !( customers_buffer = calloc( 1, existing_file_size + 1 ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "func:nwipe_gui_delete_customer_csv_entry:customers_buffer, calloc returned NULL " );
    }
    else
    {
        /* create a second buffer which is identical in size to the first, it will store the customer
         * csv file minus the one selected entry
         */

        if( !( new_customers_buffer = calloc( 1, existing_file_size + 1 ) ) )
        {
            nwipe_log( NWIPE_LOG_ERROR,
                       "func:nwipe_gui_delete_customer_csv_entry:customers_buffer, calloc returned NULL " );
        }
        else
        {
            /* Read the whole of customers.csv file into customers_buffer */
            if( ( fptr = fopen( nwipe_customers_file, "rb" ) ) == NULL )
            {
                nwipe_log( NWIPE_LOG_ERROR,
                           "func:nwipe_gui_delete_customer_csv_entry:Unable to open %s",
                           nwipe_customers_file );
            }
            else
            {
                /* Read the customers.csv file and populate the list array with the data */
                if( ( result_size = fread( customers_buffer, existing_file_size, 1, fptr ) ) != 1 )
                {
                    nwipe_log( NWIPE_LOG_ERROR,
                               "func:nwipe_gui_delete_customer_csv_entry:Error reading customers file, # elements read "
                               "not as expected "
                               "%i elements",
                               result_size );
                }
                else
                {
                    /* --------------------------------------------------------------------
                     * Read the first line which is the csv header from the existing customer
                     * buffer & write to the new buffer.
                     */

                    idx1 = 0;  // Index for the current csv buffer
                    idx2 = 0;  // Index for the new csv buffer

                    linecount = 1;  // count the lines in the csv file starting at line 1

                    while( idx1 < existing_file_size && idx2 < existing_file_size )
                    {
                        if( customers_buffer[idx1] != LINEFEED )
                        {
                            new_customers_buffer[idx2++] = customers_buffer[idx1++];
                        }
                        else
                        {
                            new_customers_buffer[idx2++] = customers_buffer[idx1++];
                            break;
                        }
                    }

                    /* -------------------------------------------------------------------------------
                     * Now copy the existing customer entries, counting the lines as we go and when we
                     * get to the the line selected for deletion we skip over it and then carry on
                     * copying.
                     */

                    while( idx1 < existing_file_size && idx2 < existing_file_size )
                    {
                        /* Don't copy nulls */
                        if( customers_buffer[idx1] == 0 )
                        {
                            idx1++;
                            continue;
                        }

                        /* Is this the line to delete? */
                        if( linecount == *selected_entry )
                        {
                            /* skip all the characters in this line */
                            while( idx1 < existing_file_size && customers_buffer[idx1] != LINEFEED )
                            {
                                idx1++;
                            }

                            /* skip the trailing linefeed if it exists, may not exist if last line */
                            if( customers_buffer[idx1] == LINEFEED )
                            {
                                idx1++;
                            }
                            linecount++;
                            nwipe_log( NWIPE_LOG_INFO, "Deleted customer entry from cache" );
                            status_flag = 1;
                        }
                        else
                        {
                            /* Is the character a LINEFEED? */
                            if( customers_buffer[idx1] == LINEFEED )
                            {
                                linecount++;
                            }

                            /* Copy a character */
                            new_customers_buffer[idx2++] = customers_buffer[idx1++];
                        }
                    }

                    /* Rename the customers.csv file to customers.csv.backup */
                    if( rename( nwipe_customers_file, nwipe_customers_file_backup_tmp ) != 0 )
                    {
                        nwipe_log( NWIPE_LOG_ERROR,
                                   "func:delete_customer_csv_entry:Unable to rename %s to %s",
                                   nwipe_customers_file,
                                   nwipe_customers_file_backup_tmp );
                    }
                    else
                    {
                        /* Create/open the customers.csv file */
                        if( ( fptr2 = fopen( nwipe_customers_file, "wb" ) ) == NULL )
                        {
                            nwipe_log( NWIPE_LOG_ERROR,
                                       "func:delete_customer_csv_entry:Unable to open %s",
                                       nwipe_customers_file );
                        }
                        else
                        {
                            /* write the new customers.csv file */
                            new_customers_buffer_length = strlen( new_customers_buffer );

                            if( ( result_size = fwrite(
                                      new_customers_buffer, sizeof( char ), new_customers_buffer_length, fptr2 ) )
                                != new_customers_buffer_length )
                            {
                                nwipe_log(
                                    NWIPE_LOG_ERROR,
                                    "func:delete_customer_csv_entry:fwrite: Error result_size = %i not as expected",
                                    result_size );
                            }
                            else
                            {
                                /* Remove the customer.csv.backup file if it exists */
                                if( remove( nwipe_customers_file_backup ) != 0 )
                                {
                                    nwipe_log( NWIPE_LOG_ERROR,
                                               "func:delete_customer_csv_entry:Unable to remove %s",
                                               nwipe_customers_file_backup_tmp );
                                }
                                else
                                {
                                    /* Rename the customers.csv.backup.tmp file to customers.csv.backup */
                                    if( rename( nwipe_customers_file_backup_tmp, nwipe_customers_file_backup ) != 0 )
                                    {
                                        nwipe_log( NWIPE_LOG_ERROR,
                                                   "func:delete_customer_csv_entry:Unable to rename %s to %s",
                                                   nwipe_customers_file,
                                                   nwipe_customers_file_backup_tmp );
                                    }
                                    if( status_flag == 1 )
                                    {
                                        nwipe_log(
                                            NWIPE_LOG_INFO, "Deleted customer entry in %s", nwipe_customers_file );
                                    }
                                    else
                                    {
                                        nwipe_log( NWIPE_LOG_INFO,
                                                   "Failed to delete customer entry in %s",
                                                   nwipe_customers_file );
                                    }
                                }
                            }
                            fclose( fptr2 );
                        }
                    }
                }
                fclose( fptr );
            }
            free( new_customers_buffer );
        }
        free( customers_buffer );
    }
}
