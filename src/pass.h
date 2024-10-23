/*
 *  pass.h: Routines that read and write patterns to block devices.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
 *
 *  Modifications to original dwipe Copyright Andy Beverley <andy@andybev.com>
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

#ifndef PASS_H_
#define PASS_H_

int nwipe_random_pass( nwipe_context_t* c );
int nwipe_random_verify( nwipe_context_t* c );
int nwipe_static_pass( nwipe_context_t* c, nwipe_pattern_t* pattern );
int nwipe_static_verify( nwipe_context_t* c, nwipe_pattern_t* pattern );

void test_functionn( int count, nwipe_context_t** c );

#endif /* PASS_H_ */
