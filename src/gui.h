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

void nwipe_gui_free( void );  // Stop the GUI.
void nwipe_gui_init( void );  // Start the GUI.
void nwipe_gui_create_main_window( void );  // Create the main window
void nwipe_gui_create_header_window( void );  // Create the header window
void nwipe_gui_create_footer_window( const char* );  // Create the footer window and write text
void nwipe_gui_create_options_window( void );  // Create the options window
void nwipe_gui_create_stats_window( void );  // Create the stats window
void nwipe_gui_create_all_windows_on_terminal_resize(
    int force_creation,
    const char* footer_text );  // If terminal is resized recreate all windows
void nwipe_gui_select( int count, nwipe_context_t** c );  // Select devices to wipe.
void* nwipe_gui_status( void* ptr );  // Update operation progress.
void nwipe_gui_method( void );  // Change the method option.
void nwipe_gui_options( void );  // Update the options window.
void nwipe_gui_prng( void );  // Change the prng option.
void nwipe_gui_rounds( void );  // Change the rounds option.
void nwipe_gui_verify( void );  // Change the verify option.
void nwipe_gui_noblank( void );  // Change the noblank option.
int spinner( nwipe_context_t** ptr, int );  // Return the next spinner character
void temp1_flash( nwipe_context_t* );  // toggles term1_flash_status, which flashes the temperature
void wprintw_temperature( nwipe_context_t* );

int compute_stats( void* ptr );
void nwipe_update_speedring( nwipe_speedring_t* speedring, u64 speedring_done, time_t speedring_now );

#define NOMENCLATURE_RESULT_STR_SIZE 8

#define GETCH_BLOCK_MS 250 /* millisecond block time for getch() */

#endif /* GUI_H_ */
