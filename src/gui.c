/*
 *  gui.c: An ncurses GUI for nwipe.
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

/* RATIONALE:
 *
 *   This entire GUI is a non-portable task-specific thunk.
 *
 *   The alternatives are, however, no better. The CDK is large and clumsy,
 *   and things like ncurses libmenu are not worth the storage overhead.
 *
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <time.h>

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <panel.h>
#include <stdint.h>
#include <libconfig.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "gui.h"
#include "pass.h"
#include "logging.h"
#include "version.h"
#include "temperature.h"
#include "miscellaneous.h"
#include "hpa_dco.h"
#include "customers.h"
#include "conf.h"

#define NWIPE_GUI_PANE 8

/* Header window: width, height, x coordinate, y coordinate. */
#define NWIPE_GUI_HEADER_W COLS
#define NWIPE_GUI_HEADER_H 1
#define NWIPE_GUI_HEADER_X 0
#define NWIPE_GUI_HEADER_Y 0

/* Footer window: width, height, x coordinate, y coordinate. */
#define NWIPE_GUI_FOOTER_W COLS
#define NWIPE_GUI_FOOTER_H 1
#define NWIPE_GUI_FOOTER_X 0
#define NWIPE_GUI_FOOTER_Y ( LINES - 1 )

/* Options window: width, height, x coorindate, y coordinate. */
#define NWIPE_GUI_OPTIONS_W 44
#define NWIPE_GUI_OPTIONS_H 7
#define NWIPE_GUI_OPTIONS_Y 1
#define NWIPE_GUI_OPTIONS_X 0

/* Options fields, relative to their window. */
#define NWIPE_GUI_OPTIONS_TAB 10
#define NWIPE_GUI_OPTIONS_ENTROPY_Y 1
#define NWIPE_GUI_OPTIONS_ENTROPY_X 1
#define NWIPE_GUI_OPTIONS_PRNG_Y 2
#define NWIPE_GUI_OPTIONS_PRNG_X 1
#define NWIPE_GUI_OPTIONS_METHOD_Y 3
#define NWIPE_GUI_OPTIONS_METHOD_X 1
#define NWIPE_GUI_OPTIONS_VERIFY_Y 4
#define NWIPE_GUI_OPTIONS_VERIFY_X 1
#define NWIPE_GUI_OPTIONS_ROUNDS_Y 5
#define NWIPE_GUI_OPTIONS_ROUNDS_X 1

/* Stats window: width, height, x coordinate, y coordinate. */
#define NWIPE_GUI_STATS_W ( COLS - 44 )
#define NWIPE_GUI_STATS_H 7
#define NWIPE_GUI_STATS_Y 1
#define NWIPE_GUI_STATS_X 44

/* Stats fields, relative to their window. */
#define NWIPE_GUI_STATS_RUNTIME_Y 1
#define NWIPE_GUI_STATS_RUNTIME_X 1
#define NWIPE_GUI_STATS_ETA_Y 2
#define NWIPE_GUI_STATS_ETA_X 1
#define NWIPE_GUI_STATS_LOAD_Y 3
#define NWIPE_GUI_STATS_LOAD_X 1
#define NWIPE_GUI_STATS_THROUGHPUT_Y 4
#define NWIPE_GUI_STATS_THROUGHPUT_X 1
#define NWIPE_GUI_STATS_ERRORS_Y 5
#define NWIPE_GUI_STATS_ERRORS_X 1
#define NWIPE_GUI_STATS_TAB 16

/* Select window: width, height, x coordinate, y coordinate. */
#define NWIPE_GUI_MAIN_W COLS
#define NWIPE_GUI_MAIN_H ( LINES - NWIPE_GUI_MAIN_Y - 1 )
#define NWIPE_GUI_MAIN_Y 8
#define NWIPE_GUI_MAIN_X 0

#define SKIP_DEV_PREFIX 5

/* Window pointers. */
WINDOW* footer_window;
WINDOW* header_window;
WINDOW* main_window;
WINDOW* options_window;
WINDOW* stats_window;

PANEL* footer_panel;
PANEL* header_panel;
PANEL* main_panel;
PANEL* options_panel;
PANEL* stats_panel;

/* Options window title. */
const char* options_title = " Options ";

/* Statistics window title. */
const char* stats_title = " Statistics ";

/* Footer labels. */
const char* main_window_footer =
    "S=Start m=Method p=PRNG v=Verify r=Rounds b=Blanking Space=Select c=Config CTRL+C=Quit";
const char* main_window_footer_warning_lower_case_s = "  WARNING: To start the wipe press SHIFT+S (uppercase S)  ";

const char* main_window_footer_warning_no_blanking_with_ops2 =
    "  WARNING: Zero blanking is not allowed with ops2 method  ";

const char* main_window_footer_warning_no_blanking_with_verify_only =
    "  WARNING: Zero blanking is not allowed with verify method  ";

const char* main_window_footer_warning_no_drive_selected =
    "  No drives selected, use spacebar to select a drive, then press S to start  ";

/* Oddly enough, placing extra quotes around the footer strings fixes corruption to the right
 * of the footer message when the terminal is resized, a quirk in ncurses? - DO NOT REMOVE THE \" */
const char* selection_footer = "J=Down K=Up Space=Select Backspace=Cancel Ctrl+C=Quit";
const char* selection_footer_config = "J=Down K=Up Return=Select ESC|Backspace=back Ctrl+C=Quit";
const char* selection_footer_preview_prior_to_drive_selection =
    "A=Accept & display drives J=Down K=Up Space=Select Backspace=Cancel Ctrl+C=Quit";
const char* selection_footer_add_customer = "S=Save J=Down K=Up Space=Select Backspace=Cancel Ctrl+C=Quit";
const char* selection_footer_add_customer_yes_no = "Save Customer Details Y/N";
const char* end_wipe_footer = "B=[Toggle between dark\\blank\\blue screen] Ctrl+C=Quit";
const char* rounds_footer = "Left=Erase Esc=Cancel Ctrl+C=Quit";
const char* selection_footer_text_entry = "Esc=Cancel Return=Submit Ctrl+C=Quit";

/* The number of lines available in the terminal */
int stdscr_lines;

/* The number of columns available in the terminal */
int stdscr_cols;

/* The size of the terminal lines when previously checked */
int stdscr_lines_previous;

/* The size of the terminal columns when previously checked */
int stdscr_cols_previous;

int tft_saver = 0;

void nwipe_gui_title( WINDOW* w, const char* s )
{
    /**
     * Prints the string 's' centered on the first line of the window 'w'.
     */

    /* The number of lines in the window. (Not used.) */
    int wy;
    (void) wy; /* flag wy not used to the compiler, to silence warning */

    /* The number of columns in the window. */
    int wx;

    /* Get the window dimensions. */
    getmaxyx( w, wy, wx );

    /*Calculate available total margin */
    int margin = ( wx - strlen( s ) );
    if( margin < 0 )
    {
        margin = 0;
    }

    /* tft_saver = grey text on black mode */
    if( tft_saver )
    {
        wattron( w, A_BOLD );
    }

    /* Print the title. */
    mvwprintw( w, 0, margin / 2, "%s", s );

} /* nwipe_gui_title */

void nwipe_init_pairs( void )
{
    if( has_colors() )
    {
        /* Initialize color capabilities. */
        start_color();

        if( can_change_color() )
        {
            /* Redefine cyan to gray. */
            init_color( COLOR_CYAN, 128, 128, 128 );
        }

        /* If we are in tft saver mode set grey text on black background else
         * Set white on blue as the emphasis color */
        if( tft_saver )
        {
            init_pair( 1, COLOR_BLACK, COLOR_BLACK );
        }
        else
        {
            init_pair( 1, COLOR_WHITE, COLOR_BLUE );
        }

        /* Set gray (or cyan) on blue as the normal color. */
        init_pair( 2, COLOR_CYAN, COLOR_BLUE );

        /* Set red on blue as the hilite color. */
        init_pair( 3, COLOR_RED, COLOR_BLUE );

        /* If we are in tft saver mode set grey text on black background else
         * Set white on blue as the emphasis color */
        if( tft_saver )
        {
            init_pair( 4, COLOR_BLACK, COLOR_BLACK );
        }
        else
        {
            init_pair( 4, COLOR_BLUE, COLOR_WHITE );
        }

        /* Set white on green for success messages. */
        init_pair( 5, COLOR_WHITE, COLOR_GREEN );

        /* Set white on red for failure messages. */
        init_pair( 6, COLOR_WHITE, COLOR_RED );

        /* Set black on black for when hiding the display. */
        init_pair( 7, COLOR_BLACK, COLOR_BLACK );

        /* Set green on blue for reverse bold messages */
        init_pair( 8, COLOR_GREEN, COLOR_WHITE );

        /* Set green on blue for reverse bold error messages */
        init_pair( 9, COLOR_RED, COLOR_WHITE );

        /* Set black on yellow for warning messages */
        init_pair( 10, COLOR_BLACK, COLOR_YELLOW );

        /* Set black on blue for minimum temperature reached */
        init_pair( 11, COLOR_BLACK, COLOR_BLUE );

        /* Set blue on blue to make temperature invisible */
        init_pair( 12, COLOR_BLUE, COLOR_BLUE );

        /* Set magenta on blue  */
        init_pair( 13, COLOR_MAGENTA, COLOR_BLUE );

        /* Set white on black for low critical temperature */
        init_pair( 14, COLOR_WHITE, COLOR_BLACK );

        /* Set the background style. */
        wbkgdset( stdscr, COLOR_PAIR( 1 ) | ' ' );
    }
}

void nwipe_gui_init( void )
{
    /**
     * Initializes the ncurses gui.
     */

    /* Initialize the screen. */
    initscr();

    /* Disable TTY line buffering. */
    cbreak();

    /* Disable TTY echo. */
    noecho();

    /* Enable most special keys. */
    keypad( stdscr, TRUE );

    /* Create the text/background color pairs */
    nwipe_init_pairs();

    /* Clear the screen. */
    wclear( stdscr );

    /* Create the header window. */
    nwipe_gui_create_header_window();

    /* Create the footer window and panel */
    nwipe_gui_create_footer_window( main_window_footer );

    /* Create the options window and panel */
    nwipe_gui_create_options_window();

    /* Create the stats window. */
    nwipe_gui_create_stats_window();

    /* Create a new main window and panel */
    nwipe_gui_create_main_window();

    update_panels();
    doupdate();

    /* Hide the cursor. */
    curs_set( 0 );

} /* nwipe_gui_init */

