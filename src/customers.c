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

    /* Validate csv contents. With the exception of line feeds,
     * remove non printable characters and move to a secondary buffer.
     */
    idx = 0;
    idx2 = 0;
    while( idx < size )
    {
        if( ( raw_buffer[idx] > 0x20 && raw_buffer[idx] < 0x7F ) || raw_buffer[idx] == 0x0A )
        {
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

    nwipe_log( NWIPE_LOG_INFO, "Line selected = %d", selected_entry );

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

    nwipe_log( NWIPE_LOG_INFO, "Line selected = %d", selected_entry );
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

    FILE* fptr;

    size_t result_size;

    /* Length of the new customer line */
    int csv_line_length;

    struct stat st;

    extern char nwipe_customers_file[];

    intmax_t existing_file_size = 0;

    /* pointer to the new customer entry in csv format. */
    char* csv_buffer = 0;

    /* pointer to the buffer containing the existing customer file */
    char* customers_buffer = 0;

    /* pointer to the buffer containing the existing customer file plus the new entry */
    char* new_customers_buffer = 0;

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
            if( !( new_customers_buffer = calloc( 1, existing_file_size + csv_line_length + 1 == 0 ) ) )
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

                /* Read the customers.csv file and populate the list array with the data */
                if( ( result_size = fread( customers_buffer, existing_file_size, 1, fptr ) ) != 1 )
                {
                    nwipe_log( NWIPE_LOG_ERROR,
                               "func:nwipe_gui_add_customer:Error reading customers file, # bytes read not as expected "
                               "%i bytes",
                               result_size );
                }
                else
                {
                    /* Read the first line of the existing customer buffer & write to the new buffer */
                }
            }
        }
    }
    free( csv_buffer );
    free( customers_buffer );
    free( new_customers_buffer );
}
