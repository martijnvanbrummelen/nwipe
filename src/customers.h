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

#ifndef CUSTOMERS_H_INCLUDED
#define CUSTOMERS_H_INCLUDED

void customer_processes( int );
void select_customers( int, char** );
void delete_customer();
void add_customer();
void write_customer_csv_entry( char*, char*, char*, char* );

typedef char* nwipe_customers_buffer_t;
typedef char** nwipe_customers_pointers_t;

#define SELECT_CUSTOMER 1
#define DELETE_CUSTOMER 2

#define LINEFEED 0x0A

#endif  // CUSTOMERS_H_INCLUDED