void nwipe_gui_free( void )
{
    /**
     * Releases the ncurses gui.
     *
     */
    /* Free ncurses resources. */
    if( del_panel( footer_panel ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting footer panel failed!." );
    }
    if( del_panel( header_panel ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting header panel failed!." );
    }
    if( del_panel( main_panel ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting main panel failed!." );
    }
    if( del_panel( options_panel ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting options panel failed!." );
    }
    if( del_panel( stats_panel ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting stats panel failed!." );
    }
    if( delwin( footer_window ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting footer window failed!." );
    }
    if( delwin( header_window ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting header window failed!." );
    }
    if( delwin( main_window ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting main window failed!." );
    }
    if( delwin( options_window ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting options window failed!." );
    }
    if( delwin( stats_window ) != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Deleting stats window failed!." );
    }
    if( endwin() != OK )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Curses endwin() failed !" );
    }

} /* nwipe_gui_free */

void nwipe_gui_create_main_window()
{
    /* Create the main window. */
    main_window = newwin( NWIPE_GUI_MAIN_H, NWIPE_GUI_MAIN_W, NWIPE_GUI_MAIN_Y, NWIPE_GUI_MAIN_X );
    main_panel = new_panel( main_window );

    if( has_colors() )
    {
        /* Set the background style. */
        wbkgdset( main_window, COLOR_PAIR( 1 ) | ' ' );

        /* Apply the color change. */
        wattron( main_window, COLOR_PAIR( 1 ) );

        /* In tft saver mode we toggle the intensity bit which gives us grey text */
        if( tft_saver )
        {
            wattron( main_window, A_BOLD );
        }
        else
        {
            wattroff( main_window, A_BOLD );
        }
    }

    /* Clear the main window. */
    werase( main_window );

    /* Add a border. */
    box( main_window, 0, 0 );

    /* refresh main window */
    wnoutrefresh( main_window );

} /* nwipe_gui_create_main_window */

void nwipe_gui_create_header_window()
{
    char anon_label[] = " (ANONYMIZED)";
    char bannerplus[80];

    /* Create the header window. */
    header_window = newwin( NWIPE_GUI_HEADER_H, NWIPE_GUI_HEADER_W, NWIPE_GUI_HEADER_Y, NWIPE_GUI_HEADER_X );
    header_panel = new_panel( header_window );

    if( has_colors() )
    {
        /* Set the background style of the header window. */
        wbkgdset( header_window, COLOR_PAIR( 4 ) | ' ' );

        if( tft_saver )
        {
            wattron( main_window, A_BOLD );
        }
    }

    /* Clear the header window. */
    werase( header_window );

    /* If in anonymized mode modify the title banner to reflect this */
    strcpy( bannerplus, banner );

    if( nwipe_options.quiet )
    {
        strcat( bannerplus, anon_label );
    }

    /* Print the product banner. */
    nwipe_gui_title( header_window, bannerplus );

    /* Refresh the header window */
    wnoutrefresh( header_window );

} /* nwipe_gui_create_header_window */

void nwipe_gui_create_footer_window( const char* footer_text )
{
    /* Create the footer window. */
    footer_window = newwin( NWIPE_GUI_FOOTER_H, NWIPE_GUI_FOOTER_W, NWIPE_GUI_FOOTER_Y, NWIPE_GUI_FOOTER_X );
    footer_panel = new_panel( footer_window );

    if( has_colors() )
    {
        /* Set the background style of the footer window. */
        wbkgdset( footer_window, COLOR_PAIR( 4 ) | ' ' );
    }

    /* Erase the footer window. */
    werase( footer_window );

    /* Add help text to the footer */
    nwipe_gui_title( footer_window, footer_text );

    /* Refresh the footer window */
    wnoutrefresh( footer_window );

} /* nwipe_gui_create_footer_window */

void nwipe_gui_amend_footer_window( const char* footer_text )
{
    /* Clear the footer window. */
    werase( footer_window );

    /* Add help text to the footer */
    nwipe_gui_title( footer_window, footer_text );

    /* Refresh the footer window */
    wnoutrefresh( footer_window );

} /* nwipe_gui_amend_footer_window */

void nwipe_gui_create_options_window()
{
    /* Create the options window. */
    options_window = newwin( NWIPE_GUI_OPTIONS_H, NWIPE_GUI_OPTIONS_W, NWIPE_GUI_OPTIONS_Y, NWIPE_GUI_OPTIONS_X );
    options_panel = new_panel( options_window );

    if( has_colors() )
    {
        /* Set the background style of the options window. */
        wbkgdset( options_window, COLOR_PAIR( 1 ) | ' ' );

        /* Apply the color change to the options window. */
        wattron( options_window, COLOR_PAIR( 1 ) );

        if( tft_saver )
        {
            wattron( options_window, A_BOLD );
        }
    }

    /* Clear the options window. */
    werase( options_window );

    /* Add a border. */
    box( options_window, 0, 0 );

} /* nwipe_gui_create_options_window */

void nwipe_gui_create_stats_window()
{
    /* Create the stats window. */
    stats_window = newwin( NWIPE_GUI_STATS_H, NWIPE_GUI_STATS_W, NWIPE_GUI_STATS_Y, NWIPE_GUI_STATS_X );
    stats_panel = new_panel( stats_window );

    if( has_colors() )
    {
        /* Set the background style of the stats window. */
        wbkgdset( stats_window, COLOR_PAIR( 1 ) | ' ' );

        /* Apply the color change to the stats window. */
        wattron( stats_window, COLOR_PAIR( 1 ) );

        if( tft_saver )
        {
            wattron( stats_window, A_BOLD );
        }
    }

    /* Clear the new window. */
    werase( stats_window );

    /* Add a border. */
    box( stats_window, 0, 0 );

    /* Add a title. */
    nwipe_gui_title( stats_window, stats_title );

    /* Print field labels. */
    mvwprintw( stats_window, NWIPE_GUI_STATS_RUNTIME_Y, NWIPE_GUI_STATS_RUNTIME_X, "Runtime:       " );
    mvwprintw( stats_window, NWIPE_GUI_STATS_ETA_Y, NWIPE_GUI_STATS_ETA_X, "Remaining:     " );
    mvwprintw( stats_window, NWIPE_GUI_STATS_LOAD_Y, NWIPE_GUI_STATS_LOAD_X, "Load Averages: " );
    mvwprintw( stats_window, NWIPE_GUI_STATS_THROUGHPUT_Y, NWIPE_GUI_STATS_THROUGHPUT_X, "Throughput:    " );
    mvwprintw( stats_window, NWIPE_GUI_STATS_ERRORS_Y, NWIPE_GUI_STATS_ERRORS_X, "Errors:        " );

} /* nwipe_gui_create_stats_window */

void nwipe_gui_create_all_windows_on_terminal_resize( int force_creation, const char* footer_text )
{
    /* Get the terminal size */
    getmaxyx( stdscr, stdscr_lines, stdscr_cols );

    /* If the user has resized the terminal then recreate the windows and panels */
    if( stdscr_cols_previous != stdscr_cols || stdscr_lines_previous != stdscr_lines || force_creation == 1 )
    {
        /* Save the revised terminal size so we check whether the user has resized next time */
        stdscr_lines_previous = stdscr_lines;
        stdscr_cols_previous = stdscr_cols;

        /* Clear the screen. */
        wclear( stdscr );

        /* Create a new header window and panel due to terminal size having changed */
        nwipe_gui_create_header_window();

        /* Create a new main window and panel due to terminal size having changed */
        nwipe_gui_create_main_window();

        /* Create a new footer window and panel due to terminal size having changed */
        nwipe_gui_create_footer_window( footer_text );

        /* Create a new options window and panel due to terminal size having changed */
        nwipe_gui_create_options_window();

        /* Create a new stats window and panel due to terminal size having changed */
        nwipe_gui_create_stats_window();

        /* Update the options window. */
        nwipe_gui_options();

        update_panels();
        doupdate();
    }
}

void nwipe_gui_select( int count, nwipe_context_t** c )
{
    extern int terminate_signal;

    /* Widget labels. */
    const char* select_title = " Disks and Partitions ";

    /* The number of lines available in the window. */
    int wlines;

    /* The number of columns available in the window. */
    int wcols;

    /* The number of selection elements that we can show in the window. */
    int slots;

    /* The index of the element that is visible in the first slot. */
    int offset = 0;

    /* The selection focus. */
    int focus = 0;

    /* A generic loop variable. */
    int i = 0;

    /* User input buffer. */
    int keystroke;

    /* The current working line. */
    int yy;

    /* Flag, Valid key hit = 1, anything else = 0 */
    int validkeyhit;

    /* Counts number of drives and partitions that have been selected */
    int number_of_selected_contexts = 0;

    /* Control A toggle status -1=indefined, 0=all drives delected, 1=all drives selected */
    int select_all_toggle_status = -1;

    /* Get the terminal size */
    getmaxyx( stdscr, stdscr_lines, stdscr_cols );

    /* Save the terminal size so we check whether the user has resized */
    stdscr_lines_previous = stdscr_lines;
    stdscr_cols_previous = stdscr_cols;

    time_t temperature_check_time = time( NULL );

    /* Used in the selection loop to trap a failure of the timeout(), getch() mechanism to block for the designated
     * period */
    int iteration_counter;

    /* Used in the selection loop to trap a failure of the timeout(), getch() mechanism to block for the designated
     * period */
    int expected_iterations;

    time_t previous_iteration_timestamp;

    do
    {

        nwipe_gui_create_all_windows_on_terminal_resize( 0, main_window_footer );

        /* There is one slot per line. */
        getmaxyx( main_window, wlines, wcols );

        /* Less two lines for the box and two lines for padding. */
        slots = wlines - 4;
        if( slots < 0 )
        {
            slots = 0;
        }

        /* The code here adjusts the offset value, required when the terminal is resized vertically */
        if( slots > count )
        {
            offset = 0;
        }
        else
        {
            if( focus >= count )
            {
                /* The focus is already at the last element. */
                focus = count - 1;
            }
            if( focus < 0 )
            {
                /* The focus is already at the last element. */
                focus = 0;
            }
        }

        if( count >= slots && slots > 0 )
        {
            offset = focus + 1 - slots;
            if( offset < 0 )
            {
                offset = 0;
            }
        }

        /* Clear the main window, necessary when switching selections such as method etc */
        werase( main_window );

        /* Refresh main window */
        wnoutrefresh( main_window );

        /* If the user selected an option the footer text would have changed.
         * Here we set it back to the main key help text */
        nwipe_gui_create_footer_window( main_window_footer );

        /* Refresh the stats window */
        wnoutrefresh( stats_window );

        /* Refresh the options window */
        wnoutrefresh( options_window );

        /* Update the options window. */
        nwipe_gui_options();

        /* Initialize the line offset. */
        yy = 2;

        for( i = 0; i < slots && i < count; i++ )
        {

            /* Move to the next line. */
            mvwprintw( main_window, yy++, 1, " " );

            if( i + offset == focus )
            {
                if( c[focus]->select == NWIPE_SELECT_TRUE || c[focus]->select == NWIPE_SELECT_FALSE )
                {
                    /* Print the 'enabled' cursor. */
                    waddch( main_window, ACS_RARROW );
                }

                else
                {
                    /* Print the 'disabled' cursor. */
                    waddch( main_window, ACS_DIAMOND );
                }
            }

            else
            {
                /* Print whitespace. */
                waddch( main_window, ' ' );
            }

            /* In the event for the offset value somehow becoming invalid, this if statement will prevent a segfault
             * and the else part will log the out of bounds values for debugging */
            if( i + offset >= 0 && i + offset < count )
            {

                switch( c[i + offset]->select )
                {
                    case NWIPE_SELECT_TRUE:

                        if( nwipe_options.method == &nwipe_verify_zero || nwipe_options.method == &nwipe_verify_one )
                        {
                            wprintw( main_window,
                                     "[vrfy] %s %s ",
                                     c[i + offset]->gui_device_name,
                                     c[i + offset]->device_type_str );
                        }
                        else
                        {
                            wprintw( main_window,
                                     "[wipe] %s %s ",
                                     c[i + offset]->gui_device_name,
                                     c[i + offset]->device_type_str );
                        }
                        break;

                    case NWIPE_SELECT_FALSE:
                        /* Print an element that is not selected. */
                        wprintw( main_window,
                                 "[    ] %s %s ",
                                 c[i + offset]->gui_device_name,
                                 c[i + offset]->device_type_str );
                        break;

                    case NWIPE_SELECT_TRUE_PARENT:

                        /* This element will be wiped when its parent is wiped. */
                        wprintw( main_window,
                                 "[****] %s %s ",
                                 c[i + offset]->gui_device_name,
                                 c[i + offset]->device_type_str );
                        break;

                    case NWIPE_SELECT_FALSE_CHILD:

                        /* We can't wipe this element because it has a child that is being wiped. */
                        wprintw( main_window,
                                 "[----] %s %s ",
                                 c[i + offset]->gui_device_name,
                                 c[i + offset]->device_type_str );
                        break;

                    case NWIPE_SELECT_DISABLED:

                        /* We don't know how to wipe this device. (Iomega Zip drives.) */
                        wprintw( main_window, "[????] %s ", "Unrecognized Device" );
                        break;

                    default:

                        /* TODO: Handle the sanity error. */
                        break;

                } /* switch select */

                wprintw( main_window, "[%s] ", c[i + offset]->device_size_text );

                /* Read the drive temperature values */
                // nwipe_update_temperature( c[i + offset] );

                /* print the temperature */
                wprintw_temperature( c[i + offset] );

                switch( c[i + offset]->HPA_status )
                {
                    case HPA_ENABLED:
                        wprintw( main_window, " " );
                        wattron( main_window, COLOR_PAIR( 9 ) );
                        wprintw( main_window, "[HS? YES]" );
                        wattroff( main_window, COLOR_PAIR( 9 ) );
                        break;

                    case HPA_DISABLED:
                        wprintw( main_window, " " );
                        wprintw( main_window, "[HS? NO ]" );
                        break;

                    case HPA_UNKNOWN:
                        wprintw( main_window, " " );
                        wprintw( main_window, "[HS? ???]" );
                        break;

                    default:

                        wprintw( main_window, " " );
                        wprintw( main_window, "[HS? N/A]" );
                        break;
                }

                /* print the drive model and serial number */
                wprintw( main_window, " %s/%s", c[i + offset]->device_model, c[i + offset]->device_serial_no );

                if( c[i + offset]->HPA_toggle_time + 1 < time( NULL ) )
                {
                    switch( c[i + offset]->HPA_display_toggle_state )
                    {
                        case 0:
                            c[i + offset]->HPA_display_toggle_state = 1;
                            break;

                        case 1:
                            c[i + offset]->HPA_display_toggle_state = 0;
                            break;
                    }
                    c[i + offset]->HPA_toggle_time = time( NULL );
                }
            }
            else
            {
                nwipe_log( NWIPE_LOG_DEBUG,
                           "GUI.c,nwipe_gui_select(), scroll, array index out of bounds, i=%u, count=%u, slots=%u, "
                           "focus=%u, offset=%u",
                           i,
                           count,
                           slots,
                           focus,
                           offset );
            }

        } /* for */

        if( offset > 0 )
        {
            mvwprintw( main_window, 1, wcols - 8, " More " );
            waddch( main_window, ACS_UARROW );
        }

        if( count - offset > slots )
        {
            mvwprintw( main_window, wlines - 2, wcols - 8, " More " );
            waddch( main_window, ACS_DARROW );
        }

        /* Draw a border around the menu window. */
        box( main_window, 0, 0 );

        /* Print a title. */
        nwipe_gui_title( main_window, select_title );

        /* Refresh the window. */
        wnoutrefresh( main_window );

        /* Output to physical screen */
        doupdate();

        /* Initialise the iteration counter */
        iteration_counter = 0;

        previous_iteration_timestamp = time( NULL );

        /* Calculate Maximum allowed iterations per second */
        expected_iterations = ( 1000 / GETCH_BLOCK_MS ) * 8;

        do
        {
            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */

            validkeyhit = 0;
            timeout( GETCH_BLOCK_MS );  // block getch() for ideally about 250ms.
            keystroke = getch();  // Get user input.
            timeout( -1 );  // Switch back to blocking mode.

            /* To avoid 100% CPU usage, check for a runaway condition caused by the "keystroke = getch(); (above), from
             * immediately returning an error condition. We check for an error condition because getch() returns a ERR
             * value when the timeout value "timeout( 250 );" expires as well as when a real error occurs. We can't
             * differentiate from normal operation and a failure of the getch function to block for the specified period
             * of timeout. So here we check the while loop hasn't exceeded the number of expected iterations per second
             * ie. a timeout(250) block value of 250ms means we should not see any more than (1000/250) = 4 iterations.
             * We increase this to 32 iterations to allow a little tolerance. Why is this necessary? It's been found
             * that in KDE konsole and other terminals based on the QT terminal engine exiting the terminal without
             * first exiting nwipe results in nwipe remaining running but detached from any interface which causes
             * getch to fail and its associated timeout. So the CPU or CPU core rises to 100%. Here we detect that
             * failure and exit nwipe gracefully with the appropriate error. This does not affect use of tmux for
             * attaching or detaching from a running nwipe session when sitting at the selection screen. All other
             * terminals correctly terminate nwipe when the terminal itself is exited.
             */

            iteration_counter++;

            if( previous_iteration_timestamp == time( NULL ) )
            {
                if( iteration_counter > expected_iterations )
                {
                    nwipe_log( NWIPE_LOG_ERROR,
                               "GUI.c,nwipe_gui_select(), loop runaway, did you close the terminal without exiting "
                               "nwipe? Exiting nwipe now." );
                    /* Issue signal to nwipe to exit immediately but gracefully */
                    terminate_signal = 1;
                }
            }
            else
            {
                /* new second, so reset counter */
                iteration_counter = 0;
                previous_iteration_timestamp = time( NULL );
            }

            /* We don't necessarily use all of these. For future reference these are some CTRL+key values
             * ^A - 1, ^B - 2, ^D - 4, ^E - 5, ^F - 6, ^G - 7, ^H - 8, ^I - 9, ^K - 11, ^L - 12, ^N - 14,
             * ^O - 15, ^P - 16, ^R - 18, ^T - 20, ^U - 21, ^V - 22, ^W - 23, ^X - 24, ^Y - 25
             * Use nwipe_log( NWIPE_LOG_DEBUG, "Key Name: %s - %u", keyname(keystroke),keystroke) to
             * figure out what code is returned by what ever key combination */

            switch( keystroke )
            {
                case KEY_DOWN:
                case 'j':
                case 'J':

                    validkeyhit = 1;

                    /* Increment the focus. */
                    focus += 1;

                    if( focus >= count )
                    {
                        /* The focus is already at the last element. */
                        focus = count - 1;
                        break;
                    }

                    if( focus - offset >= slots )
                    {
                        /* The next element is offscreen. Scroll down. */
                        offset += 1;
                        break;
                    }

                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    validkeyhit = 1;

                    /* Decrement the focus. */
                    focus -= 1;

                    if( focus < 0 )
                    {
                        /* The focus is already at the last element. */
                        focus = 0;
                        break;
                    }

                    if( focus < offset )
                    {
                        /* The next element is offscreen. Scroll up. */
                        offset -= 1;
                        break;
                    }

                    break;

                case KEY_ENTER:
                case 10:
                case ' ':

                    validkeyhit = 1;

                    /* TODO: This block should be made into a function. */

                    if( c[focus]->select == NWIPE_SELECT_TRUE )
                    {
                        /* Reverse the selection of this element. */
                        c[focus]->select = NWIPE_SELECT_FALSE;

                        if( c[focus]->device_part == 0 )
                        {
                            /* Sub-deselect all partitions and slices within this disk. */
                            for( i = 0; i < count; i++ )
                            {
                                if( c[i]->device_type == c[focus]->device_type
                                    && c[i]->device_host == c[focus]->device_host
                                    && c[i]->device_bus == c[focus]->device_bus
                                    && c[i]->device_target == c[focus]->device_target
                                    && c[i]->device_lun == c[focus]->device_lun && c[i]->device_part > 0 )
                                {
                                    c[i]->select = NWIPE_SELECT_FALSE;
                                }

                            } /* for all contexts */

                        } /* if sub-deselect */

                        else
                        {
                            /* The number of selected partitions or slices within this disk. */
                            int j = 0;

                            for( i = 0; i < count; i++ )
                            {
                                if( c[i]->device_type == c[focus]->device_type
                                    && c[i]->device_host == c[focus]->device_host
                                    && c[i]->device_bus == c[focus]->device_bus
                                    && c[i]->device_target == c[focus]->device_target
                                    && c[i]->device_lun == c[focus]->device_lun && c[i]->device_part > 0
                                    && c[i]->select == NWIPE_SELECT_TRUE )
                                {
                                    /* Increment the counter. */
                                    j += 1;
                                }

                            } /* for all contexts */

                            if( j == 0 )
                            {
                                /* Find the parent disk of this partition or slice. */
                                for( i = 0; i < count; i++ )
                                {
                                    if( c[i]->device_type == c[focus]->device_type
                                        && c[i]->device_host == c[focus]->device_host
                                        && c[i]->device_bus == c[focus]->device_bus
                                        && c[i]->device_target == c[focus]->device_target
                                        && c[i]->device_lun == c[focus]->device_lun && c[i]->device_part == 0 )
                                    {
                                        /* Enable the disk element. */
                                        c[i]->select = NWIPE_SELECT_FALSE;
                                    }

                                } /* for all contexts */

                            } /* if */

                        } /* else super-enable */

                        break;

                    } /* if NWIPE_SELECT_TRUE */

                    if( c[focus]->select == NWIPE_SELECT_FALSE )
                    {
                        /* Reverse the selection. */
                        c[focus]->select = NWIPE_SELECT_TRUE;

                        if( c[focus]->device_part == 0 )
                        {
                            /* Sub-select all partitions and slices within this disk. */
                            for( i = 0; i < count; i++ )
                            {
                                if( c[i]->device_type == c[focus]->device_type
                                    && c[i]->device_host == c[focus]->device_host
                                    && c[i]->device_bus == c[focus]->device_bus
                                    && c[i]->device_target == c[focus]->device_target
                                    && c[i]->device_lun == c[focus]->device_lun && c[i]->device_part > 0 )
                                {
                                    c[i]->select = NWIPE_SELECT_TRUE_PARENT;
                                }

                            } /* for */

                        } /* if sub-select */

                        else
                        {
                            /* ASSERT: ( c[focus]->device_part > 0 ) */

                            /* Super-deselect the disk that contains this device. */
                            for( i = 0; i < count; i++ )
                            {
                                if( c[i]->device_type == c[focus]->device_type
                                    && c[i]->device_host == c[focus]->device_host
                                    && c[i]->device_bus == c[focus]->device_bus
                                    && c[i]->device_target == c[focus]->device_target
                                    && c[i]->device_lun == c[focus]->device_lun && c[i]->device_part == 0 )
                                {
                                    c[i]->select = NWIPE_SELECT_FALSE_CHILD;
                                }
                            }

                        } /* else super-deselect */

                        break;

                    } /* if NWIPE_SELECT_FALSE */

                    /* TODO: Explain to the user why they can't change this. */
                    break;

                case 'm':
                case 'M':

                    validkeyhit = 1;

                    /*  Run the method dialog. */
                    nwipe_gui_method();
                    break;

                case 'p':
                case 'P':

                    validkeyhit = 1;

                    /* Run the PRNG dialog. */
                    nwipe_gui_prng();

                    break;

                case 'r':
                case 'R':

                    validkeyhit = 1;

                    /* Run the rounds dialog. */
                    nwipe_gui_rounds();

                    break;

                case 'v':
                case 'V':

                    validkeyhit = 1;

                    /* Run the option dialog. */
                    nwipe_gui_verify();
                    break;

                case 'b':
                case 'B':

                    validkeyhit = 1;

                    if( nwipe_options.method == &nwipe_ops2 )
                    {
                        /* Warn the user about that zero blanking with the ops2 method is not allowed */
                        wattron( footer_window, COLOR_PAIR( 10 ) );
                        nwipe_gui_amend_footer_window( main_window_footer_warning_no_blanking_with_ops2 );
                        doupdate();
                        sleep( 3 );
                        wattroff( footer_window, COLOR_PAIR( 10 ) );

                        /* After the delay return footer text back to key help */
                        nwipe_gui_amend_footer_window( main_window_footer );
                        doupdate();

                        break;
                    }

                    if( nwipe_options.method == &nwipe_verify_zero || nwipe_options.method == &nwipe_verify_one )
                    {
                        /* Warn the user about that zero blanking with the ops2 method is not allowed */
                        wattron( footer_window, COLOR_PAIR( 10 ) );
                        nwipe_gui_amend_footer_window( main_window_footer_warning_no_blanking_with_verify_only );
                        doupdate();
                        sleep( 3 );
                        wattroff( footer_window, COLOR_PAIR( 10 ) );

                        /* After the delay return footer text back to key help */
                        nwipe_gui_amend_footer_window( main_window_footer );
                        doupdate();

                        break;
                    }

                    /* Run the noblank dialog. */
                    nwipe_gui_noblank();
                    break;

                case 'c':
                case 'C':
                    /* main configuration menu */
                    validkeyhit = 1;

                    /* Run the configuration dialog */
                    nwipe_gui_config();
                    break;

                case 'S':

                    /* User wants to start the wipe */
                    validkeyhit = 1;

                    /* Have any drives have been selected ? */
                    number_of_selected_contexts = 0;
                    for( i = 0; i < count; i++ )
                    {
                        if( c[i]->select == NWIPE_SELECT_TRUE )
                        {
                            number_of_selected_contexts += 1;
                        }
                    }

                    /* if no drives have been selected, print a warning on the footer */
                    if( number_of_selected_contexts == 0 )
                    {
                        wattron( footer_window, COLOR_PAIR( 10 ) );
                        nwipe_gui_amend_footer_window( main_window_footer_warning_no_drive_selected );
                        doupdate();
                        sleep( 3 );
                        wattroff( footer_window, COLOR_PAIR( 10 ) );

                        /* After the delay return footer text back to key help */
                        nwipe_gui_amend_footer_window( main_window_footer );
                        doupdate();

                        /* Remove any repeated S key strokes, without this the gui would hang
                         * for a period of time, i.e sleep above x number of repeated 's' keystrokes
                         * which could run into minutes */
                        do
                        {
                            timeout( 250 );  // block getch() for 250ms.
                            keystroke = getch();  // Get user input.
                            timeout( -1 );  // Switch back to blocking mode.
                        } while( keystroke == 'S' );

                        /* Remove the S from keystroke, which allows us to stay within the selection menu loop */
                        keystroke = 0;
                    }

                    break;

                case 's':

                    /* user has mistakenly hit the lower case 's' instead of capital 'S' */
                    validkeyhit = 1;

                    /* Warn the user about their mistake */
                    wattron( footer_window, COLOR_PAIR( 10 ) );
                    nwipe_gui_amend_footer_window( main_window_footer_warning_lower_case_s );
                    doupdate();
                    sleep( 3 );
                    wattroff( footer_window, COLOR_PAIR( 10 ) );

                    /* After the delay return footer text back to key help */
                    nwipe_gui_amend_footer_window( main_window_footer );
                    doupdate();

                    /* Remove any repeated s key strokes, without this the gui would hang
                     * for a period of time, i.e sleep above x number of repeated 's' keystrokes
                     * which could run into minutes */
                    do
                    {
                        timeout( 250 );  // block getch() for 250ms.
                        keystroke = getch();  // Get user input.
                        timeout( -1 );  // Switch back to blocking mode.
                    } while( keystroke == 's' );

                    break;

                case 1:

                    /* Ctrl A - Toggle select/deselect all drives */
                    validkeyhit = 1;

                    if( select_all_toggle_status == -1 || select_all_toggle_status == 0 )
                    {
                        for( i = 0; i < count; i++ )
                        {
                            c[i]->select = NWIPE_SELECT_TRUE;
                        }
                        select_all_toggle_status = 1;
                    }
                    else
                    {
                        if( select_all_toggle_status == 1 )
                        {
                            for( i = 0; i < count; i++ )
                            {
                                c[i]->select = NWIPE_SELECT_FALSE;
                            }
                            select_all_toggle_status = 0;
                        }
                        else
                        {
                            nwipe_log(
                                NWIPE_LOG_ERROR,
                                "gui.c:nwipe_gui_select(), Invalid value in variable select_all_toggle_status = %d",
                                select_all_toggle_status );
                        }
                    }

                    break;

            } /* keystroke switch */

            /* Check the terminal size, if the user has changed it the while loop checks for
             * this change and exits the valid key hit loop so the windows can be updated */
            getmaxyx( stdscr, stdscr_lines, stdscr_cols );

            /* Update the selection window every 1 second specifically
             * so that the drive temperatures are updated and also the line toggle that
             * occurs with the HPA status and the drive size & temperature.
             */
            if( time( NULL ) > ( temperature_check_time + 1 ) )
            {
                temperature_check_time = time( NULL );
                validkeyhit = 1;
            }

        } /* key hit loop */
        while( validkeyhit == 0 && terminate_signal != 1 && stdscr_cols_previous == stdscr_cols
               && stdscr_lines_previous == stdscr_lines );

    } while( keystroke != 'S' && terminate_signal != 1 );

    if( keystroke == 'S' )
    {
        /* If user has pressed S to start wipe change status line */
        werase( footer_window );
        nwipe_gui_title( footer_window, end_wipe_footer );
        wnoutrefresh( footer_window );
    }

} /* nwipe_gui_select */

void nwipe_gui_options( void )
{
    /**
     * Updates the options window.
     *
     * @modifies  options_window
     *
     */

    /* Erase the window. */
    werase( options_window );

    mvwprintw(
        options_window, NWIPE_GUI_OPTIONS_ENTROPY_Y, NWIPE_GUI_OPTIONS_ENTROPY_X, "Entropy: Linux Kernel (urandom)" );

    mvwprintw(
        options_window, NWIPE_GUI_OPTIONS_PRNG_Y, NWIPE_GUI_OPTIONS_PRNG_X, "PRNG:    %s", nwipe_options.prng->label );

    mvwprintw( options_window,
               NWIPE_GUI_OPTIONS_METHOD_Y,
               NWIPE_GUI_OPTIONS_METHOD_X,
               "Method:  %s",
               nwipe_method_label( nwipe_options.method ) );

    mvwprintw( options_window, NWIPE_GUI_OPTIONS_VERIFY_Y, NWIPE_GUI_OPTIONS_VERIFY_X, "Verify:  " );

    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            wprintw( options_window, "Off" );
            break;

        case NWIPE_VERIFY_LAST:
            wprintw( options_window, "Last Pass" );
            break;

        case NWIPE_VERIFY_ALL:
            wprintw( options_window, "All Passes" );
            break;

        default:
            wprintw( options_window, "Unknown %i", nwipe_options.verify );

    } /* switch verify */

    mvwprintw( options_window, NWIPE_GUI_OPTIONS_ROUNDS_Y, NWIPE_GUI_OPTIONS_ROUNDS_X, "Rounds:  " );

    /* Disable blanking for ops2 and verify methods */
    if( nwipe_options.method == &nwipe_ops2 || nwipe_options.method == &nwipe_verify_zero
        || nwipe_options.method == &nwipe_verify_one )
    {
        nwipe_options.noblank = 1;
    }

    if( nwipe_options.noblank )
    {
        wprintw( options_window, "%i (no final blanking pass)", nwipe_options.rounds );
    }
    else
    {
        wprintw( options_window, "%i (plus blanking pass)", nwipe_options.rounds );
    }

    /* Add a border. */
    box( options_window, 0, 0 );

    /* Add a title. */
    nwipe_gui_title( options_window, options_title );

    /* Refresh the window. */
    // wrefresh( options_window );
    wnoutrefresh( options_window );

} /* nwipe_gui_options */

void nwipe_gui_rounds( void )
{
    /**
     * Allows the user to change the rounds option.
     *
     * @modifies  nwipe_options.rounds
     * @modifies  main_window
     *
     */

    /* Set the initial focus. */
    int focus = nwipe_options.rounds;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, rounds_footer );
    wrefresh( footer_window );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Rounds " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "This is the number of times to run the wipe method on each device." );
        yy++;

        if( focus > 0 )
        {
            /* Print this line last so that the cursor is in the right place. */
            mvwprintw( main_window, 2, tab1, "> %i", focus );
        }
        else
        {
            mvwprintw( main_window, yy++, tab1, "The number of rounds must be a non-negative integer." );

            /* Print this line last so that the cursor is in the right place. */
            mvwprintw( main_window, 2, tab1, "> " );
        }

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':

                if( focus < 100000000 )
                {
                    /* Left shift, base ten. */
                    focus *= 10;

                    /* This assumes ASCII input, where the zero character is 0x30. */
                    focus += keystroke - 48;
                }

                break;

            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                /* Right shift, base ten. */
                focus /= 10;

                break;

        } /* switch keystroke */

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    if( focus > 0 )
    {
        /* Set the number of rounds. */
        nwipe_options.rounds = focus;
    }

} /* nwipe_guid_rounds */

void nwipe_gui_prng( void )
{
    /**
     * Allows the user to change the PRNG.
     *
     * @modifies  nwipe_options.prng
     * @modifies  main_window
     *
     */

    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_aes_ctr_prng;
    extern int terminate_signal;

    /* The number of implemented PRNGs. */
    const int count = 4;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 30;

    /* Set the initial focus. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer );
    wrefresh( footer_window );

    if( nwipe_options.prng == &nwipe_twister )
    {
        focus = 0;
    }
    if( nwipe_options.prng == &nwipe_isaac )
    {
        focus = 1;
    }
    if( nwipe_options.prng == &nwipe_isaac64 )
    {
        focus = 2;
    }
    if( nwipe_options.prng == &nwipe_aes_ctr_prng )
    {
        focus = 3;
    }

    do
    {
        /* Clear the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );

        /* Initialize the working row. */
        yy = 3;

        /* Print the options. */
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_twister.label );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_isaac.label );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_isaac64.label );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_aes_ctr_prng.label );
        yy++;

        /* Print the cursor. */
        mvwaddch( main_window, 3 + focus, tab1, ACS_RARROW );

        switch( focus )
        {
            case 0:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "The Mersenne Twister, by Makoto Matsumoto and Takuji Nishimura, is a        " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "generalized feedback shift register PRNG that is uniform and                " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "equidistributed in 623-dimensions with a proven period of 2^19937-1.        " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "This implementation passes the Marsaglia Diehard test suite.                " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                break;

            case 1:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "ISAAC, by Bob Jenkins, is a PRNG derived from RC4 with a minimum period of  " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "2^40 and an expected period of 2^8295.  It is difficult to recover the      " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "initial PRNG state by cryptanalysis of the ISAAC stream.                    " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Performs best on a 32-bit CPU. Use ISAAC-64 if this system has a 64-bit CPU." );
                break;

            case 2:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "ISAAC-64, by Bob Jenkins, is like 32-bit ISAAC, but with a minimum period of" );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "2^77 and an expected period of 2^16583. It is difficult to recover the      " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "initial PRNG state by cryptanalysis of the ISAAC-64 stream.                 " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Performs best on a 64-bit CPU. Use ISAAC if this system has a 32-bit CPU.   " );
                break;
           case 3:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "AES-CTR Ni Prototype   " );
                break;

        } 

	
	/* switch */

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Pseudo Random Number Generator " );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':

                if( focus < count - 1 )
                {
                    focus += 1;
                }
                break;

            case KEY_UP:
            case 'k':
            case 'K':

                if( focus > 0 )
                {
                    focus -= 1;
                }
                break;

            case KEY_ENTER:
            case ' ':
            case 10:

                if( focus == 0 )
                {
                    nwipe_options.prng = &nwipe_twister;
                }
                if( focus == 1 )
                {
                    nwipe_options.prng = &nwipe_isaac;
                }
                if( focus == 2 )
                {
                    nwipe_options.prng = &nwipe_isaac64;
                }
		if( focus == 3 )
                {
                    nwipe_options.prng = &nwipe_aes_ctr_prng;
                }
                return;

            case KEY_BACKSPACE:
            case KEY_BREAK:

                return;

        } /* switch */

    } while( terminate_signal != 1 );

} /* nwipe_gui_prng */

