/*
 * ****************************************************************************
 *  customers.h: Functions related to customer processing for the PDF erasure *
 *  certificate.                                                              *
 * ****************************************************************************
 *
 *  Copyright PartialVolume <https://github.com/PartialVolume>.
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CUSTOMERS_H_INCLUDED
#define CUSTOMERS_H_INCLUDED

void customer_processes( int );
void select_customers( int, char** );
void delete_customer( int, char** );
void add_customer();
void write_customer_csv_entry( char*, char*, char*, char* );
void delete_customer_csv_entry( int* );

typedef char* nwipe_customers_buffer_t;
typedef char** nwipe_customers_pointers_t;

#define SELECT_CUSTOMER 1
#define DELETE_CUSTOMER 2

#define LINEFEED 0x0A

#endif  // CUSTOMERS_H_INCLUDED
