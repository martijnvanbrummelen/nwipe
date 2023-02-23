/*.
 *  hpa_dco.h: The header file for the host protected area (HPA) and
 *  disk configuration overlay routines
 *
 *  Copyright https://github.com/PartialVolume/shredos.x86_64
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
#ifndef HPA_DCO_H_
#define HPA_DCO_H_

#define HPA_DISABLED 0
#define HPA_ENABLED 1
#define HPA_UNKNOWN 2

#define PRE_WIPE_HPA_CHECK 0
#define POST_WIPE_HPA_CHECK 1

int hpa_dco_status( nwipe_context_t*, int );

#endif /* HPA_DCO_H_ */