void nwipe_gui_verify( void )
{
    /**
     * Allows the user to change the verification option.
     *
     * @modifies  nwipe_options.verify
     * @modifies  main_window
     *
     */

    extern int terminate_signal;

    /* The number of definitions in the nwipe_verify_t enumeration. */
    const int count = 3;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 30;

    /* Set the initial focus. */
    int focus = nwipe_options.verify;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer );
    wrefresh( footer_window );

    do
    {
        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );

        /* Clear the main window. */
        werase( main_window );

        /* Initialize the working row. */
        yy = 2;

        /* Print the options. */
        mvwprintw( main_window, yy++, tab1, "  Verification Off  " );
        mvwprintw( main_window, yy++, tab1, "  Verify Last Pass  " );
        mvwprintw( main_window, yy++, tab1, "  Verify All Passes " );
        mvwprintw( main_window, yy++, tab1, "                    " );

        /* Print the cursor. */
        mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

        switch( focus )
        {
            case 0:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Do not verify passes. The wipe will be a write-only operation.              " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                break;

            case 1:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Check whether the device is actually empty after the last pass fills the    " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "device with zeros.                                                          " );
                break;

            case 2:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "After every pass, read back the pattern and check whether it is correct.    " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "This program writes the entire length of the device before it reads back    " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "for verification, even for random pattern passes, to better ensure that     " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "hardware caches are actually flushed.                                       " );
                break;

        } /* switch */

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Verification Mode " );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':

                if( focus < count - 1 )
                {
                    focus += 1;
                }
                break;

            case KEY_UP:
            case 'k':
            case 'K':

                if( focus > 0 )
                {
                    focus -= 1;
                }
                break;

            case KEY_ENTER:
            case ' ':
            case 10:

                if( focus >= 0 && focus < count )
                {
                    nwipe_options.verify = focus;
                }
                return;

            case KEY_BACKSPACE:
            case KEY_BREAK:

                return;

        } /* switch */

    } while( terminate_signal != 1 );

} /* nwipe_gui_verify */

void nwipe_gui_noblank( void )
{
    /**
     * Allows the user to change the verification option.
     *
     * @modifies  nwipe_options.noblank
     * @modifies  main_window
     *
     */

    extern int terminate_signal;

    /* The number of options available. */
    const int count = 2;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 40;

    /* Set the initial focus. */
    int focus = nwipe_options.noblank;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer );
    wrefresh( footer_window );

    do
    {
        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );

        /* Clear the main window. */
        werase( main_window );

        /* Initialize the working row. */
        yy = 2;

        /* Print the options. */
        mvwprintw( main_window, yy++, tab1, "  Perform a final blanking pass       " );
        mvwprintw( main_window, yy++, tab1, "  Do not perform final blanking pass  " );
        mvwprintw( main_window, yy++, tab1, "                                      " );

        /* Print the cursor. */
        mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

        switch( focus )
        {
            case 0:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Perform a final blanking pass after the wipe, leaving disk with only zeros. " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Note that the RCMP TSSIT OPS-II method never blanks the device regardless   " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "of this setting.                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                break;

            case 1:

                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Do not perform a final blanking pass. Leave data as per final wiping pass.  " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Note that the RCMP TSSIT OPS-II method never blanks the device regardless   " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "of this setting.                                                            " );
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "                                                                            " );
                break;

        } /* switch */

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Final Blanking Pass " );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':

                if( focus < count - 1 )
                {
                    focus += 1;
                }
                break;

            case KEY_UP:
            case 'k':
            case 'K':

                if( focus > 0 )
                {
                    focus -= 1;
                }
                break;

            case KEY_ENTER:
            case ' ':
            case 10:

                if( focus >= 0 && focus < count )
                {
                    nwipe_options.noblank = focus;
                }
                return;

            case KEY_BACKSPACE:
            case KEY_BREAK:

                return;

        } /* switch */

    }

    while( terminate_signal != 1 );
} /* nwipe_gui_noblank */

void nwipe_gui_method( void )
{
    /**
     * Allows the user to change the wipe method.
     *
     * @modifies  nwipe_options.method
     * @modifies  main_window
     *
     */

    extern int terminate_signal;

    /* The number of implemented methods. */
    const int count = 10;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 30;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer );
    wrefresh( footer_window );

    if( nwipe_options.method == &nwipe_zero )
    {
        focus = 0;
    }
    if( nwipe_options.method == &nwipe_one )
    {
        focus = 1;
    }
    if( nwipe_options.method == &nwipe_ops2 )
    {
        focus = 2;
    }
    if( nwipe_options.method == &nwipe_dodshort )
    {
        focus = 3;
    }
    if( nwipe_options.method == &nwipe_dod522022m )
    {
        focus = 4;
    }
    if( nwipe_options.method == &nwipe_gutmann )
    {
        focus = 5;
    }
    if( nwipe_options.method == &nwipe_random )
    {
        focus = 6;
    }
    if( nwipe_options.method == &nwipe_verify_zero )
    {
        focus = 7;
    }
    if( nwipe_options.method == &nwipe_verify_one )
    {
        focus = 8;
    }
    if( nwipe_options.method == &nwipe_is5enh )
    {
        focus = 9;
    }

    do
    {
        /* Clear the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );

        /* Initialize the working row. */
        yy = 2;

        /* Print the options. */
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_zero ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_one ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_ops2 ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_dodshort ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_dod522022m ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_gutmann ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_random ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_verify_zero ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_verify_one ) );
        mvwprintw( main_window, yy++, tab1, "  %s", nwipe_method_label( &nwipe_is5enh ) );
        mvwprintw( main_window, yy++, tab1, "                                             " );

        /* Print the cursor. */
        mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

        switch( focus )
        {
            case 0:

                mvwprintw( main_window, 2, tab2, "Security Level: high (1 pass)" );

                mvwprintw( main_window, 4, tab2, "This method fills the device with zeros. Note    " );
                mvwprintw( main_window, 5, tab2, "that the rounds option does not apply to this    " );
                mvwprintw( main_window, 6, tab2, "method. This method always runs one round.       " );
                mvwprintw( main_window, 7, tab2, "                                                 " );
                mvwprintw( main_window, 8, tab2, "There is no publicly available evidence that   " );
                mvwprintw( main_window, 9, tab2, "data can be recovered from a modern traditional  " );
                mvwprintw( main_window, 10, tab2, "hard drive (HDD) that has been zero wiped,       " );
                mvwprintw( main_window, 11, tab2, "however a wipe that includes a prng may be       " );
                mvwprintw( main_window, 12, tab2, "preferable.                                      " );
                break;

            case 1:

                mvwprintw( main_window, 2, tab2, "Security Level: high (1 pass)" );

                mvwprintw( main_window, 4, tab2, "This method fills the device with ones. Note that" );
                mvwprintw( main_window, 5, tab2, "the rounds option does not apply to this method. " );
                mvwprintw( main_window, 6, tab2, "This method always runs one round.               " );
                mvwprintw( main_window, 7, tab2, "                                                 " );
                mvwprintw( main_window, 8, tab2, "This method might be used when wiping a solid    " );
                mvwprintw( main_window, 9, tab2, "state drive if an additional level of security is" );
                mvwprintw( main_window, 10, tab2, "required beyond using the drives internal secure " );
                mvwprintw( main_window, 11, tab2, "erase features. Alternatively PRNG may be        " );
                mvwprintw( main_window, 12, tab2, "preferable.                                      " );
                break;

            case 2:

                mvwprintw( main_window, 2, tab2, "Security Level: higher (8 passes)" );

                mvwprintw( main_window, 4, tab2, "The Royal Canadian Mounted Police Technical      " );
                mvwprintw( main_window, 5, tab2, "Security Standard for Information Technology.    " );
                mvwprintw( main_window, 6, tab2, "Appendix OPS-II: Media Sanitization.             " );
                mvwprintw( main_window, 7, tab2, "                                                 " );
                mvwprintw( main_window, 8, tab2, "This implementation, with regards to paragraph 2 " );
                mvwprintw( main_window, 9, tab2, "section A of the standard, uses a pattern that is" );
                mvwprintw( main_window, 10, tab2, "one random byte and that is changed each round.  " );
                break;

            case 3:

                mvwprintw( main_window, 2, tab2, "Security Level: higher (3 passes)" );

                mvwprintw( main_window, 4, tab2, "The US Department of Defense 5220.22-M short wipe" );
                mvwprintw( main_window, 5, tab2, "This method is composed of passes 1, 2 & 7 from  " );
                mvwprintw( main_window, 6, tab2, "the standard DoD 5220.22-M wipe.                 " );
                mvwprintw( main_window, 7, tab2, "                                                 " );
                mvwprintw( main_window, 8, tab2, "Pass 1: A random character                       " );
                mvwprintw( main_window, 9, tab2, "Pass 2: The bitwise complement of pass 1.        " );
                mvwprintw( main_window, 10, tab2, "Pass 3: A random number generated data stream    " );
                break;

            case 4:

                mvwprintw( main_window, 2, tab2, "Security Level: higher (7 passes)" );

                mvwprintw( main_window, 3, tab2, "The American Department of Defense 5220.22-M     " );
                mvwprintw( main_window, 4, tab2, "standard wipe.                                   " );
                mvwprintw( main_window, 5, tab2, "                                                 " );
                mvwprintw( main_window, 6, tab2, "Pass 1: A Random character                       " );
                mvwprintw( main_window, 7, tab2, "Pass 2: The bitwise complement of pass 1         " );
                mvwprintw( main_window, 8, tab2, "Pass 3: A random number generated data stream    " );
                mvwprintw( main_window, 9, tab2, "Pass 4: A Random character                       " );
                mvwprintw( main_window, 10, tab2, "Pass 5: A Random character                       " );
                mvwprintw( main_window, 11, tab2, "Pass 6: The bitwise complement of pass 5         " );
                mvwprintw( main_window, 12, tab2, "Pass 7: A random number generated data stream    " );
                break;

            case 5:

                mvwprintw( main_window, 2, tab2, "Security Level: Paranoid ! (35 passes)           " );
                mvwprintw( main_window, 3, tab2, "Don't waste your time with this on a modern drive" );

                mvwprintw( main_window, 5, tab2, "This is the method described by Peter Gutmann in " );
                mvwprintw( main_window, 6, tab2, "the paper entitled \"Secure Deletion of Data from" );
                mvwprintw( main_window, 7, tab2, "Magnetic and Solid-State Memory\", however not   " );
                mvwprintw( main_window, 8, tab2, "relevant in regards to modern hard disk drives.  " );
                break;

            case 6:

                mvwprintw( main_window, 2, tab2, "Security Level: Depends on Rounds" );

                mvwprintw( main_window, 4, tab2, "This method fills the device with a stream from  " );
                mvwprintw( main_window, 5, tab2, "the PRNG. It is probably the best method to use  " );
                mvwprintw( main_window, 6, tab2, "on modern hard disk drives due to variation in   " );
                mvwprintw( main_window, 7, tab2, "encoding methods.                                " );
                mvwprintw( main_window, 8, tab2, "                                                 " );
                mvwprintw( main_window, 9, tab2, "This method has a high security level with 1     " );
                mvwprintw( main_window, 10, tab2, "round and an increasingly higher security level " );
                mvwprintw( main_window, 11, tab2, "as rounds are increased." );
                break;

            case 7:

                mvwprintw( main_window, 2, tab2, "Security Level: Not applicable" );

                mvwprintw( main_window, 4, tab2, "This method only reads the device and checks     " );
                mvwprintw( main_window, 5, tab2, "that it is all zero.                             " );

                break;

            case 8:

                mvwprintw( main_window, 2, tab2, "Security Level: Not applicable" );

                mvwprintw( main_window, 4, tab2, "This method only reads the device and checks     " );
                mvwprintw( main_window, 5, tab2, "that it is all ones (0xFF)." );

                break;

            case 9:

                mvwprintw( main_window, 2, tab2, "Security Level: higher (3 passes)" );

                mvwprintw( main_window, 4, tab2, "HMG IA/IS 5 (Infosec Standard 5): Secure         " );
                mvwprintw( main_window, 5, tab2, "Sanitisation of Protectively Marked Information  " );
                mvwprintw( main_window, 6, tab2, "or Sensitive Information                         " );
                mvwprintw( main_window, 7, tab2, "                                                 " );
                mvwprintw( main_window, 8, tab2, "This method fills the device with 0s, then with  " );
                mvwprintw( main_window, 9, tab2, "1s, then with a PRNG stream, then reads the      " );
                mvwprintw( main_window, 10, tab2, "device to verify the PRNG stream was             " );
                mvwprintw( main_window, 11, tab2, "successfully written.                            " );
                break;

        } /* switch */

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Wipe Method " );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 ); /* block getch() for 250ms */
        keystroke = getch(); /* Get a keystroke. */
        timeout( -1 ); /* Switch back to blocking mode */

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':

                if( focus < count - 1 )
                {
                    focus += 1;
                }
                break;

            case KEY_UP:
            case 'k':
            case 'K':

                if( focus > 0 )
                {
                    focus -= 1;
                }
                break;

            case KEY_BACKSPACE:
            case KEY_BREAK:

                return;

        } /* switch */

    } while( keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10 && terminate_signal != 1 );

    switch( focus )
    {
        case 0:
            nwipe_options.method = &nwipe_zero;
            break;

        case 1:
            nwipe_options.method = &nwipe_one;
            break;

        case 2:
            nwipe_options.method = &nwipe_ops2;
            break;

        case 3:
            nwipe_options.method = &nwipe_dodshort;
            break;

        case 4:
            nwipe_options.method = &nwipe_dod522022m;
            break;

        case 5:
            nwipe_options.method = &nwipe_gutmann;
            break;

        case 6:
            nwipe_options.method = &nwipe_random;
            break;

        case 7:
            nwipe_options.method = &nwipe_verify_zero;
            break;

        case 8:
            nwipe_options.method = &nwipe_verify_one;
            break;

        case 9:
            nwipe_options.method = &nwipe_is5enh;
            break;
    }

} /* nwipe_gui_method */

