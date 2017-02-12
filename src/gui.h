/*
 *  gui.h: An ncurses GUI for nwipe.
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


#ifndef GUI_H_
#define GUI_H_

void nwipe_gui_free( void );                             /* Stop the GUI.              */
void nwipe_gui_init( void );                             /* Start the GUI.             */
void nwipe_gui_select( int count, nwipe_context_t** c ); /* Select devices to wipe.    */
void *nwipe_gui_status( void *ptr );                     /* Update operation progress. */
void nwipe_gui_method( void );                           /* Change the method option.  */
void nwipe_gui_options( void );                          /* Update the options window. */
void nwipe_gui_prng( void );                             /* Change the prng option.    */
void nwipe_gui_rounds( void );                           /* Change the rounds option.  */
void nwipe_gui_verify( void );                           /* Change the verify option.  */
void nwipe_gui_noblank( void );                          /* Change the noblank option. */

int compute_stats(void *ptr);
void nwipe_update_speedring( nwipe_speedring_t* speedring, u64 speedring_done, time_t speedring_now );


#endif /* GUI_H_ */

/* eof */
