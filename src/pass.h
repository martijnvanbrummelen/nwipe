/*
 *  pass.h: Pass-related I/O routines.
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

#ifndef PASS_H_
#define PASS_H_

#include "context.h" /* nwipe_context_t */
#include "method.h" /* nwipe_pattern_t */

int nwipe_random_pass( nwipe_context_t* c );
int nwipe_random_verify( nwipe_context_t* c );
int nwipe_static_pass( nwipe_context_t* c, nwipe_pattern_t* pattern );
int nwipe_static_verify( nwipe_context_t* c, nwipe_pattern_t* pattern );

#endif /* PASS_H_ */