void nwipe_gui_config( void )
{
    /**
     * Display the configuration Main Menu selection window
     *
     */

    extern int terminate_signal;

    /* Number of entries in the configuration menu. */
    const int count = 8;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 40;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    do
    {
        /* Clear the main window. */
        werase( main_window );

        /* Update the footer window. */
        werase( footer_window );
        nwipe_gui_title( footer_window, selection_footer_config );
        wrefresh( footer_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_config );

        /* Initialize the working row. */
        yy = 2;

        /* Print the options. */
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Enable/Disable   " );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Edit Organisation" );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Select Customer  " );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Add Customer     " );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Delete Customer  " );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Preview Details  " );
        mvwprintw( main_window, yy++, tab1, "  %s", "PDF Report - Preview at Start " );
        yy++;
        mvwprintw( main_window, yy++, tab1, "  %s", "Set System Date & Time        " );
        mvwprintw( main_window, yy++, tab1, "                                      " );

        /* Print the cursor. */
        mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

        switch( focus )
        {
            case 0:

                if( nwipe_options.PDF_enable )
                {
                    mvwprintw( main_window, 2, tab2, "PDF Report = ENABLED" );
                }
                else
                {
                    mvwprintw( main_window, 2, tab2, "PDF Report = DISABLED" );
                }

                mvwprintw( main_window, 4, tab2, "Enable or Disable creation of the PDF " );
                mvwprintw( main_window, 5, tab2, "report/certificate                    " );
                break;

            case 1:

                mvwprintw( main_window, 2, tab2, "PDF Report - Edit Organisation" );

                mvwprintw( main_window, 4, tab2, "This option allows you to edit details" );
                mvwprintw( main_window, 5, tab2, "of the organisation that is performing" );
                mvwprintw( main_window, 6, tab2, "the erasure. This includes: business  " );
                mvwprintw( main_window, 7, tab2, "name, business address, contact name  " );
                mvwprintw( main_window, 8, tab2, "and contact phone.                    " );
                break;

            case 2:
                mvwprintw( main_window, 2, tab2, "PDF Report - Select Customer" );

                mvwprintw( main_window, 4, tab2, "Allows selection of a customer as     " );
                mvwprintw( main_window, 5, tab2, "displayed on the PDF report. Customer " );
                mvwprintw( main_window, 6, tab2, "information includes Name (This can be" );
                mvwprintw( main_window, 7, tab2, "a personal or business name), address " );
                mvwprintw( main_window, 8, tab2, "contact name and contact phone.       " );
                mvwprintw( main_window, 9, tab2, "                                      " );
                mvwprintw( main_window, 10, tab2, "Customer data is located in:         " );
                mvwprintw( main_window, 11, tab2, "/etc/nwipe/nwipe_customers.csv       " );
                break;

            case 3:

                mvwprintw( main_window, 2, tab2, "PDF Report - Add Customer     " );

                mvwprintw( main_window, 4, tab2, "This option allows you to add a new   " );
                mvwprintw( main_window, 5, tab2, "customer. A customer can be optionally" );
                mvwprintw( main_window, 6, tab2, "displayed on the PDF report. Customer " );
                mvwprintw( main_window, 7, tab2, "information includes Name (This can be" );
                mvwprintw( main_window, 8, tab2, "a personal or business name), address " );
                mvwprintw( main_window, 9, tab2, "contact name and contact phone.       " );
                mvwprintw( main_window, 10, tab2, "                                      " );
                mvwprintw( main_window, 11, tab2, "Customer data is saved in:            " );
                mvwprintw( main_window, 12, tab2, "/etc/nwipe/nwipe_customers.csv        " );
                break;

            case 4:

                mvwprintw( main_window, 2, tab2, "PDF Report - Delete Customer  " );

                mvwprintw( main_window, 4, tab2, "This option allows you to delete a    " );
                mvwprintw( main_window, 5, tab2, "customer. A customer can be optionally" );
                mvwprintw( main_window, 6, tab2, "displayed on the PDF report. Customer " );
                mvwprintw( main_window, 7, tab2, "information includes Name (This can be" );
                mvwprintw( main_window, 8, tab2, "a personal or business name), address " );
                mvwprintw( main_window, 9, tab2, "contact name and contact phone.       " );
                mvwprintw( main_window, 10, tab2, "                                      " );
                mvwprintw( main_window, 11, tab2, "Customer data is saved in:            " );
                mvwprintw( main_window, 12, tab2, "/etc/nwipe/nwipe_customers.csv        " );
                break;

            case 5:

                mvwprintw( main_window, 2, tab2, "PDF Report - Preview Organisation,    " );
                mvwprintw( main_window, 3, tab2, "Customer and Date/Time details        " );

                mvwprintw( main_window, 5, tab2, "This allows the above information to  " );
                mvwprintw( main_window, 6, tab2, "be checked prior to starting the wipe " );
                mvwprintw( main_window, 7, tab2, "so that the information is correct on " );
                mvwprintw( main_window, 8, tab2, "the pdf report.                       " );
                break;

            case 6:

                if( nwipe_options.PDF_preview_details )
                {
                    mvwprintw( main_window, 2, tab2, "Preview Org. & Customer at start = ENABLED" );
                }
                else
                {
                    mvwprintw( main_window, 2, tab2, "Preview Org. & Customer at start = DISABLED" );
                }
                mvwprintw( main_window, 4, tab2, "A preview prior to the drive selection" );
                mvwprintw( main_window, 5, tab2, "of the organisation that is performing" );
                mvwprintw( main_window, 6, tab2, "the wipe, the customer details and the" );
                mvwprintw( main_window, 7, tab2, "current date and time in order to     " );
                mvwprintw( main_window, 8, tab2, "confirm that the information is       " );
                mvwprintw( main_window, 9, tab2, "correct on the pdf report prior to    " );
                mvwprintw( main_window, 10, tab2, "drive selection and starting the erase" );
                break;

            case 8:

                mvwprintw( main_window, 2, tab2, "Set System Date & Time                " );

                mvwprintw( main_window, 4, tab2, "Useful when host is not connected to  " );
                mvwprintw( main_window, 5, tab2, "the internet and not running the 'ntp'" );
                mvwprintw( main_window, 6, tab2, "(network time protocol).              " );
                break;
        } /* switch */

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Configuration " );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 ); /* block getch() for 250ms */
        keystroke = getch(); /* Get a keystroke. */
        timeout( -1 ); /* Switch back to blocking mode */

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':

                if( focus < count - 1 )
                {
                    if( focus == 6 )
                    {
                        focus += 2; /* mind the gaps */
                    }
                    else
                    {
                        focus += 1;
                    }
                }
                break;

            case KEY_UP:
            case 'k':
            case 'K':

                if( focus > 0 )
                {
                    if( focus == 8 )
                    {
                        focus -= 2; /* mind the gaps */
                    }
                    else
                    {
                        focus -= 1;
                    }
                }
                break;

            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */

                return;

        } /* switch */

        if( keystroke == 0x0A )
        {
            switch( focus )
            {
                case 0:
                    /* Toggle on pressing ENTER key */
                    if( nwipe_options.PDF_enable == 0 )
                    {
                        nwipe_options.PDF_enable = 1;

                        /* write the setting to nwipe.conf */
                        nwipe_conf_update_setting( "PDF_Certificate.PDF_Enable", "ENABLED" );
                    }
                    else
                    {
                        nwipe_options.PDF_enable = 0;

                        /* write the setting to nwipe.conf */
                        nwipe_conf_update_setting( "PDF_Certificate.PDF_Enable", "DISABLED" );
                    }
                    break;

                case 1:
                    nwipe_gui_edit_organisation();
                    break;

                case 2:
                    customer_processes( SELECT_CUSTOMER );

                    break;

                case 3:
                    nwipe_gui_add_customer();
                    break;

                case 4:
                    customer_processes( DELETE_CUSTOMER );
                    break;

                case 5:
                    nwipe_gui_preview_org_customer( SHOWING_IN_CONFIG_MENUS );
                    break;

                case 6:
                    /* Toggle on pressing ENTER key */
                    if( nwipe_options.PDF_preview_details == 0 )
                    {
                        nwipe_options.PDF_preview_details = 1;

                        /* write the setting to nwipe.conf */
                        nwipe_conf_update_setting( "PDF_Certificate.PDF_Preview", "ENABLED" );
                    }
                    else
                    {
                        nwipe_options.PDF_preview_details = 0;

                        /* write the setting to nwipe.conf */
                        nwipe_conf_update_setting( "PDF_Certificate.PDF_Preview", "DISABLED" );
                    }
                    break;

                case 8:
                    nwipe_gui_set_date_time();
                    break;
            }
            keystroke = -1;
        }

    } while( keystroke != KEY_ENTER && /* keystroke != ' ' && */ keystroke != 10 && terminate_signal != 1 );

} /* end of nwipe_config() */

