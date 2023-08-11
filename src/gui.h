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

/**
 * The primary user interface.  Allows the user to
 * change options and specify the devices to be wiped.
 *
 * @parameter  count       The number of contexts in the array.
 * @parameter  c           An array of device contexts.
 *
 * @modifies   c[].select  Sets the select flag according to user input.
 * @modifies   options     Sets program options according to to user input.
 *
 */
void nwipe_gui_select( int count, nwipe_context_t** c );  // Select devices to wipe.
void* nwipe_gui_status( void* ptr );  // Update operation progress.
void nwipe_gui_method( void );  // Change the method option.
void nwipe_gui_options( void );  // Update the options window.
void nwipe_gui_prng( void );  // Change the prng option.
void nwipe_gui_rounds( void );  // Change the rounds option.
void nwipe_gui_verify( void );  // Change the verify option.
void nwipe_gui_noblank( void );  // Change the noblank option.
void nwipe_gui_config( void );  // Change the nwipe settings
void nwipe_gui_edit_organisation( void );  // Edit organisation performing the erasure
void nwipe_gui_organisation_business_name( const char* );  // Edit business name performing erase
void nwipe_gui_organisation_business_address( const char* );  // Edit business address performing erase
void nwipe_gui_organisation_contact_name( const char* );  // Edit business contact name
void nwipe_gui_organisation_contact_phone( const char* );  // Edit business contact phone
void nwipe_gui_organisation_op_tech_name( const char* );  // Edit the name of the operator/technician
void nwipe_gui_list( int, char* window_title, char**, int* );
void nwipe_gui_add_customer( void );  // Add new customer
void nwipe_gui_add_customer_name( char* );  // Add new customer name
void nwipe_gui_add_customer_address( char* );  // Add new customer address
void nwipe_gui_add_customer_contact_name( char* );  // Add new customer contact name
void nwipe_gui_add_customer_contact_phone( char* );  // Add new customer contact phone
int nwipe_gui_yes_no_footer( void );  // Change footer to yes no
void nwipe_gui_preview_org_customer( void );  // Preview window  for wipe organisation and customer

/**
 * Truncate a string based on start position and terminal width
 *
 * @parameter wcols         Width of window, obtained from getmaxyx(..)
 * @parameter start_column  Start column where the string starts
 * @parameter input_string  The string to be truncated if necessary
 * @parameter ouput_string  The possibly truncated string
 * @parameter ouput_string_length   Max length of output string
 * @Return returns a pointer to the output string
 */
char* str_truncate( int, int, const char*, char*, int );  // Truncate string based on start column and terminal width
int spinner( nwipe_context_t** ptr, int );  // Return the next spinner character
void temp1_flash( nwipe_context_t* );  // toggles term1_flash_status, which flashes the temperature

/**
 * If the current drive temperature is available, print it to the GUI.
 * This function determines if the drive temperature limits are specified &
 * if so, whether the temperature should be printed as white text on blue if the
 * drive is operating within it's temperature specification or red text on
 * blue if the drive has exceeded the critical high temperature or black on
 * blue if the drive has dropped below the drives minimum temperature specification.
 * @param pointer to the drive context
 */
void wprintw_temperature( nwipe_context_t* );

int compute_stats( void* ptr );
void nwipe_update_speedring( nwipe_speedring_t* speedring, u64 speedring_done, time_t speedring_now );

#define NOMENCLATURE_RESULT_STR_SIZE 8

/* Note Do not change unless you understand how this value affects keyboard response and screen refresh when
 * the drive selection screen is displayed. (prior to wipe starting). */
#define GETCH_BLOCK_MS 250 /* millisecond block time for getch() */

/* Note The value of 1 (100ms) is the ideal speed for screen refresh during a wipe, a value of 2 is noticeably slower,
 * don't change unless you understand how this value affects keyboard responsiveness and speed of screen stats/spinner
 * updating */
#define GETCH_GUI_STATS_UPDATE_MS 1 /* 1 * 100 = 1/10/sec = millisecond block time for gui stats screen updates */

#define FIELD_LENGTH 256

#define YES 1
#define NO 0

#endif /* GUI_H_ */
