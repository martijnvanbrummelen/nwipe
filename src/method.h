/*
 *  methods.c: Method implementations for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  Modifications to original dwipe Copyright Andy Beverley <andy@andybev.com>
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

#ifndef METHOD_H_
#define METHOD_H_

/* The argument list for nwipe methods. */
#define NWIPE_METHOD_SIGNATURE nwipe_context_t* c

typedef enum nwipe_verify_t_ {
    NWIPE_VERIFY_NONE = 0,  // Do not read anything back from the device.
    NWIPE_VERIFY_LAST,  // Check the last pass.
    NWIPE_VERIFY_ALL,  // Check all passes.
} nwipe_verify_t;

/* The typedef of the function that will do the wipe. */
typedef int ( *nwipe_method_t )( void* ptr );

typedef struct
{
    int length;  // Length of the pattern in bytes, -1 means random.
    char* s;  // The actual bytes of the pattern.
} nwipe_pattern_t;

const char* nwipe_method_label( void* method );
int nwipe_runmethod( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* patterns );

void* nwipe_dod522022m( void* ptr );
void* nwipe_dodshort( void* ptr );
void* nwipe_gutmann( void* ptr );
void* nwipe_ops2( void* ptr );
void* nwipe_is5enh( void* ptr );
void* nwipe_random( void* ptr );
void* nwipe_zero( void* ptr );
void* nwipe_one( void* ptr );
void* nwipe_verify_zero( void* ptr );
void* nwipe_verify_one( void* ptr );
void* nwipe_bruce7( void* ptr );
void* nwipe_bmb( void* ptr );
void* nwipe_secure_erase( void* ptr );
void* nwipe_secure_erase_prng_verify( void* ptr );
void* nwipe_sanitize_crypto_erase( void* ptr );
void* nwipe_sanitize_block_erase( void* ptr );
void* nwipe_sanitize_overwrite( void* ptr );

void calculate_round_size( nwipe_context_t* );

#endif /* METHOD_H_ */