void nwipe_gui_edit_organisation( void )
{
    /**
     * Display the list of organisation details available for editing
     *
     */

    extern int terminate_signal;

    /* Number of entries in the configuration menu. */
    const int count = 5;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 27;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* variables used by libconfig for extracting data from nwipe.conf */
    config_setting_t* setting;
    const char *business_name, *business_address, *contact_name, *contact_phone, *op_tech_name;
    extern config_t nwipe_cfg;

    do
    {
        do
        {
            /* Clear the main window. */
            werase( main_window );

            /* Update the footer window. */
            werase( footer_window );
            nwipe_gui_title( footer_window, selection_footer_config );
            wrefresh( footer_window );

            nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_config );

            /* Initialize the working row. */
            yy = 2;

            /* Print the options. */
            mvwprintw( main_window, yy++, tab1, "  %s", "Edit Business Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Edit Business Address" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Edit Contact Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Edit Contact Phone" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Edit Tech/Operator" );
            mvwprintw( main_window, yy++, tab1, "                             " );

            /* Print the cursor. */
            mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

            /* libconfig: Locate the Organisation Details section in nwipe.conf */
            setting = config_lookup( &nwipe_cfg, "Organisation_Details" );

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Business_Name", &business_name ) )
            {
                mvwprintw( main_window, 2, tab2, ": %s", business_name );
            }
            else
            {
                mvwprintw( main_window, 2, tab2, ": Cannot retrieve business_name, nwipe.conf" );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Business_Address", &business_address ) )
            {
                mvwprintw( main_window, 3, tab2, ": %s", business_address );
            }
            else
            {
                mvwprintw( main_window, 3, tab2, ": Cannot retrieve business address, nwipe.conf" );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
            {
                mvwprintw( main_window, 4, tab2, ": %s", contact_name );
            }
            else
            {
                mvwprintw( main_window, 4, tab2, ": Cannot retrieve contact name, nwipe.conf" );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
            {
                mvwprintw( main_window, 5, tab2, ": %s", contact_phone );
            }
            else
            {
                mvwprintw( main_window, 5, tab2, ": Cannot retrieve contact phone, nwipe.conf" );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Op_Tech_Name", &op_tech_name ) )
            {
                mvwprintw( main_window, 6, tab2, ": %s", op_tech_name );
            }
            else
            {
                mvwprintw( main_window, 6, tab2, ": Cannot retrieve op_tech_name, nwipe.conf" );
            }

            /* Add a border. */
            box( main_window, 0, 0 );

            /* Add a title. */
            nwipe_gui_title( main_window, " PDF Report - Edit Organisation " );

            /* Refresh the window. */
            wrefresh( main_window );

            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */
            timeout( 250 ); /* block getch() for 250ms */
            keystroke = getch(); /* Get a keystroke. */
            timeout( -1 ); /* Switch back to blocking mode */

            switch( keystroke )
            {
                case KEY_DOWN:
                case 'j':
                case 'J':

                    if( focus < count - 1 )
                    {
                        focus += 1;
                    }
                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    if( focus > 0 )
                    {
                        focus -= 1;
                    }
                    break;

                case KEY_BACKSPACE:
                case KEY_BREAK:
                case 27: /* ESC */

                    return;

            } /* switch */

        } while( keystroke != KEY_ENTER && keystroke != 10 && terminate_signal != 1 );

        if( keystroke == KEY_ENTER || keystroke == 10 || keystroke == ' ' )
        {
            switch( focus )
            {
                case 0:
                    nwipe_gui_organisation_business_name( business_name );
                    keystroke = 0;
                    break;

                case 1:
                    nwipe_gui_organisation_business_address( business_address );
                    keystroke = 0;
                    break;

                case 2:
                    nwipe_gui_organisation_contact_name( contact_name );
                    keystroke = 0;
                    break;

                case 3:
                    nwipe_gui_organisation_contact_phone( contact_phone );
                    keystroke = 0;
                    break;

                case 4:
                    nwipe_gui_organisation_op_tech_name( op_tech_name );
                    keystroke = 0;
                    break;
            }
        }

    } while( keystroke != KEY_ENTER && keystroke != 10 && terminate_signal != 1 );

} /* end of nwipe_gui_edit_organisation( void ) */

void nwipe_gui_organisation_business_name( const char* business_name )
{
    /**
     * Allows the user to change the organisation business name as displayed on the PDF report.
     *
     * @modifies  business_name in nwipe.conf
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer */
    char buffer[256] = "";

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    /* variables used by libconfig for inserting data into nwipe.conf */
    config_setting_t* setting;
    // const char* business_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Copy the current business name to the buffer */
    strcpy( buffer, business_name );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Edit Organisation Business Name " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the business name of the organisation performing the erasure" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < 255 )
        {
            buffer[idx++] = keystroke;
            buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* libconfig: Locate the Organisation Details section in nwipe.conf */
    if( !( setting = config_lookup( &nwipe_cfg, "Organisation_Details.Business_Name" ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to locate [Organisation_Details.Business_Name] in %s", nwipe_config_file );
    }

    /* libconfig: Write the new business name */
    if( config_setting_set_string( setting, buffer ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to write [%s] to [Organisation_Details.Business_Name] in %s",
                   buffer,
                   nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( config_write_file( &nwipe_cfg, nwipe_config_file ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write organisation business name to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "[Success] Business name written to %s", nwipe_config_file );
    }

} /* End of nwipe_gui_organisation_business_name() */

void nwipe_gui_organisation_business_address( const char* business_address )
{
    /**
     * Allows the user to change the organisation business address as displayed on the PDF report.
     *
     * @modifies  business_address in nwipe.conf
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer */
    char buffer[256] = "";

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    /* variables used by libconfig for inserting data into nwipe.conf */
    config_setting_t* setting;
    // const char* business_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Copy the current business address to the buffer */
    strcpy( buffer, business_address );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Edit Organisation Business Address " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the business address of the organisation performing the erasure" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < 255 )
        {
            buffer[idx++] = keystroke;
            buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* libconfig: Locate the Organisation Details section in nwipe.conf */
    if( !( setting = config_lookup( &nwipe_cfg, "Organisation_Details.Business_Address" ) ) )
    {
        nwipe_log(
            NWIPE_LOG_ERROR, "Failed to locate [Organisation_Details.Business_Address] in %s", nwipe_config_file );
    }

    /* libconfig: Write the new business name */
    if( config_setting_set_string( setting, buffer ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to write [%s] to [Organisation_Details.Business_Address] in %s",
                   buffer,
                   nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( config_write_file( &nwipe_cfg, nwipe_config_file ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write organisation business address to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "[Success] Business address written to %s", nwipe_config_file );
    }

} /* End of nwipe_gui_organisation_business_address() */

void nwipe_gui_organisation_contact_name( const char* contact_name )
{
    /**
     * Allows the user to change the organisation business address as displayed on the PDF report.
     *
     * @modifies  business_address in nwipe.conf
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer */
    char buffer[256] = "";

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    /* variables used by libconfig for inserting data into nwipe.conf */
    config_setting_t* setting;
    // const char* business_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Copy the current business address to the buffer */
    strcpy( buffer, contact_name );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Edit Organisation Contact Name " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the contact name for the organisation performing the erasure" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < 255 )
        {
            buffer[idx++] = keystroke;
            buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* libconfig: Locate the Organisation Details section in nwipe.conf */
    if( !( setting = config_lookup( &nwipe_cfg, "Organisation_Details.Contact_Name" ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to locate [Organisation_Details.Contact_Name] in %s", nwipe_config_file );
    }

    /* libconfig: Write the new organisation contact name */
    if( config_setting_set_string( setting, buffer ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to write [%s] to [Organisation_Details.Contact_Name] in %s",
                   buffer,
                   nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( config_write_file( &nwipe_cfg, nwipe_config_file ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write organisation contact name to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "[Success] Business contact name written to %s", nwipe_config_file );
    }

} /* End of nwipe_gui_organisation_contact_name() */

void nwipe_gui_organisation_contact_phone( const char* contact_phone )
{
    /**
     * Allows the user to change the organisation contact name as displayed on the PDF report.
     *
     * @modifies  organisation contact name in nwipe.conf
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer */
    char buffer[256] = "";

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    /* variables used by libconfig for inserting data into nwipe.conf */
    config_setting_t* setting;
    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Copy the current business address to the buffer */
    strcpy( buffer, contact_phone );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Edit Organisation Contact Phone " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the contact phone for the organisation performing the erasure" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < 255 )
        {
            buffer[idx++] = keystroke;
            buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* libconfig: Locate the Organisation Details section in nwipe.conf */
    if( !( setting = config_lookup( &nwipe_cfg, "Organisation_Details.Contact_Phone" ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to locate [Organisation_Details.Contact_Phone] in %s", nwipe_config_file );
    }

    /* libconfig: Write the organistion contact phone */
    if( config_setting_set_string( setting, buffer ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to write [%s] to [Organisation_Details.Contact_Phone] in %s",
                   buffer,
                   nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( config_write_file( &nwipe_cfg, nwipe_config_file ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write organisation contact phone to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "[Success] Business contact phone written to %s", nwipe_config_file );
    }

} /* End of nwipe_gui_organisation_contact_phone() */

void nwipe_gui_organisation_op_tech_name( const char* op_tech_name )
{
    /**
     * Allows the user to change the organisation contact name as displayed on the PDF report.
     *
     * @modifies  organisation contact name in nwipe.conf
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer */
    char buffer[256] = "";

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    /* variables used by libconfig for inserting data into nwipe.conf */
    config_setting_t* setting;
    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Copy the current op_tech_name to the buffer */
    strcpy( buffer, op_tech_name );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Edit Operator/Technician Name " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the operator/technician's name that is performing the erasure" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < 255 )
        {
            buffer[idx++] = keystroke;
            buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* libconfig: Locate the Organisation Details section in nwipe.conf */
    if( !( setting = config_lookup( &nwipe_cfg, "Organisation_Details.Op_Tech_Name" ) ) )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to locate [Organisation_Details.Op_Tech_Name] in %s", nwipe_config_file );
    }

    /* libconfig: Write the organistion operator/technician name */
    if( config_setting_set_string( setting, buffer ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR,
                   "Failed to write [%s] to [Organisation_Details.Op_Tech_Name] in %s",
                   buffer,
                   nwipe_config_file );
    }

    /* Write out the new configuration. */
    if( config_write_file( &nwipe_cfg, nwipe_config_file ) == CONFIG_FALSE )
    {
        nwipe_log( NWIPE_LOG_ERROR, "Failed to write organisation operator/technician to %s", nwipe_config_file );
    }
    else
    {
        nwipe_log( NWIPE_LOG_INFO, "[Success] operator/technician name written to %s", nwipe_config_file );
    }

} /* End of nwipe_gui_organisation_op_tech_name() */

void nwipe_gui_list( int count, char* window_title, char** list, int* selected_entry )
{
    /**
     * Displays a selectable list in a window, return 1 -n in selected entry.
     * If selected entry = 0, then user cancelled selection.
     */

    extern int terminate_signal;

    /* The number of lines available in the window. */
    int wlines;

    /* The number of columns available in the window. */
    int wcols;

    /* The number of selection elements that we can show in the window. */
    int slots;

    /* The index of the element that is visible in the first slot. */
    int offset = 0;

    /* The selection focus. */
    int focus = 0;

    /* A generic loop variable. */
    int i = 0;

    /* User input buffer. */
    int keystroke;

    /* The current working line. */
    int yy;

    /* Flag, Valid key hit = 1, anything else = 0 */
    int validkeyhit;

    /* Processed customer entry as displayed in selection dialog */
    char* display_line;

    /* Get the terminal size */
    getmaxyx( stdscr, stdscr_lines, stdscr_cols );

    /* Save the terminal size so we check whether the user has resized */
    stdscr_lines_previous = stdscr_lines;
    stdscr_cols_previous = stdscr_cols;

    /* Used to refresh the window every second */
    time_t check_time = time( NULL );

    /* Used in the selection loop to trap a failure of the timeout(), getch() mechanism to block for the designated
     * period */
    int iteration_counter;

    /* Used in the selection loop to trap a failure of the timeout(), getch() mechanism to block for the designated
     * period */
    int expected_iterations;

    /* General Indexes */
    int idx, idx2;

    time_t previous_iteration_timestamp;

    do
    {

        nwipe_gui_create_all_windows_on_terminal_resize( 0, main_window_footer );

        /* There is one slot per line. */
        getmaxyx( main_window, wlines, wcols );

        /* Less two lines for the box and two lines for padding. */
        slots = wlines - 4;
        if( slots < 0 )
        {
            slots = 0;
        }

        /* The code here adjusts the offset value, required when the terminal is resized vertically */
        if( slots > count )
        {
            offset = 0;
        }
        else
        {
            if( focus >= count )
            {
                /* The focus is already at the last element. */
                focus = count - 1;
            }
            if( focus < 0 )
            {
                /* The focus is already at the last element. */
                focus = 0;
            }
        }

        if( count >= slots && slots > 0 )
        {
            offset = focus + 1 - slots;
            if( offset < 0 )
            {
                offset = 0;
            }
        }

        /* Clear the main window, necessary when switching selections such as method etc */
        werase( main_window );

        /* Refresh main window */
        wnoutrefresh( main_window );

        /* Set footer help text */
        /* Update the footer window. */
        werase( footer_window );
        nwipe_gui_title( footer_window, selection_footer );
        wrefresh( footer_window );

        /* Refresh the stats window */
        wnoutrefresh( stats_window );

        /* Refresh the options window */
        wnoutrefresh( options_window );

        /* Update the options window. */
        nwipe_gui_options();

        /* Initialize the line offset. */
        yy = 2;

        for( i = 0; i < slots && i < count; i++ )
        {
            /* Move to the next line. */
            mvwprintw( main_window, yy++, 1, " " );

            if( i + offset == focus )
            {
                /* Print the 'enabled' cursor. */
                waddch( main_window, ACS_RARROW );
            }

            else
            {
                /* Print whitespace. */
                waddch( main_window, ' ' );
            }

            /* In the event for the offset value somehow becoming invalid, this if statement will prevent a segfault
             * and the else part will log the out of bounds values for debugging */
            if( i + offset >= 0 && i + offset < count )
            {
                /* print a entry from the list, we need to process the string before display,
                 * removing the double quotes that are used in csv for identifying the start & end of a field.
                 */
                if( ( display_line = calloc( sizeof( char ), strlen( list[i + offset] ) ) ) )
                {
                    idx = 0;
                    idx2 = 0;
                    while( list[i + offset][idx] != 0 )
                    {
                        if( list[i + offset][idx] == '"' )
                        {
                            idx++;
                        }
                        else
                        {
                            display_line[idx2++] = list[i + offset][idx++];
                        }
                    }
                    display_line[idx2] = 0;
                    wprintw( main_window, "%s ", display_line );
                    free( display_line );
                }
            }
            else
            {
                nwipe_log( NWIPE_LOG_DEBUG,
                           "GUI.c,nwipe_gui_select(), scroll, array index out of bounds, i=%u, count=%u, slots=%u, "
                           "focus=%u, offset=%u",
                           i,
                           count,
                           slots,
                           focus,
                           offset );
            }

        } /* for */

        if( offset > 0 )
        {
            mvwprintw( main_window, 1, wcols - 8, " More " );
            waddch( main_window, ACS_UARROW );
        }

        if( count - offset > slots )
        {
            mvwprintw( main_window, wlines - 2, wcols - 8, " More " );
            waddch( main_window, ACS_DARROW );
        }

        /* Draw a border around the menu window. */
        box( main_window, 0, 0 );

        /* Print a title. */
        nwipe_gui_title( main_window, window_title );

        /* Refresh the window. */
        wnoutrefresh( main_window );

        /* Output to physical screen */
        doupdate();

        /* Initialise the iteration counter */
        iteration_counter = 0;

        previous_iteration_timestamp = time( NULL );

        /* Calculate Maximum allowed iterations per second */
        expected_iterations = ( 1000 / GETCH_BLOCK_MS ) * 8;

        do
        {
            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */

            validkeyhit = 0;
            timeout( GETCH_BLOCK_MS );  // block getch() for ideally about 250ms.
            keystroke = getch();  // Get user input.
            timeout( -1 );  // Switch back to blocking mode.

            /* To avoid 100% CPU usage, check for a runaway condition caused by the "keystroke = getch(); (above), from
             * immediately returning an error condition. We check for an error condition because getch() returns a ERR
             * value when the timeout value "timeout( 250 );" expires as well as when a real error occurs. We can't
             * differentiate from normal operation and a failure of the getch function to block for the specified period
             * of timeout. So here we check the while loop hasn't exceeded the number of expected iterations per second
             * ie. a timeout(250) block value of 250ms means we should not see any more than (1000/250) = 4 iterations.
             * We increase this to 32 iterations to allow a little tolerance. Why is this necessary? It's been found
             * that in KDE konsole and other terminals based on the QT terminal engine exiting the terminal without
             * first exiting nwipe results in nwipe remaining running but detached from any interface which causes
             * getch to fail and its associated timeout. So the CPU or CPU core rises to 100%. Here we detect that
             * failure and exit nwipe gracefully with the appropriate error. This does not affect use of tmux for
             * attaching or detaching from a running nwipe session when sitting at the selection screen. All other
             * terminals correctly terminate nwipe when the terminal itself is exited.
             */

            iteration_counter++;

            if( previous_iteration_timestamp == time( NULL ) )
            {
                if( iteration_counter > expected_iterations )
                {
                    nwipe_log( NWIPE_LOG_ERROR,
                               "GUI.c,nwipe_gui_select(), loop runaway, did you close the terminal without exiting "
                               "nwipe? Exiting nwipe now." );
                    /* Issue signal to nwipe to exit immediately but gracefully */
                    terminate_signal = 1;
                }
            }
            else
            {
                /* new second, so reset counter */
                iteration_counter = 0;
                previous_iteration_timestamp = time( NULL );
            }

            /* We don't necessarily use all of these. For future reference these are some CTRL+key values
             * ^A - 1, ^B - 2, ^D - 4, ^E - 5, ^F - 6, ^G - 7, ^H - 8, ^I - 9, ^K - 11, ^L - 12, ^N - 14,
             * ^O - 15, ^P - 16, ^R - 18, ^T - 20, ^U - 21, ^V - 22, ^W - 23, ^X - 24, ^Y - 25
             * Use nwipe_log( NWIPE_LOG_DEBUG, "Key Name: %s - %u", keyname(keystroke),keystroke) to
             * figure out what code is returned by what ever key combination */

            switch( keystroke )
            {
                case KEY_DOWN:
                case 'j':
                case 'J':

                    validkeyhit = 1;

                    /* Increment the focus. */
                    focus += 1;

                    if( focus >= count )
                    {
                        /* The focus is already at the last element. */
                        focus = count - 1;
                        break;
                    }

                    if( focus - offset >= slots )
                    {
                        /* The next element is offscreen. Scroll down. */
                        offset += 1;
                        break;
                    }

                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    validkeyhit = 1;

                    /* Decrement the focus. */
                    focus -= 1;

                    if( focus < 0 )
                    {
                        /* The focus is already at the last element. */
                        focus = 0;
                        break;
                    }

                    if( focus < offset )
                    {
                        /* The next element is offscreen. Scroll up. */
                        offset -= 1;
                        break;
                    }

                    break;

                case KEY_BACKSPACE:
                case KEY_LEFT:
                case 127:
                    *selected_entry = 0;
                    return;
                    break;

                case KEY_ENTER:
                case 10:
                case ' ':

                    validkeyhit = 1;

                    /* Return the index of the selected item in the list. Values start at 1 not zero */
                    *selected_entry = focus + 1;
                    return;

                    break;

            } /* keystroke switch */

            /* Check the terminal size, if the user has changed it the while loop checks for
             * this change and exits the valid key hit loop so the windows can be updated */
            getmaxyx( stdscr, stdscr_lines, stdscr_cols );

            /* Update the selection window every 1 second specifically
             * so that the drive temperatures are updated and also the line toggle that
             * occurs with the HPA status and the drive size & temperature.
             */
            if( time( NULL ) > ( check_time + 1 ) )
            {
                check_time = time( NULL );
                validkeyhit = 1;
            }

        } /* key hit loop */
        while( validkeyhit == 0 && terminate_signal != 1 && stdscr_cols_previous == stdscr_cols
               && stdscr_lines_previous == stdscr_lines );

    } while( terminate_signal != 1 );

} /* nwipe_gui_list */

void nwipe_gui_add_customer( void )
{
    /**
     * Add new customer top level menu
     *
     */

    extern int terminate_signal;

    /* Number of entries in the configuration menu. */
    const int count = 4;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 27;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    char customer_name[FIELD_LENGTH] = "";
    char customer_address[FIELD_LENGTH] = "";
    char customer_contact_name[FIELD_LENGTH] = "";
    char customer_contact_phone[FIELD_LENGTH] = "";

    /* 0 = NO = don't save, 1 = YES = save customer */
    int save = NO;

    /* 0 = Display standard dialog footer, 1 = YES = display "Save Y/N" footer */
    int yes_no = NO;

    /* variables used by libconfig for extracting data from nwipe.conf */
    config_setting_t* setting;
    // const char *business_name, *business_address, *contact_name, *contact_phone, *op_tech_name;
    extern config_t nwipe_cfg;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_config );
    wrefresh( footer_window );

    do
    {
        do
        {
            /* Clear the main window. */
            werase( main_window );

            /* Change footer based on whether we are waiting for a Y/N response */
            if( yes_no == YES )
            {
                /* Update the footer window. */
                werase( footer_window );
                nwipe_gui_title( footer_window, selection_footer_add_customer_yes_no );
                wrefresh( footer_window );

                nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_add_customer_yes_no );
            }
            else
            {
                /* Update the footer window. */
                werase( footer_window );
                nwipe_gui_title( footer_window, selection_footer_config );
                wrefresh( footer_window );

                nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_config );
            }
            /* Initialize the working row. */
            yy = 2;

            /* Print the options. */
            mvwprintw( main_window, yy++, tab1, "  %s : %s", "Add Customer Name         ", customer_name );
            mvwprintw( main_window, yy++, tab1, "  %s : %s", "Add Customer Address      ", customer_address );
            mvwprintw( main_window, yy++, tab1, "  %s : %s", "Add Customer Contact Name ", customer_contact_name );
            mvwprintw( main_window, yy++, tab1, "  %s : %s", "Add Customer Contact Phone", customer_contact_phone );
            mvwprintw( main_window, yy++, tab1, "                             " );

            /* Print the cursor. */
            mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

            /* Add a border. */
            box( main_window, 0, 0 );

            /* Add a title. */
            nwipe_gui_title( main_window, " PDF Report - Add New Customer " );

            /* Refresh the window. */
            wrefresh( main_window );

            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */
            timeout( 250 ); /* block getch() for 250ms */
            keystroke = getch(); /* Get a keystroke. */
            timeout( -1 ); /* Switch back to blocking mode */

            if( yes_no == NO )
            {
                switch( keystroke )
                {
                    // Save customer
                    case 's':
                    case 'S':
                        break;

                    case KEY_DOWN:
                    case 'j':
                    case 'J':

                        if( focus < count - 1 )
                        {
                            focus += 1;
                        }
                        break;

                    case KEY_UP:
                    case 'k':
                    case 'K':

                        if( focus > 0 )
                        {
                            focus -= 1;
                        }
                        break;

                    case KEY_BACKSPACE:
                    case KEY_BREAK:
                    case 27: /* ESC */

                        /* If the user has entered any text then ask if the customer entries should be saved */
                        if( customer_name[0] != 0 || customer_address[0] != 0 || customer_contact_name[0] != 0
                            || customer_contact_phone[0] != 0 )
                        {
                            /* Set the footer yes/no flag */
                            yes_no = YES;
                        }
                        else
                        {
                            return;
                        }
                        break;

                } /* switch */
            }
            else
            {
                /* Waiting for a Y/N response */
                switch( keystroke )
                {
                    case 'y':
                    case 'Y':
                        save = 1;
                        break;

                    case 'n':
                    case 'N':
                        return;
                        break;
                }
            }

        } while( save != YES && keystroke != 's' && keystroke != KEY_ENTER && keystroke != 10
                 && terminate_signal != 1 );

        if( keystroke == KEY_ENTER || keystroke == 10 )
        {
            switch( focus )
            {
                case 0:
                    nwipe_gui_add_customer_name( customer_name );
                    keystroke = 0;
                    break;

                case 1:
                    nwipe_gui_add_customer_address( customer_address );
                    keystroke = 0;
                    break;

                case 2:
                    nwipe_gui_add_customer_contact_name( customer_contact_name );
                    keystroke = 0;
                    break;

                case 3:
                    nwipe_gui_add_customer_contact_phone( customer_contact_phone );
                    keystroke = 0;
                    break;
            }
        }

    } while( save != YES && keystroke != 's' && keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10
             && terminate_signal != 1 );

    /* If save set, or user pressed s or S then save the customer details */
    if( keystroke == 's' || keystroke == 'S' || save == 1 )
    {
        write_customer_csv_entry( customer_name, customer_address, customer_contact_name, customer_contact_phone );
    }

} /* end of nwipe_gui_add_customer( void ) */

void nwipe_gui_add_customer_name( char* customer_name )
{
    /**
     * Allows the user to change the customer's contact name as displayed on the PDF report.
     *
     * @modifies  customer's contact name
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( customer_name );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Add New Customer Name " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the customer's name (business or personal name)" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", customer_name );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    customer_name[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH )
        {
            customer_name[idx++] = keystroke;
            customer_name[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", customer_name );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

} /* End of nwipe_gui_add_customer_name() */

void nwipe_gui_add_customer_address( char* customer_address )
{
    /**
     * Allows the user to change the customer's address as displayed on the PDF report.
     *
     * @modifies  customer's address
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( customer_address );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Add New Customer Address " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the customer's address" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", customer_address );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    customer_address[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH )
        {
            customer_address[idx++] = keystroke;
            customer_address[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", customer_address );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

} /* End of nwipe_gui_add_customer_address() */

void nwipe_gui_add_customer_contact_name( char* customer_contact_name )
{
    /**
     * Allows the user to change the customer contact name as displayed on the PDF report.
     *
     * @modifies  customer's contact name
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( customer_contact_name );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Add New Customer Contact Name " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the customer's contact name" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", customer_contact_name );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    customer_contact_name[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH )
        {
            customer_contact_name[idx++] = keystroke;
            customer_contact_name[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", customer_contact_name );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

} /* End of nwipe_gui_add_customer_contact_name() */

void nwipe_gui_add_customer_contact_phone( char* customer_contact_phone )
{
    /**
     * Allows the user to change the customer contact phone as displayed on the PDF report.
     *
     * @modifies  customer's contact phone
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    /* buffer index */
    int idx = 0;

    extern int terminate_signal;

    // const char* contact_name;
    extern config_t nwipe_cfg;
    extern char nwipe_config_file[];

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( customer_contact_phone );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Add New Customer Contact Phone " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the customer's contact phone" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", customer_contact_phone );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    customer_contact_phone[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH )
        {
            customer_contact_phone[idx++] = keystroke;
            customer_contact_phone[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", customer_contact_phone );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

} /* End of nwipe_gui_add_customer_contact_phone() */

void nwipe_gui_preview_org_customer( int mode )
{
    /**
     * Display the organisation and customers details and the current system date and time
     *
     */

    extern int terminate_signal;

    /* Number of entries in the configuration menu. */
    const int count = 12;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 27;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    time_t t;

    char output_str[FIELD_LENGTH];

    /* Window dimensions */
    int wlines;
    int wcols;

    /* variables used by libconfig for extracting data from nwipe.conf */
    config_setting_t* setting;
    const char *business_name, *business_address, *contact_name, *contact_phone, *op_tech_name;
    const char *customer_name, *customer_address, *customer_contact_name, *customer_contact_phone;
    extern config_t nwipe_cfg;

    do
    {
        do
        {
            /* Clear the main window. */
            werase( main_window );

            /* Update the footer window. */
            werase( footer_window );
            if( mode == SHOWING_IN_CONFIG_MENUS )
            {
                nwipe_gui_title( footer_window, selection_footer );
                nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer );
            }
            else
            {
                nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_preview_prior_to_drive_selection );
                nwipe_gui_title( footer_window, selection_footer_preview_prior_to_drive_selection );
            }
            wrefresh( footer_window );

            /* Determine size of window */
            getmaxyx( main_window, wlines, wcols );

            /* Initialize the working row. */
            yy = 2;

            /* Print the options. */
            mvwprintw( main_window, yy++, tab1, "  %s", "Business Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Business Address" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Contact Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Contact Phone" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Tech/Operator" );
            yy++;
            mvwprintw( main_window, yy++, tab1, "  %s", "Customer Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Customer Address" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Customer Contact Name" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Customer Contact Phone" );
            yy++;
            mvwprintw( main_window, yy++, tab1, "  %s", "System Date/Time" );

            /* Print the cursor. */
            mvwaddch( main_window, 2 + focus, tab1, ACS_RARROW );

            /******************************************************************
             * libconfig: Locate the Organisation Details section in nwipe.conf
             */

            setting = config_lookup( &nwipe_cfg, "Organisation_Details" );

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Business_Name", &business_name ) )
            {
                str_truncate( wcols, tab2, business_name, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 2, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve business_name, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 2, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Business_Address", &business_address ) )
            {
                str_truncate( wcols, tab2, business_address, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 3, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve business address, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 3, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
            {
                str_truncate( wcols, tab2, contact_name, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 4, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve contact name, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 4, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
            {
                str_truncate( wcols, tab2, contact_phone, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 5, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate(
                    wcols, tab2, "Cannot retrieve customer contact phone, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 5, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Op_Tech_Name", &op_tech_name ) )
            {
                str_truncate( wcols, tab2, op_tech_name, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 6, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve op_tech_name, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 6, tab2, ": %s", output_str );
            }

            /**********************************************************************
             * libconfig: Locate the current customer details section in nwipe.conf
             */
            setting = config_lookup( &nwipe_cfg, "Selected_Customer" );

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Customer_Name", &customer_name ) )
            {
                str_truncate( wcols, tab2, customer_name, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 8, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve Customer_Name, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 8, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Customer_Address", &customer_address ) )
            {
                str_truncate( wcols, tab2, customer_address, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 9, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve customer address, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 9, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Name", &contact_name ) )
            {
                str_truncate( wcols, tab2, contact_name, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 10, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve contact name, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 10, tab2, ": %s", output_str );
            }

            /* Retrieve data from nwipe.conf */
            if( config_setting_lookup_string( setting, "Contact_Phone", &contact_phone ) )
            {
                str_truncate( wcols, tab2, contact_phone, output_str, FIELD_LENGTH );
                mvwprintw( main_window, 11, tab2, ": %s", output_str );
            }
            else
            {
                str_truncate( wcols, tab2, "Cannot retrieve contact phone, nwipe.conf", output_str, FIELD_LENGTH );
                mvwprintw( main_window, 11, tab2, ": %s", output_str );
            }

            /*******************************
             * Retrieve system date and time
             */
            time( &t );
            mvwprintw( main_window, 13, tab2, ": %s", ctime( &t ) );

            /* ************
             * Add a border
             */
            box( main_window, 0, 0 );

            /*************
             * Add a title
             */
            nwipe_gui_title( main_window, " PDF Report - Preview Organisation, customer and date/time " );

            /********************
             * Refresh the window
             */
            wrefresh( main_window );

            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */
            timeout( 250 ); /* block getch() for 250ms */
            keystroke = getch(); /* Get a keystroke. */
            timeout( -1 ); /* Switch back to blocking mode */

            switch( keystroke )
            {
                case KEY_DOWN:
                case 'j':
                case 'J':

                    if( focus < count - 1 )
                    {
                        if( focus == 4 || focus == 9 )
                        {
                            focus += 2; /* mind the gaps */
                        }
                        else
                        {
                            focus += 1;
                        }
                    }
                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    if( focus > 0 )
                    {
                        if( focus == 6 || focus == 11 )
                        {
                            focus -= 2; /* mind the gaps */
                        }
                        else
                        {
                            focus -= 1;
                        }
                    }
                    break;

                case KEY_BACKSPACE:
                case KEY_BREAK:
                case 27: /* ESC */

                    return;
                    break;

                case 'A':
                case 'a':
                    if( mode == SHOWING_PRIOR_TO_DRIVE_SELECTION )
                    {
                        return;
                    }

            } /* switch */

        } while( keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10 && terminate_signal != 1 );

        if( keystroke == KEY_ENTER || keystroke == 10 || keystroke == ' ' )
        {
            switch( focus )
            {
                case 0:
                    nwipe_gui_organisation_business_name( business_name );
                    keystroke = 0;
                    break;

                case 1:
                    nwipe_gui_organisation_business_address( business_address );
                    keystroke = 0;
                    break;

                case 2:
                    nwipe_gui_organisation_contact_name( contact_name );
                    keystroke = 0;
                    break;

                case 3:
                    nwipe_gui_organisation_contact_phone( contact_phone );
                    keystroke = 0;
                    break;

                case 4:
                    nwipe_gui_organisation_op_tech_name( op_tech_name );
                    keystroke = 0;
                    break;

                case 6:
                case 7:
                case 8:
                case 9:
                    nwipe_gui_config();
                    break;

                case 11:
                    nwipe_gui_set_date_time();
            }
        }

    } while( keystroke != 'A' && keystroke != 'a' && keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10
             && terminate_signal != 1 );

} /* end of nwipe_gui_preview_org_customer( void ) */

void nwipe_gui_set_date_time( void )
{
    /**
     * Set system date and time
     *
     */

    extern int terminate_signal;

    /* Number of entries in the configuration menu. */
    const int count = 6;

    /* The first tabstop. */
    const int tab1 = 2;

    /* The second tabstop. */
    const int tab2 = 27;

    /* The currently selected method. */
    int focus = 0;

    /* The current working row. */
    int yy;

    /* Input buffer. */
    int keystroke;

    time_t t;

    /* Window dimensions */
    int wlines;
    int wcols;

    extern config_t nwipe_cfg;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_config );
    wrefresh( footer_window );

    do
    {
        do
        {
            /* Clear the main window. */
            werase( main_window );

            nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_config );

            /* Determine size of window */
            getmaxyx( main_window, wlines, wcols );

            /* Initialize the working row. */
            yy = 4;

            /* Print the options. */
            mvwprintw( main_window, yy++, tab1, "  %s", "Year" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Month" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Day" );
            yy++;
            mvwprintw( main_window, yy++, tab1, "  %s", "Hours" );
            mvwprintw( main_window, yy++, tab1, "  %s", "Minutes" );
            yy++;
            mvwprintw( main_window,
                       yy++,
                       tab1,
                       "  %s",
                       "If a Network Time Protocol (NTP) daemon is running date/time may not change" );
            mvwprintw( main_window,
                       yy++,
                       tab1,
                       "  %s",
                       "or may revert back to NTP provided time. Setting time here is for use when" );
            mvwprintw( main_window,
                       yy++,
                       tab1,
                       "  %s",
                       "the host system is not running NTP or not connected to the internet." );

            /* Print the cursor. */
            mvwaddch( main_window, 4 + focus, tab1, ACS_RARROW );

            /*******************************
             * Retrieve system date and time
             */
            time( &t );
            mvwprintw( main_window, 2, tab1, "%s", ctime( &t ) );

            /* ************
             * Add a border
             */
            box( main_window, 0, 0 );

            /*************
             * Add a title
             */
            nwipe_gui_title( main_window, " Set date/time " );

            /********************
             * Refresh the window
             */
            wrefresh( main_window );

            /* Wait 250ms for input from getch, if nothing getch will then continue,
             * This is necessary so that the while loop can be exited by the
             * terminate_signal e.g.. the user pressing control-c to exit.
             * Do not change this value, a higher value means the keys become
             * sluggish, any slower and more time is spent unnecessarily looping
             * which wastes CPU cycles.
             */
            timeout( 250 ); /* block getch() for 250ms */
            keystroke = getch(); /* Get a keystroke. */
            timeout( -1 ); /* Switch back to blocking mode */

            switch( keystroke )
            {
                case KEY_DOWN:
                case 'j':
                case 'J':

                    if( focus < count - 1 )
                    {
                        if( focus == 2 )
                        {
                            focus += 2; /* mind the gaps */
                        }
                        else
                        {
                            focus += 1;
                        }
                    }
                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    if( focus > 0 )
                    {
                        if( focus == 4 )
                        {
                            focus -= 2; /* mind the gaps */
                        }
                        else
                        {
                            focus -= 1;
                        }
                    }
                    break;

                case KEY_BACKSPACE:
                case KEY_BREAK:
                case 27: /* ESC */

                    return;

            } /* switch */

        } while( keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10 && terminate_signal != 1 );

        if( keystroke == KEY_ENTER || keystroke == 10 || keystroke == ' ' )
        {
            switch( focus )
            {
                case 0:
                    /* Set year */
                    nwipe_gui_set_system_year();
                    keystroke = 0;
                    break;

                case 1:
                    /* Set month */
                    nwipe_gui_set_system_month();
                    keystroke = 0;
                    break;

                case 2:
                    /* Set day */
                    nwipe_gui_set_system_day();
                    keystroke = 0;
                    break;

                case 4:
                    /* Set hours */
                    nwipe_gui_set_system_hour();
                    keystroke = 0;
                    break;

                case 5:
                    /* Set minutes */
                    nwipe_gui_set_system_minute();
                    keystroke = 0;
                    break;
            }
        }

    } while( keystroke != KEY_ENTER && keystroke != ' ' && keystroke != 10 && terminate_signal != 1 );

} /* end of nwipe_gui_set_date_time( void ) */

void nwipe_gui_set_system_year( void )
{
    /**
     * Allows the user to edit the host systems year
     *
     * @modifies  system year
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy = 2;

    /* Input buffer. */
    int keystroke;

    /* Various output from the date command is processed in this buffer */
    char date_buffer[256];
    date_buffer[0] = 0;

    char year[5] = "";
    char month[3] = "";
    char day[3] = "";
    char hours[3] = "";
    char minutes[3] = "";
    char seconds[3] = "";

    /* buffer index */
    int idx = 0;

    int status = 0;

    FILE* fp;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    fp = popen( "date +%Y", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "popen:Failed to retrieve date +%Y %s", date_buffer );
        mvwprintw( main_window, yy + 4, tab1, "popen:date command failed retrieving year" );
    }

    if( fgets( date_buffer, sizeof( date_buffer ), fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "fgets:failed to retrieve year %s", date_buffer );
        mvwprintw( main_window, yy + 5, tab1, "fgets:failed retrieving year" );
    }

    /* terminate string after fourth character removing any lf */
    date_buffer[4] = 0;

    pclose( fp );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( date_buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Set System Year " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window, yy++, tab1, "Enter the current year, four numeric digits, return key to submit" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", date_buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    date_buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH && idx < 4 )
        {
            date_buffer[idx++] = keystroke;
            date_buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", date_buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* Write year back to system */
    status = read_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:read_system_datetime failed, see previous messages for detail" );
    }

    strncpy( year, date_buffer, 4 );

    status = write_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:write_system_datetime failed, see previous messages for detail" );
    }

} /* End of nwipe_gui_set_system_year() */

void nwipe_gui_set_system_month( void )
{
    /**
     * Allows the user to edit the host systems year
     *
     * @modifies  system month
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy = 2;

    /* Input buffer. */
    int keystroke;

    /* Various output from the date command is processed in this buffer */
    char date_buffer[256];
    date_buffer[0] = 0;

    char year[5] = "";
    char month[3] = "";
    char day[3] = "";
    char hours[3] = "";
    char minutes[3] = "";
    char seconds[3] = "";

    /* buffer index */
    int idx = 0;

    int status = 0;

    FILE* fp;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    fp = popen( "date +%m", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "popen:Failed to retrieve date +%m %s", date_buffer );
        mvwprintw( main_window, yy + 4, tab1, "popen:date command failed retrieving month" );
    }

    if( fgets( date_buffer, sizeof( date_buffer ), fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "fgets:failed to retrieve month %s", date_buffer );
        mvwprintw( main_window, yy + 5, tab1, "fgets:failed retrieving month" );
    }

    /* terminate string after fourth character removing any lf */
    date_buffer[2] = 0;

    pclose( fp );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( date_buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Set System Month " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw(
            main_window, yy++, tab1, "Enter the current month, two numeric digits, i.e 01, return key to submit" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", date_buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    date_buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH && idx < 4 )
        {
            date_buffer[idx++] = keystroke;
            date_buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", date_buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* Write year back to system */
    status = read_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:read_system_datetime failed, see previous messages for detail" );
    }

    strncpy( month, date_buffer, 2 );

    status = write_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:write_system_datetime failed, see previous messages for detail" );
    }

} /* End of nwipe_gui_set_system_month() */

void nwipe_gui_set_system_day( void )
{
    /**
     * Allows the user to edit the host systems year
     *
     * @modifies  system day of the month
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy = 2;

    /* Input buffer. */
    int keystroke;

    /* Various output from the date command is processed in this buffer */
    char date_buffer[256];
    date_buffer[0] = 0;

    char year[5] = "";
    char month[3] = "";
    char day[3] = "";
    char hours[3] = "";
    char minutes[3] = "";
    char seconds[3] = "";

    /* buffer index */
    int idx = 0;

    int status = 0;

    FILE* fp;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    fp = popen( "date +%d", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "popen:Failed to retrieve date +%d %s", date_buffer );
        mvwprintw( main_window, yy + 4, tab1, "popen:date command failed retrieving day of month" );
    }

    if( fgets( date_buffer, sizeof( date_buffer ), fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "fgets:failed to retrieve day of month %s", date_buffer );
        mvwprintw( main_window, yy + 5, tab1, "fgets:failed retrieving day of month" );
    }

    /* terminate string after fourth character removing any lf */
    date_buffer[2] = 0;

    pclose( fp );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( date_buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Set System Day of Month " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw( main_window,
                   yy++,
                   tab1,
                   "Enter the current day of month, two numeric digits, i.e 01, return key to submit" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", date_buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    date_buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH && idx < 4 )
        {
            date_buffer[idx++] = keystroke;
            date_buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", date_buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* Write year back to system */
    status = read_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:read_system_datetime failed, see previous messages for detail" );
    }

    strncpy( day, date_buffer, 2 );

    status = write_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:write_system_datetime failed, see previous messages for detail" );
    }

} /* End of nwipe_gui_set_system_day() */

void nwipe_gui_set_system_hour( void )
{
    /**
     * Allows the user to edit the host systems year
     *
     * @modifies  system hour
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy = 2;

    /* Input buffer. */
    int keystroke;

    /* Various output from the date command is processed in this buffer */
    char date_buffer[256];
    date_buffer[0] = 0;

    char year[5] = "";
    char month[3] = "";
    char day[3] = "";
    char hours[3] = "";
    char minutes[3] = "";
    char seconds[3] = "";

    /* buffer index */
    int idx = 0;

    int status = 0;

    FILE* fp;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    fp = popen( "date +%H", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "popen:Failed to retrieve date +%H %s", date_buffer );
        mvwprintw( main_window, yy + 4, tab1, "popen:date command failed retrieving hour" );
    }

    if( fgets( date_buffer, sizeof( date_buffer ), fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "fgets:failed to retrieve the hour %s", date_buffer );
        mvwprintw( main_window, yy + 5, tab1, "fgets:failed retrieving the hour" );
    }

    /* terminate string after fourth character removing any lf */
    date_buffer[2] = 0;

    pclose( fp );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( date_buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Set System Hour " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw(
            main_window, yy++, tab1, "Enter the current hour, two numeric digits, i.e 01, return key to submit" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", date_buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    date_buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH && idx < 4 )
        {
            date_buffer[idx++] = keystroke;
            date_buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", date_buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* Write year back to system */
    status = read_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:read_system_datetime failed, see previous messages for detail" );
    }

    strncpy( hours, date_buffer, 2 );

    status = write_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:write_system_datetime failed, see previous messages for detail" );
    }

} /* End of nwipe_gui_set_system_hour() */

void nwipe_gui_set_system_minute( void )
{
    /**
     * Allows the user to edit the host systems year
     *
     * @modifies  system minute
     * @modifies  main_window
     *
     */

    /* The first tabstop. */
    const int tab1 = 2;

    /* The current working row. */
    int yy = 2;

    /* Input buffer. */
    int keystroke;

    /* Various output from the date command is processed in this buffer */
    char date_buffer[256];
    date_buffer[0] = 0;

    char year[5] = "";
    char month[3] = "";
    char day[3] = "";
    char hours[3] = "";
    char minutes[3] = "";
    char seconds[3] = "";

    /* buffer index */
    int idx = 0;

    int status = 0;

    FILE* fp;

    extern int terminate_signal;

    /* Update the footer window. */
    werase( footer_window );
    nwipe_gui_title( footer_window, selection_footer_text_entry );
    wrefresh( footer_window );

    fp = popen( "date +%M", "r" );
    if( fp == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "popen:Failed to retrieve date +%M %s", date_buffer );
        mvwprintw( main_window, yy + 4, tab1, "popen:date command failed retrieving minute" );
    }

    if( fgets( date_buffer, sizeof( date_buffer ), fp ) == NULL )
    {
        nwipe_log( NWIPE_LOG_INFO, "fgets:failed to retrieve the minute %s", date_buffer );
        mvwprintw( main_window, yy + 5, tab1, "fgets:failed retrieving the minute" );
    }

    /* terminate string after fourth character removing any lf */
    date_buffer[2] = 0;

    pclose( fp );

    /* Set the buffer index to point to the end of the string, i.e the NULL */
    idx = strlen( date_buffer );

    do
    {
        /* Erase the main window. */
        werase( main_window );

        nwipe_gui_create_all_windows_on_terminal_resize( 0, selection_footer_text_entry );

        /* Add a border. */
        box( main_window, 0, 0 );

        /* Add a title. */
        nwipe_gui_title( main_window, " Set System Minute " );

        /* Initialize the working row. */
        yy = 4;

        mvwprintw(
            main_window, yy++, tab1, "Enter the current minute, two numeric digits, i.e 01, return key to submit" );

        /* Print this line last so that the cursor is in the right place. */
        mvwprintw( main_window, 2, tab1, ">%s", date_buffer );

        /* Reveal the cursor. */
        curs_set( 1 );

        /* Refresh the window. */
        wrefresh( main_window );

        /* Wait 250ms for input from getch, if nothing getch will then continue,
         * This is necessary so that the while loop can be exited by the
         * terminate_signal e.g.. the user pressing control-c to exit.
         * Do not change this value, a higher value means the keys become
         * sluggish, any slower and more time is spent unnecessarily looping
         * which wastes CPU cycles.
         */
        timeout( 250 );  // block getch() for 250ms.
        keystroke = getch();  // Get a keystroke.
        timeout( -1 );  // Switch back to blocking mode.

        switch( keystroke )
        {
            /* Escape key. */
            case 27:
                return;

            case KEY_BACKSPACE:
            case KEY_LEFT:
            case 127:

                if( idx > 0 )
                {
                    date_buffer[--idx] = 0;
                }

                break;

        } /* switch keystroke */

        if( ( keystroke >= ' ' && keystroke <= '~' ) && keystroke != '\"' && idx < FIELD_LENGTH && idx < 4 )
        {
            date_buffer[idx++] = keystroke;
            date_buffer[idx] = 0;
            mvwprintw( main_window, 2, tab1, ">%s", date_buffer );
        }

        /* Hide the cursor. */
        curs_set( 0 );

    } while( keystroke != 10 && terminate_signal != 1 );

    /* Write year back to system */
    status = read_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:read_system_datetime failed, see previous messages for detail" );
    }

    strncpy( minutes, date_buffer, 2 );

    status = write_system_datetime( year, month, day, hours, minutes, seconds );
    if( status != 0 )
    {
        nwipe_log( NWIPE_LOG_ERROR, "func:write_system_datetime failed, see previous messages for detail" );
    }

} /* End of nwipe_gui_set_system_minute() */

void nwipe_gui_load( void )
{
    /**
     * Prints the system load average to the statistics window.
     *
     * @modifies stat_window      Prints the system load average to the statistics window.
     *
     */

    /* A file handle for the stat file. */
    FILE* nwipe_fp;

    /* The one, five, and fifteen minute load averages. */
    float load_01;
    float load_05;
    float load_15;

    /* Open the loadavg file. */
    nwipe_fp = fopen( NWIPE_KNOB_LOADAVG, "r" );

    /* Print the label. */
    mvwprintw( stats_window, NWIPE_GUI_STATS_LOAD_Y, NWIPE_GUI_STATS_LOAD_X, "Load Averages:" );

    if( nwipe_fp )
    {
        /* The load averages are the first three numbers in the file. */
        if( 3 == fscanf( nwipe_fp, "%f %f %f", &load_01, &load_05, &load_15 ) )
        {
            /* Print the load average. */
            mvwprintw( stats_window,
                       NWIPE_GUI_STATS_LOAD_Y,
                       NWIPE_GUI_STATS_TAB,
                       "%04.2f %04.2f %04.2f",
                       load_01,
                       load_05,
                       load_15 );
        }
        else
        {
            /* Print an error. */
            mvwprintw( stats_window, NWIPE_GUI_STATS_LOAD_Y, NWIPE_GUI_STATS_TAB, "(fscanf error %i)", errno );
        }

        /* Close the loadavg file. */
        fclose( nwipe_fp );
    }
    else
    {
        mvwprintw( stats_window, NWIPE_GUI_STATS_LOAD_Y, NWIPE_GUI_STATS_TAB, "(fopen error %i)", errno );
    }

} /* nwipe_gui_load */

void* nwipe_gui_status( void* ptr )
{
    /**
     * Shows runtime statistics and overall progress.
     *
     * @parameter count           The number of contexts in the array.
     * @parameter c               An array of device contexts.
     *
     * @modifies  main_window     Prints information into the main window.
     * @modifies  c[].throughput  Updates the i/o throughput value.
     *
     */

    extern int terminate_signal;

    nwipe_thread_data_ptr_t* nwipe_thread_data_ptr;
    nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t*) ptr;

    nwipe_context_t** c;
    nwipe_misc_thread_data_t* nwipe_misc_thread_data;
    int count;

    c = nwipe_thread_data_ptr->c;
    nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;
    count = nwipe_misc_thread_data->nwipe_selected;

    char nomenclature_result_str[NOMENCLATURE_RESULT_STR_SIZE]; /* temporary usage */

    /* Spinner character */
    char spinner_string[2];

    /* Create the finish message, this changes based on whether PDF creation is enabled
     * and whether a logfile has been specified
     */
    char finish_message[NWIPE_GUI_FOOTER_W + 132];
    if( nwipe_options.logfile[0] == 0 && nwipe_options.PDF_enable != 0 )
    {
        snprintf( finish_message,
                  sizeof( finish_message ),
                  "Wipe finished - press enter to create pdfs & exit. Logged to STDOUT" );
    }
    else
    {
        if( nwipe_options.logfile[0] != 0 && nwipe_options.PDF_enable != 0 )
        {
            snprintf( finish_message,
                      sizeof( finish_message ),
                      "Wipe finished - press enter to create pdfs & exit. Logged to %s",
                      nwipe_options.logfile );
        }
        else
        {
            if( nwipe_options.logfile[0] != 0 && nwipe_options.PDF_enable == 0 )
            {
                snprintf( finish_message,
                          sizeof( finish_message ),
                          "Wipe finished - press enter to exit (pdfs disabled in config). Logged to %s",
                          nwipe_options.logfile );
            }
            else
            {
                if( nwipe_options.logfile[0] == 0 && nwipe_options.PDF_enable == 0 )
                {
                    snprintf( finish_message,
                              sizeof( finish_message ),
                              "Wipe finished - press enter to exit (pdfs disabled in config). Logged to STDOUT" );
                }
                else
                {
                    /* This is a catch all something unexpected happens with the above logic */
                    snprintf( finish_message,
                              sizeof( finish_message ),
                              "Wipe finished - press enter to exit. Logged to STDOUT" );
                }
            }
        }
    }

    /* We count time from when this function is first called. */
    static time_t nwipe_time_start = 0;

    /* Whether the screen has been blanked by the user. */
    static int nwipe_gui_blank = 0;

    /* The current time. */
    time_t nwipe_time_now;

    /* The time when all wipes ended */
    time_t nwipe_time_stopped;

    /* The index of the element that is visible in the first slot. */
    static int offset;

    /* The number of elements that we can show in the window. */
    int slots;

    /* Window dimensions. */
    int wlines;
    int wcols;

    /* Generic loop variable. */
    int i;

    /* The current working line in the main window. */
    int yy;

    /* User input buffer. */
    int keystroke;

    /* controls main while loop */
    int loop_control;

    /* The combined througput of all processes. */
    nwipe_misc_thread_data->throughput = 0;

    /* The estimated runtime of the slowest device. */
    nwipe_misc_thread_data->maxeta = 0;

    /* The combined number of errors of all processes. */
    nwipe_misc_thread_data->errors = 0;

    /* Time values. */
    int nwipe_hh;
    int nwipe_mm;
    int nwipe_ss;

    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = 100000000L; /* sleep for 0.1 seconds */

    /* Throughput variables */
    u64 nwipe_throughput;

    /* The number of active wipe processes. */
    /* Set to 1 initially to start loop.    */
    int nwipe_active = 1;

    /* Used in the gui status loop to trap a failure of the halfdelay(), getch() mechanism to block for the designated
     * period */
    int expected_iterations;

    /* Used in the selection loop to trap a failure of the timeout(), getch() mechanism to block for the designated
     * period, initialise the counter */
    int iteration_counter = 0;

    time_t previous_iteration_timestamp = time( NULL );

    /* Calculate Maximum allowed iterations per second (typically 20), which is double the expected iterations
     * (typically 10) */
    expected_iterations = ( 1000 / GETCH_GUI_STATS_UPDATE_MS ) * 2;

    if( nwipe_time_start == 0 )
    {
        /* This is the first time that we have been called. */
        nwipe_time_start = time( NULL ) - 1;
    }

    nwipe_gui_title( footer_window, end_wipe_footer );

    loop_control = 1;

    while( loop_control )
    {
        /* IMPORTANT ! Halfdelay(1) causes getch() to pause for 0.1 secs. This is important for two reasons.
         * 1. Pauses the getch for 0.1 secs so that the screen is only updated max 10 times/sec. Without
         *    this delay the loop would run hundreds of times per sec maxing out the core.
         * 2. By keeping the delay below 0.2 seconds, i.e 0.1, it makes the keypress and resizing
         *    nice and responsive.
         */
        halfdelay( GETCH_GUI_STATS_UPDATE_MS );  // Important, don't change this unless you know what you are doing !
                                                 // Related to getch().

        keystroke = getch();  // Get user input.

        iteration_counter++;

        /* Much like the same check we perform in the nwipe_gui_select() function, here we check that we are not looping
         * any faster than as defined by the halfdelay() function above, typically this loop runs at 10 times a second.
         * This check makes sure that if the loop runs faster than double this value i.e 20 times a second then the
         * program exits. This check is therefore determining whether the getch() function is returning immediately
         * rather than blocking for the defined period of 100ms. Why is this necessary? Some terminals (konsole &
         * deriviatives) that are exited while nwipe is still running fail to terminate nwipe this causes the
         * halfdelay()/getch() functions to immediately fail causing the loop frequency to drastically increase. We
         * detect that speed increase here and therefore close down nwipe. This doesn't affect the use of the tmux
         * terminal by which you can detach and reattach to running nwipe processes. tmux still works correctly.
         */
        if( previous_iteration_timestamp == time( NULL ) )
        {
            if( iteration_counter > expected_iterations )
            {
                nwipe_log( NWIPE_LOG_ERROR,
                           "GUI.c,nwipe_gui_status(), loop runaway, did you close the terminal without exiting "
                           "nwipe? Initiating shutdown now." );
                /* Issue signal to nwipe to shutdown immediately but gracefully */
                terminate_signal = 1;
            }
        }
        else
        {
            /* new second, so reset counter */
            iteration_counter = 0;
            previous_iteration_timestamp = time( NULL );
        }

        /* Get the current time. */
        if( nwipe_active && terminate_signal != 1 )
        {
            nwipe_time_now = time( NULL );
            nwipe_time_stopped = nwipe_time_now;
        }
        else
        {
            nwipe_time_now = nwipe_time_stopped;
        }

        /* Erase the main window. */
        werase( main_window );

        /* Erase the stats window. */
        werase( stats_window );

        /* Erase the footer window */
        werase( footer_window );

        /* Only repaint the windows on terminal resize if the user hasn't blanked the screen */
        if( nwipe_gui_blank == 0 )
        {
            if( nwipe_active != 0 )
            {
                /* if resizing the terminal during a wipe a specific footer is required */
                nwipe_gui_create_all_windows_on_terminal_resize( 0, end_wipe_footer );
            }
            else
            {
                /* and if the wipes have finished a different footer is required */
                nwipe_gui_create_all_windows_on_terminal_resize( 0, finish_message );
            }
        }

        /* Initialize our working offset to the third line. */
        yy = 2;

        /* Get the window dimensions. */
        getmaxyx( main_window, wlines, wcols );

        /* Less four lines for the box and padding. */
        slots = wlines - 4;

        /* Each element prints three lines. */
        slots /= 3;

        if( nwipe_active == 0 || terminate_signal == 1 )
        {
            nwipe_gui_title( footer_window, finish_message );

            // Refresh the footer_window ;
            wnoutrefresh( footer_window );
        }

        if( terminate_signal == 1 )
        {
            loop_control = 0;
        }

        if( keystroke > 0x0a && keystroke < 0x7e && nwipe_gui_blank == 1 )
        {
            tft_saver = 0;
            nwipe_init_pairs();
            nwipe_gui_create_all_windows_on_terminal_resize( 1, end_wipe_footer );

            /* Show screen */
            nwipe_gui_blank = 0;

            /* Set background */
            wbkgdset( stdscr, COLOR_PAIR( 1 ) );
            wclear( stdscr );

            /* Unhide panels */
            show_panel( header_panel );
            show_panel( footer_panel );
            show_panel( stats_panel );
            show_panel( options_panel );
            show_panel( main_panel );

            /* Reprint the footer */
            nwipe_gui_title( footer_window, end_wipe_footer );

            // Refresh the footer_window ;
            wnoutrefresh( footer_window );

            /* Update panels */
            update_panels();
            doupdate();
        }
        else if( keystroke > 0 )
        {

            switch( keystroke )
            {

                case 'b':
                case 'B':

                    if( nwipe_gui_blank == 0 && tft_saver != 1 )
                    {
                        /* grey text on black background */
                        tft_saver = 1;
                        nwipe_init_pairs();
                        nwipe_gui_create_all_windows_on_terminal_resize( 1, end_wipe_footer );
                    }
                    else
                    {
                        if( nwipe_gui_blank == 0 && tft_saver == 1 )
                        {
                            /* Blank screen. */
                            tft_saver = 0;
                            nwipe_gui_blank = 1;
                            hide_panel( header_panel );
                            hide_panel( footer_panel );
                            hide_panel( stats_panel );
                            hide_panel( options_panel );
                            hide_panel( main_panel );
                        }

                        /* Set the background style. */
                        wbkgdset( stdscr, COLOR_PAIR( 7 ) );
                        wclear( stdscr );
                    }

                    break;

                case KEY_DOWN:
                case 'j':
                case 'J':

                    /* Scroll down. */
                    offset += 1;

                    if( count < slots )
                    {
                        offset = 0;
                    }

                    else if( offset + slots > count )
                    {
                        offset = count - slots;
                    }

                    break;

                case KEY_UP:
                case 'k':
                case 'K':

                    /* Scroll up. */
                    offset -= 1;

                    if( offset < 0 )
                    {
                        offset = 0;
                    }

                    break;

                case ' ':
                case 0x0a:

                    /* Check whether we have finished all wipes, if yes exit while loop if user pressed spacebar or
                     * return. */
                    if( !nwipe_active || terminate_signal == 1 )
                    {
                        loop_control = 0;
                    }

                    break;

                default:

                    /* Do nothing. */
                    break;
            }

        } /* keystroke */

        /* If wipe has completed and user has specified auto poweroff or nowait then we can skip waiting for the user to
         * press return */
        if( !nwipe_active )
        {
            if( nwipe_options.autopoweroff || nwipe_options.nowait )
            {
                loop_control = 0;
            }
        }

        /* Update data in statistics & main windows only if we're in 'gui' mode and only if a wipe has started */
        if( global_wipe_status == 1 )
        {
            /* Always run compute_stats() as we need to whether any threads are still active */
            if( terminate_signal != 1 )
            {
                nwipe_active = compute_stats( ptr );  // Returns number of active wipe threads
            }

            /* Only print the stats if the user hasn't blanked the screen */
            if( nwipe_gui_blank == 0 )
            {

                /* Print information for the user. */
                for( i = offset; i < offset + slots && i < count; i++ )
                {
                    /* Print the device details. */
                    mvwprintw( main_window,
                               yy++,
                               2,
                               "%s %s [%s] ",
                               c[i]->device_name,
                               c[i]->device_type_str,
                               c[i]->device_size_text );
                    wprintw_temperature( c[i] );
                    wprintw( main_window, " %s/%s", c[i]->device_model, c[i]->device_serial_no );

                    /* Check whether the child process is still running the wipe. */
                    if( c[i]->wipe_status == 1 )
                    {
                        /* Print percentage and pass information. */
                        mvwprintw( main_window,
                                   yy++,
                                   4,
                                   "[%5.2f%%, round %i of %i, pass %i of %i] ",
                                   c[i]->round_percent,
                                   c[i]->round_working,
                                   c[i]->round_count,
                                   c[i]->pass_working,
                                   c[i]->pass_count );

                    } /* child running */
                    else
                    {
                        if( c[i]->result == 0 )
                        {
                            mvwprintw( main_window, yy++, 4, "[%05.2f%% complete, SUCCESS! ", c[i]->round_percent );
                        }
                        else if( c[i]->signal )
                        {
                            wattron( main_window, COLOR_PAIR( 9 ) );
                            mvwprintw( main_window, yy++, 4, "(>>> FAILURE! <<<, signal %i) ", c[i]->signal );
                            wattroff( main_window, COLOR_PAIR( 9 ) );
                        }
                        else
                        {
                            wattron( main_window, COLOR_PAIR( 9 ) );
                            mvwprintw( main_window, yy++, 4, "(>>> IOERROR! <<<, code %i) ", c[i]->result );
                            wattroff( main_window, COLOR_PAIR( 9 ) );
                        }

                    } /* child returned */

                    if( c[i]->verify_errors )
                    {
                        wprintw( main_window, "[verr:%llu] ", c[i]->verify_errors );
                    }
                    if( c[i]->pass_errors )
                    {
                        wprintw( main_window, "[perr:%llu] ", c[i]->pass_errors );
                    }
                    if( c[i]->wipe_status == 1 )
                    {
                        switch( c[i]->pass_type )
                        {
                            /* Each text field in square brackets should be the same number of characters
                             * to retain output in columns */
                            case NWIPE_PASS_FINAL_BLANK:
                                if( !c[i]->sync_status )
                                {
                                    wprintw( main_window, "[ blanking] " );
                                }
                                break;

                            case NWIPE_PASS_FINAL_OPS2:
                                if( !c[i]->sync_status )
                                {
                                    wprintw( main_window, "[OPS2final] " );
                                }
                                break;

                            case NWIPE_PASS_WRITE:
                                if( !c[i]->sync_status )
                                {
                                    wprintw( main_window, "[ writing ] " );
                                }
                                break;

                            case NWIPE_PASS_VERIFY:
                                if( !c[i]->sync_status )
                                {
                                    wprintw( main_window, "[verifying] " );
                                }
                                break;

                            case NWIPE_PASS_NONE:
                                break;
                        }

                        if( c[i]->sync_status )
                        {
                            wprintw( main_window, "[ syncing ] " );
                        }
                    }

                    /* Determine throughput nomenclature for this drive and output drives throughput to GUI */
                    Determine_C_B_nomenclature(
                        c[i]->throughput, nomenclature_result_str, NOMENCLATURE_RESULT_STR_SIZE );

                    wprintw( main_window, "[%s/s] ", nomenclature_result_str );

                    /* Insert whitespace. */
                    yy += 1;

                    /* Increment the next spinner character for this context if the thread is active */
                    if( c[i]->wipe_status == 1 )
                    {
                        spinner( c, i );
                        spinner_string[0] = c[i]->spinner_character[0];
                    }
                    else
                    {
                        /* If the wipe thread is no longer active, replace the spinner with a space */
                        spinner_string[0] = ' ';
                    }
                    spinner_string[1] = 0;
                    wprintw( main_window, " %s ", spinner_string );
                }

                if( offset > 0 )
                {
                    mvwprintw( main_window, 1, wcols - 8, " More " );
                    waddch( main_window, ACS_UARROW );
                }

                if( count - offset > slots )
                {
                    mvwprintw( main_window, wlines - 2, wcols - 8, " More " );
                    waddch( main_window, ACS_DARROW );
                }

                /* Box the main window. */
                box( main_window, 0, 0 );

                /* Refresh the main window. */
                wnoutrefresh( main_window );

                /* Update the load average field, but only if we are still wiping */
                if( nwipe_active && terminate_signal != 1 )
                {
                    nwipe_gui_load();
                }

                nwipe_throughput = nwipe_misc_thread_data->throughput;

                /* Determine the nomenclature for the combined throughput */
                Determine_C_B_nomenclature( nwipe_throughput, nomenclature_result_str, NOMENCLATURE_RESULT_STR_SIZE );

                /* Print the combined throughput. */
                mvwprintw( stats_window, NWIPE_GUI_STATS_THROUGHPUT_Y, NWIPE_GUI_STATS_THROUGHPUT_X, "Throughput:" );

                mvwprintw(
                    stats_window, NWIPE_GUI_STATS_THROUGHPUT_Y, NWIPE_GUI_STATS_TAB, "%s/s", nomenclature_result_str );

                /* Change the current time into a delta. */
                nwipe_time_now -= nwipe_time_start;

                /* Put the delta into HH:mm:ss form. */
                nwipe_hh = nwipe_time_now / 3600;
                nwipe_time_now %= 3600;
                nwipe_mm = nwipe_time_now / 60;
                nwipe_time_now %= 60;
                nwipe_ss = nwipe_time_now;

                /* Print the runtime. */
                mvwprintw( stats_window, NWIPE_GUI_STATS_RUNTIME_Y, 1, "Runtime:" );
                mvwprintw( stats_window,
                           NWIPE_GUI_STATS_RUNTIME_Y,
                           NWIPE_GUI_STATS_TAB,
                           "%02i:%02i:%02i",
                           nwipe_hh,
                           nwipe_mm,
                           nwipe_ss );

                mvwprintw( stats_window, NWIPE_GUI_STATS_ETA_Y, 1, "Remaining:" );

                time_t nwipe_maxeta = nwipe_misc_thread_data->maxeta;
                if( nwipe_maxeta > 0 )
                {
                    /* Do it again for the estimated runtime remaining. */
                    nwipe_hh = nwipe_maxeta / 3600;
                    nwipe_maxeta %= 3600;
                    nwipe_mm = nwipe_maxeta / 60;
                    nwipe_maxeta %= 60;
                    nwipe_ss = nwipe_maxeta;

                    /* Print the estimated runtime remaining. */
                    mvwprintw( stats_window,
                               NWIPE_GUI_STATS_ETA_Y,
                               NWIPE_GUI_STATS_TAB,
                               "%02i:%02i:%02i",
                               nwipe_hh,
                               nwipe_mm,
                               nwipe_ss );
                }

                /* Print the error count. */
                mvwprintw( stats_window, NWIPE_GUI_STATS_ERRORS_Y, NWIPE_GUI_STATS_ERRORS_X, "Errors:" );
                mvwprintw( stats_window,
                           NWIPE_GUI_STATS_ERRORS_Y,
                           NWIPE_GUI_STATS_TAB,
                           "  %llu",
                           nwipe_misc_thread_data->errors );

                /* Add a border. */
                box( stats_window, 0, 0 );

                /* Add a title. */
                mvwprintw( stats_window, 0, ( NWIPE_GUI_STATS_W - strlen( stats_title ) ) / 2, "%s", stats_title );

                /* Refresh internal representation of stats window */
                wnoutrefresh( stats_window );

                /* Output all windows to screen */
                doupdate();

            }  // end blank screen if

        }  // end wipes have started if

    } /* End of while loop */

    nwipe_gui_title( footer_window, finish_message );
    terminate_signal = 1;

    return NULL;
} /* nwipe_gui_status */

int compute_stats( void* ptr )
{
    nwipe_thread_data_ptr_t* nwipe_thread_data_ptr;
    nwipe_thread_data_ptr = (nwipe_thread_data_ptr_t*) ptr;

    nwipe_context_t** c;
    nwipe_misc_thread_data_t* nwipe_misc_thread_data;

    c = nwipe_thread_data_ptr->c;
    nwipe_misc_thread_data = nwipe_thread_data_ptr->nwipe_misc_thread_data;
    int count = nwipe_misc_thread_data->nwipe_selected;

    int nwipe_active = 0;
    int i;

    time_t nwipe_time_now = time( NULL );

    nwipe_misc_thread_data->throughput = 0;
    nwipe_misc_thread_data->maxeta = 0;
    nwipe_misc_thread_data->errors = 0;

    /* Enumerate all contexts to compute statistics. */
    for( i = 0; i < count; i++ )
    {
        /* Check whether the child process is still running the wipe. */
        if( c[i]->wipe_status == 1 )
        {
            /* Increment the child counter. */
            nwipe_active += 1;

            /* Even if the wipe has finished ALWAYS run the stats one last time so the final SUCCESS percentage value is
             * correct. Maintain a rolling average of throughput. */
            nwipe_update_speedring( &c[i]->speedring, c[i]->round_done, nwipe_time_now );

            if( c[i]->speedring.timestotal > 0 && c[i]->wipe_status == 1 )
            {
                /* Update the current average throughput in bytes-per-second. */
                c[i]->throughput = c[i]->speedring.bytestotal / c[i]->speedring.timestotal;

                /* Only update the estimated remaining runtime if the
                 * throughput for a given drive is greater than 100,000 bytes per second
                 * This prevents enormous ETA's being calculated on an unresponsive
                 * drive */
                if( c[i]->throughput > 100000 )
                {
                    c[i]->eta = ( c[i]->round_size - c[i]->round_done ) / c[i]->throughput;

                    if( c[i]->eta > nwipe_misc_thread_data->maxeta )
                    {
                        nwipe_misc_thread_data->maxeta = c[i]->eta;
                    }
                }
            }

            /* Calculate the average throughput */
            c[i]->throughput = (double) c[i]->round_done / (double) difftime( nwipe_time_now, c[i]->start_time );
        }

        /* Update the percentage value. */
        c[i]->round_percent = (double) c[i]->round_done / (double) c[i]->round_size * 100;

        if( c[i]->wipe_status == 1 )
        {
            /* Accumulate combined throughput. */
            nwipe_misc_thread_data->throughput += c[i]->throughput;
        }

        /* Accumulate the error count. */
        nwipe_misc_thread_data->errors += c[i]->pass_errors;
        nwipe_misc_thread_data->errors += c[i]->verify_errors;
        nwipe_misc_thread_data->errors += c[i]->fsyncdata_errors;

        /* Read the drive temperature values */
        //        if( nwipe_time_now > ( c[i]->temp1_time + 60 ) )
        //        {
        //            nwipe_update_temperature( c[i] );
        //        }

    } /* for statistics */

    return nwipe_active;
}

void nwipe_update_speedring( nwipe_speedring_t* speedring, u64 speedring_bytes, time_t speedring_now )
{

    if( speedring->timeslast == 0 )
    {
        /* Ignore the first sample and initialize. */
        speedring->timeslast = speedring_now;
        return;
    }

    if( speedring_now - speedring->timeslast < NWIPE_KNOB_SPEEDRING_GRANULARITY )
    {
        /* Avoid jitter caused by frequent updates. */
        return;
    }

    /* Subtract the oldest speed sample from the accumulator. */
    speedring->bytestotal -= speedring->bytes[speedring->position];
    speedring->timestotal -= speedring->times[speedring->position];

    /* Put the latest bytes-per-second sample into the ring buffer. */
    speedring->bytes[speedring->position] = speedring_bytes - speedring->byteslast;
    speedring->times[speedring->position] = speedring_now - speedring->timeslast;

    /* Add the newest speed sample to the accumulator. */
    speedring->bytestotal += speedring->bytes[speedring->position];
    speedring->timestotal += speedring->times[speedring->position];

    /* Remember the last sample. */
    speedring->byteslast = speedring_bytes;
    speedring->timeslast = speedring_now;

    if( ++speedring->position >= NWIPE_KNOB_SPEEDRING_SIZE )
    {
        speedring->position = 0;
    }
}

int spinner( nwipe_context_t** ptr, int device_idx )
{
    nwipe_context_t** c;

    c = ptr;

    /* The spinner characters |/-\|/-\ */
    char sc[9] = "|/-\\|/-\\/";

    /* Check sanity of index */
    if( c[device_idx]->spinner_idx < 0 || c[device_idx]->spinner_idx > 7 )
    {
        return 1;
    }

    c[device_idx]->spinner_character[0] = sc[c[device_idx]->spinner_idx];

    c[device_idx]->spinner_idx++;

    if( c[device_idx]->spinner_idx > 7 )
    {
        c[device_idx]->spinner_idx = 0;
    }

    return 0;
}

void temp1_flash( nwipe_context_t* c )
{
    if( c->temp1_flash_rate_counter < c->temp1_flash_rate )
    {
        c->temp1_flash_rate_counter++;
    }
    else
    {
        c->temp1_flash_rate_counter = 0;
        if( c->temp1_flash_rate_status == 0 )
        {
            c->temp1_flash_rate_status = 1;
        }
        else
        {
            c->temp1_flash_rate_status = 0;
        }
    }
}

void wprintw_temperature( nwipe_context_t* c )
{
    /* See header for description of function
     */

    int temp_highest_limit;
    int temp_high_limit;
    int temp_low_limit;
    int temp_lowest_limit;

    int local_temp1_input = c->temp1_input;
    int local_temp1_crit = c->temp1_crit;
    int local_temp1_max = c->temp1_max;
    int local_temp1_min = c->temp1_min;
    int local_temp1_lcrit = c->temp1_lcrit;

    /* Initialise */
    temp_highest_limit = NO_TEMPERATURE_DATA;
    temp_high_limit = NO_TEMPERATURE_DATA;
    temp_low_limit = NO_TEMPERATURE_DATA;
    temp_lowest_limit = NO_TEMPERATURE_DATA;

#if 0
    /* NOTE TEST Function, TEST Function #if 0 when not testing
     * Should increment temperature back and forth between +10 to -10
     * while changing the color based on the settings such as
     * c->temp1_crit and others below
     */
    if( c->test_use1 > 12 || c->test_use1 < -12 )
    {
        c->test_use1 = 0; // the value
        c->test_use2 = 0; // direction 0 = -- or 1 = ++
    }
    if( c->test_use1 >= 10 )
    {
        c->test_use2 = 0;
    }
    else
    {
        if( c->test_use1 <= -10 )
        {
            c->test_use2 = 1;
        }
    }
    if( c->test_use2 == 0 )
    {
        c->test_use1--;
    }
    else
    {
        c->test_use1++;
    }

    /* Five test cases to test temperature color logic
     * test only with temperatures in the range -8 to +8
     * test with each set of five for expected result if
     * changes are made.
     *
     * Uncomment only one group in turn to be tested.
     */
    local_temp1_input = c->test_use1;
    //
    // Expected result - white on blue between +5 & -5, white on red above 5
    // white on black below -5
    local_temp1_crit = NO_TEMPERATURE_DATA;
    local_temp1_max = 5;
    local_temp1_min = -5;
    local_temp1_lcrit = NO_TEMPERATURE_DATA;
    //
    // Expected result - white on blue between +5 & -5, white on red above 5
    // white on black below -5
    //local_temp1_crit = 5;
    //local_temp1_max = NO_TEMPERATURE_DATA;
    //local_temp1_min = NO_TEMPERATURE_DATA;
    //local_temp1_lcrit = -5;
    //
    // Expected result - white on blue 5 to -5, 5-7 red on blue,
    // 8+ white on red, -5 to-7 black on blue, less than -7 white on black
    //local_temp1_crit = 8;
    //local_temp1_max = 5;
    //local_temp1_min = -5;
    //local_temp1_lcrit = -8;
    //
    // Expected result - white on blue 5 to -5, 5-7 red on blue,
    // 8+ white on red, -5 to-7 black on blue, less than -7 white on black
    //local_temp1_crit = 5;
    //local_temp1_max = 8;
    //local_temp1_min = -8;
    //local_temp1_lcrit = -5;
    //
    // Expected result - always white text on blue background
    //local_temp1_crit = NO_TEMPERATURE_DATA;
    //local_temp1_max = NO_TEMPERATURE_DATA;
    //local_temp1_min = NO_TEMPERATURE_DATA;
    //local_temp1_lcrit = NO_TEMPERATURE_DATA;
#endif

    /* Depending upon the drive firmware, the meaning of 'high critical' & 'max'
     * and 'low critical' & 'low' can be interchanged. First validate for 'no data'
     * (1000000) and also a 0 in 'high critical' and 'max', then  assign the values
     * to our four variables in the appropriate order.
     */

    /* Validate critical high value & max for 0, if 0 it's invalid, change to 1000000
     */

    /* Assign temp1_crit & temp1_max to local variables as we are going to alter
     * them if 0, as they may be updated elsewhere we don't want them changed back half
     * way through our processing. This is only necessary for the high temperatures and
     * not the low temperatures as low temperatures may well be 0.
     */

    if( local_temp1_crit == 0 )
    {
        local_temp1_crit = NO_TEMPERATURE_DATA;
    }
    if( local_temp1_max == 0 )
    {
        local_temp1_max = NO_TEMPERATURE_DATA;
    }

    /* Check which way around they are */
    if( local_temp1_crit != NO_TEMPERATURE_DATA && local_temp1_max != NO_TEMPERATURE_DATA )
    {
        if( local_temp1_crit > local_temp1_max )
        {
            temp_highest_limit = local_temp1_crit;
            temp_high_limit = local_temp1_max;
        }
        else
        {
            temp_highest_limit = local_temp1_max;
            temp_high_limit = local_temp1_crit;
        }
    }
    else
    {
        /* If only one or the other is present then assign that value to both high critical and max
         */
        if( local_temp1_crit == NO_TEMPERATURE_DATA && local_temp1_max != NO_TEMPERATURE_DATA )
        {
            temp_highest_limit = local_temp1_max;
            temp_high_limit = local_temp1_max;
        }
        else
        {
            /* If high critical is present but max is not, assign high critical to both */
            if( local_temp1_crit != NO_TEMPERATURE_DATA && local_temp1_max == NO_TEMPERATURE_DATA )
            {
                temp_highest_limit = local_temp1_crit;
                temp_high_limit = local_temp1_crit;
            }
            else
            {
                /* neither is present so mark both locals as not present */
                temp_highest_limit = NO_TEMPERATURE_DATA;
                temp_high_limit = NO_TEMPERATURE_DATA;
            }
        }
    }

    /* Now do the same for the low critical limit and low limit. */

    /* Check which way around they are */
    if( local_temp1_lcrit != NO_TEMPERATURE_DATA && local_temp1_min != NO_TEMPERATURE_DATA )
    {
        if( local_temp1_lcrit < local_temp1_min )
        {
            temp_lowest_limit = local_temp1_lcrit;
            temp_low_limit = local_temp1_min;
        }
        else
        {
            temp_lowest_limit = local_temp1_min;
            temp_low_limit = local_temp1_lcrit;
        }
    }
    else
    {
        /* If only one or the other is present then assign that value to both high critical and max
         */
        if( local_temp1_lcrit == NO_TEMPERATURE_DATA && local_temp1_min != NO_TEMPERATURE_DATA )
        {
            temp_lowest_limit = local_temp1_min;
            temp_low_limit = local_temp1_min;
        }
        else
        {
            /* If high critical is present but max is not, assign high critical to both */
            if( local_temp1_lcrit != NO_TEMPERATURE_DATA && local_temp1_min == NO_TEMPERATURE_DATA )
            {
                temp_lowest_limit = local_temp1_lcrit;
                temp_low_limit = local_temp1_lcrit;
            }
            else
            {
                /* neither is present so mark both locals as not present */
                temp_lowest_limit = NO_TEMPERATURE_DATA;
                temp_low_limit = NO_TEMPERATURE_DATA;
            }
        }
    }

    /* if drive temperature has exceeded the critical temperature if available
     */
    if( ( local_temp1_input >= temp_highest_limit ) && ( local_temp1_input != NO_TEMPERATURE_DATA )
        && ( temp_highest_limit != NO_TEMPERATURE_DATA ) )
    {
        /* white on red */
        wattron( main_window, COLOR_PAIR( 6 ) );
        wprintw( main_window, "[%dC]", local_temp1_input );
        wattroff( main_window, COLOR_PAIR( 6 ) );
    }
    else
    {
        /* if drive temperature has exceeded the max temperature if available
         */
        if( ( local_temp1_input >= temp_high_limit ) && ( local_temp1_input <= temp_highest_limit )
            && ( local_temp1_input != NO_TEMPERATURE_DATA ) && ( temp_high_limit != NO_TEMPERATURE_DATA ) )
        {
            /* red on blue */
            wattron( main_window, COLOR_PAIR( 3 ) );
            wprintw( main_window, "[%dC]", local_temp1_input );
            wattroff( main_window, COLOR_PAIR( 3 ) );
        }
        else
        {
            /* if drive temperature is below the lowest critical temperature and the critical value is present
             */
            if( ( local_temp1_input <= temp_lowest_limit ) && ( temp_lowest_limit != NO_TEMPERATURE_DATA )
                && ( local_temp1_input != NO_TEMPERATURE_DATA ) )
            {
                /* white on black */
                wattron( main_window, COLOR_PAIR( 14 ) );
                wprintw( main_window, "[%dC]", local_temp1_input );
                wattroff( main_window, COLOR_PAIR( 14 ) );
            }
            else
            {
                /* if drive temperature is below the minimum but above the lowest temperature and the value is present
                 */
                if( ( ( local_temp1_input <= temp_low_limit ) && ( local_temp1_input >= temp_lowest_limit )
                      && ( local_temp1_input != NO_TEMPERATURE_DATA ) && ( temp_low_limit != NO_TEMPERATURE_DATA ) ) )
                {
                    /* black on blue */
                    wattron( main_window, COLOR_PAIR( 11 ) );
                    wprintw( main_window, "[%dC]", local_temp1_input );
                    wattroff( main_window, COLOR_PAIR( 11 ) );
                }
                else
                {
                    if( local_temp1_input != NO_TEMPERATURE_DATA )
                    {
                        /* Default white on blue */
                        wprintw( main_window, "[%dC]", local_temp1_input );
                    }
                    else
                    {
                        /* Default white on blue */
                        wprintw( main_window, "[--C]" );
                    }
                }
            }
        }
    }
}

char* str_truncate( int wcols, int start_column, const char* input, char* output, int output_length )
{
    /***
     * Truncate a string based on start position and terminal width
     */

    int length, idx = 0;

    length = wcols - start_column - 1;
    idx = 0;
    while( idx < output_length && idx < length )
    {
        output[idx] = input[idx];
        idx++;
    }
    /* terminate the string */
    output[idx] = 0;

    return output;
}
