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
#define HPA_NOT_APPLICABLE 3
#define HPA_NOT_SUPPORTED_BY_DRIVE 4

int hpa_dco_status( nwipe_context_t* );

u64 nwipe_read_dco_real_max_sectors( char* );

typedef struct nwipe_sense_dco_identify_t_t_
{
    /* This struct contains some of the decoded fields from the sense data after a
     * ATA 0xB1 device configuration overlay command has been issued. We mainly
     * use it to decode the real max sectors
     */
    u64 dco_real_max_sectors;
} nwipe_sense_dco_identify_t_t_;

#endif /* HPA_DCO_H_ */
