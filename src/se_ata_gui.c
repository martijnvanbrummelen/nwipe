/*
 * se_ata_gui.c: ATA Secure Erase (GUI)
 * Author: Copyright (c) 2026 desertwitch (dezertwitsh@gmail.com)
 * Based on: hdparm 9.65 - (c) 2007 Mark Lord (BSD-style license)
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <panel.h>

#include "nwipe.h"
#include "context.h"
#include "gui.h"
#include "logging.h"
#include "se_ata.h"

extern int terminate_signal;
extern WINDOW* main_window;
extern WINDOW* footer_window;
extern PANEL* footer_panel;

extern void nwipe_gui_title( WINDOW* w, const char* s );
extern void nwipe_gui_amend_footer_window( const char* footer_text );
extern void nwipe_gui_create_all_windows_on_terminal_resize( int force, const char* footer );

#define NWIPE_GUI_SE_ATA_ACTION_COUNT 4
#define NWIPE_GUI_SE_ATA_ACTION_DESC_LINES 4

typedef struct
{
    nwipe_se_ata_sanact_e sanact;
    const char* label;
    const char* desc_lines[NWIPE_GUI_SE_ATA_ACTION_DESC_LINES];
    int desc_count;
} nwipe_gui_se_ata_action_t;

static const char* nwipe_gui_se_ata_title = " ATA Sanitize ";

static const nwipe_gui_se_ata_action_t nwipe_gui_se_ata_actions[NWIPE_GUI_SE_ATA_ACTION_COUNT] = {
    { NWIPE_SE_ATA_SANACT_BLOCK_ERASE,
      "Block Erase",
      { "Erases all data using the device's native erase",
        "capabilities. Completes quickly and is supported",
        "by most SSDs and other modern ATA/SATA devices.",
        NULL },
      3 },
    { NWIPE_SE_ATA_SANACT_CRYPTO_SCRAMBLE,
      "Crypto Scramble",
      { "Erases all data by destroying the key that",
        "protects the encrypted contents. The fastest",
        "method, ideal for those devices with built-in",
        "encryption, while offering maximum security." },
      4 },
    { NWIPE_SE_ATA_SANACT_OVERWRITE,
      "Overwrite",
      { "Erases all data by writing a pattern over every",
        "area of the device. The slowest method and",
        "increases wear; prefer Block Erase or Crypto",
        "Scramble methods if the device supports them." },
      4 },
    { NWIPE_SE_ATA_SANACT_EXIT_FAILURE,
      "Exit Failure Mode",
      { "Clears a previously failed sanitize status and",
        "restores the device so that new actions can be",
        "started.",
        NULL },
      3 },
}; /* nwipe_gui_se_ata_actions */

static int nwipe_gui_se_ata_action_supported( nwipe_se_ata_ctx* san, nwipe_se_ata_sanact_e act )
{
    switch( act )
    {
        case NWIPE_SE_ATA_SANACT_BLOCK_ERASE:
            return san->cap_block_erase;
        case NWIPE_SE_ATA_SANACT_CRYPTO_SCRAMBLE:
            return san->cap_crypto_erase;
        case NWIPE_SE_ATA_SANACT_OVERWRITE:
            return san->cap_overwrite;
        case NWIPE_SE_ATA_SANACT_EXIT_FAILURE:
            return 1; /* Always allowed */
        default:
            return 0;
    }
} /* nwipe_gui_se_ata_action_supported */

static const char* nwipe_gui_se_ata_status_str( nwipe_se_ata_ctx* san )
{
    switch( san->state )
    {
        case NWIPE_SE_ATA_STATE_SUCCESS:
            return "Sanitized (Success)";
        case NWIPE_SE_ATA_STATE_IN_PROGRESS:
            return "Sanitizing (In Progress)";
        case NWIPE_SE_ATA_STATE_FAILURE:
            return "Sanitize Failed";
        case NWIPE_SE_ATA_STATE_FROZEN:
            return "Frozen (Locked)";
        case NWIPE_SE_ATA_STATE_UNKNOWN:
        default:
            return "Unknown";
    }
} /* nwipe_gui_se_ata_status_str */

static const char* nwipe_gui_se_ata_action_str( nwipe_se_ata_sanact_e act )
{
    for( int i = 0; i < NWIPE_GUI_SE_ATA_ACTION_COUNT; i++ )
    {
        if( nwipe_gui_se_ata_actions[i].sanact == act )
            return nwipe_gui_se_ata_actions[i].label;
    }
    return "Unknown";
} /* nwipe_gui_se_ata_action_str */

static void nwipe_gui_se_ata_print_device( nwipe_context_t* ctx,
                                           nwipe_se_ata_ctx* san,
                                           WINDOW* win,
                                           int* y,
                                           int x,
                                           nwipe_se_ata_sanact_e* act )
{
    mvwprintw( win,
               ( *y )++,
               x,
               "Device: %s (%s / %s) [0x%02x]",
               san->device_path,
               ctx->device_model ? ctx->device_model : "?",
               ctx->device_serial_no[0] ? ctx->device_serial_no : "?",
               (unsigned) san->state_raw );

    if( act )
        mvwprintw( win, ( *y )++, x, "Action: %s", nwipe_gui_se_ata_action_str( *act ) );
} /* nwipe_gui_se_ata_print_device */

static void nwipe_gui_se_ata_progress_bar( WINDOW* win, int y, int x, int width, int pct )
{
    int filled = ( pct * width ) / 100;
    char bar[64];

    if( width > (int) sizeof( bar ) - 1 )
        width = (int) sizeof( bar ) - 1;

    if( width < 0 )
        width = 0;

    if( filled < 0 )
        filled = 0;

    if( filled > width )
        filled = width;

    memset( bar, '#', (size_t) filled );
    memset( bar + filled, '-', (size_t) ( width - filled ) );

    bar[width] = '\0';
    mvwprintw( win, y, x, "[%s]", bar );
} /* nwipe_gui_se_ata_progress_bar */

static void
nwipe_gui_se_ata_show_error( nwipe_context_t* ctx, nwipe_se_ata_ctx* san, const char* line1, const char* error_msg )
{
    const char* ftr = "Enter=Return";

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int tab1 = 2;
        int keystroke;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, NULL );
        yy++;

        if( line1 )
            mvwprintw( main_window, yy++, tab1, "%s", line1 );
        if( error_msg && error_msg[0] )
            mvwprintw( main_window, yy++, tab1, "Error message: %s", error_msg );

        yy++;
        mvwprintw( main_window, yy++, tab1, "State machine was at: %s", nwipe_gui_se_ata_status_str( san ) );
        mvwprintw( main_window, yy++, tab1, "Do not derive a sanitize success/failure from this diagnostic value." );

        yy++;
        mvwprintw( main_window, yy++, tab1, "Press Enter to leave this screen..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_ENTER:
            case 10:
            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */
                return;
        }
    } while( terminate_signal != 1 );
} /* nwipe_gui_se_ata_show_error */

static int nwipe_gui_se_ata_select_action( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr = "J=Down K=Up Space|Enter=Select ESC=Cancel";
    int focus = 0;

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int ly = 2;
        int ry = 2;
        int keystroke;
        const int tab1 = 2;
        const int tab2 = 30;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &ly, tab1, NULL );
        ly++;

        ry = ly;

        for( int i = 0; i < NWIPE_GUI_SE_ATA_ACTION_COUNT; i++ )
        {
            if( nwipe_gui_se_ata_action_supported( san, nwipe_gui_se_ata_actions[i].sanact ) )
            {
                mvwprintw( main_window, ly + i, tab1, "  %s", nwipe_gui_se_ata_actions[i].label );
            }
            else
            {
                wattron( main_window, A_DIM );
                mvwprintw( main_window, ly + i, tab1, "  %s (n/a)", nwipe_gui_se_ata_actions[i].label );
                wattroff( main_window, A_DIM );
            }
        }

        mvwaddch( main_window, ly + focus, tab1, ACS_RARROW );

        if( nwipe_gui_se_ata_action_supported( san, nwipe_gui_se_ata_actions[focus].sanact ) )
        {
            for( int d = 0; d < nwipe_gui_se_ata_actions[focus].desc_count; d++ )
                mvwprintw( main_window, ry + d, tab2, "%s", nwipe_gui_se_ata_actions[focus].desc_lines[d] );
        }
        else
        {
            mvwprintw( main_window, ry, tab2, "Device does not support this sanitize action." );
        }

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':
                if( focus < NWIPE_GUI_SE_ATA_ACTION_COUNT - 1 )
                    focus++;
                break;

            case KEY_UP:
            case 'k':
            case 'K':
                if( focus > 0 )
                    focus--;
                break;

            case KEY_ENTER:
            case ' ':
            case 10:
                if( nwipe_gui_se_ata_action_supported( san, nwipe_gui_se_ata_actions[focus].sanact ) )
                {
                    san->planned_sanact = nwipe_gui_se_ata_actions[focus].sanact;
                    return 1;
                }
                else
                {
                    beep();
                }
                break;

            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27:
                return 0;
        }

    } while( terminate_signal != 1 );

    return 0;
} /* nwipe_gui_se_ata_select_action */

static int nwipe_gui_se_ata_overwrite_opts( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    int focus = 0;
    const int menu_count = 2;
    const char* ftr = "J=Down K=Up +/-=Change Enter=Confirm ESC=Cancel";

    san->owpass = 0;
    san->ovrpat = 0xDEADBEEF;

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;
        const int tab2 = 42;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->planned_sanact );
        yy++;

        mvwprintw( main_window, yy + 0, tab1, "  Overwrite Passes:" );
        mvwprintw( main_window, yy + 0, tab2, "%d", san->owpass + 1 );

        mvwprintw( main_window, yy + 1, tab1, "  Overwrite Pattern:" );
        mvwprintw( main_window, yy + 1, tab2, "0x%08X", san->ovrpat );

        mvwaddch( main_window, yy + focus, tab1, ACS_RARROW );

        yy += menu_count + 1;
        mvwprintw( main_window, yy++, tab1, "Use +/- or left/right arrows to change values." );
        mvwprintw( main_window, yy++, tab1, "Press Enter to confirm your selected options..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_DOWN:
            case 'j':
            case 'J':
                if( focus < menu_count - 1 )
                    focus++;
                break;

            case KEY_UP:
            case 'k':
            case 'K':
                if( focus > 0 )
                    focus--;
                break;

            case '+':
            case KEY_RIGHT:
                switch( focus )
                {
                    case 0:
                        if( san->owpass < 15 )
                            san->owpass++;
                        break;
                    case 1:
                        san->ovrpat += 1;
                        break;
                }
                break;

            case '-':
            case KEY_LEFT:
                switch( focus )
                {
                    case 0:
                        if( san->owpass > 0 )
                            san->owpass--;
                        break;
                    case 1:
                        san->ovrpat -= 1;
                        break;
                }
                break;

            case KEY_ENTER:
            case 10:
                return 1;

            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27:
                return 0;
        }

    } while( terminate_signal != 1 );

    return 0;
} /* nwipe_gui_se_ata_overwrite_opts */

static void nwipe_gui_se_ata_monitor( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr_progress = "ESC=Stop Monitoring";
    int user_aborted = 0;

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr_progress );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        int poll_err;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr_progress );

        poll_err = nwipe_se_ata_poll( san );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->sanact );
        yy++;

        mvwprintw( main_window, yy++, tab1, "Device accepted the command." );

        if( poll_err )
        {
            mvwprintw( main_window, yy++, tab1, "Unable to read sanitize status (error %d)", poll_err );
            if( san->error_msg[0] )
                mvwprintw( main_window, yy++, tab1, "Error message: %s", san->error_msg );
        }
        else
        {
            mvwprintw( main_window, yy++, tab1, "Device status: %s", nwipe_gui_se_ata_status_str( san ) );
            yy++;

            if( san->state == NWIPE_SE_ATA_STATE_IN_PROGRESS )
            {
                mvwprintw( main_window,
                           yy++,
                           tab1,
                           "Progress: %d%% [0x%04x]",
                           san->progress_pct,
                           (unsigned) san->progress_raw );
                nwipe_gui_se_ata_progress_bar( main_window, yy++, tab1, 40, san->progress_pct );
            }
        }

        yy++;
        mvwprintw( main_window, yy++, tab1, "Do not panic if no progress is reported; some" );
        mvwprintw( main_window, yy++, tab1, "devices become unresponsive until completion." );
        mvwprintw( main_window, yy++, tab1, "Just keep waiting, it can take a long time..." );
        mvwprintw( main_window, yy++, tab1, "DO NOT RESTART SYSTEM AND NEVER CUT THE POWER" );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        /* Finished? */
        if( !poll_err && san->state != NWIPE_SE_ATA_STATE_IN_PROGRESS )
            break;

        /* Wait ~5s, check for ESC */
        for( int tick = 0; tick < 20 && terminate_signal != 1; tick++ )
        {
            timeout( 250 );
            keystroke = getch();
            timeout( -1 );

            switch( keystroke )
            {
                case KEY_BACKSPACE:
                case KEY_BREAK:
                case 27: /* ESC */
                    user_aborted = 1;
                    break;
            }
            if( user_aborted )
                break;
        }

        if( user_aborted )
            break;

    } while( terminate_signal != 1 );

    const char* ftr_results = "Enter=Return";
    const char* result_status_str = nwipe_gui_se_ata_status_str( san );
    int logged = 0;

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr_results );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr_results );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->sanact );
        yy++;

        if( user_aborted )
        {
            mvwprintw( main_window, yy++, tab1, "You stopped monitoring. But the started action" );
            mvwprintw( main_window, yy++, tab1, "is possibly still running on the device itself." );
            yy++;
            mvwprintw( main_window, yy++, tab1, "Last reported progress: %d%%", san->progress_pct );
            yy++;
            mvwprintw( main_window, yy++, tab1, "Do not restart or cut the power until complete." );
        }
        else if( san->state == NWIPE_SE_ATA_STATE_SUCCESS )
        {
            mvwprintw( main_window, yy++, tab1, "Action completed with success." );
            mvwprintw( main_window, yy++, tab1, "Device status: %s", result_status_str );

            if( san->destructive_sanact )
            {
                /* We only update global secure erase state if it was a sanitize action */
                ctx->secure_erase_status = NWIPE_SECURE_ERASE_SUCCESS; /* Global state */
            }

            if( !logged )
            {
                nwipe_log( NWIPE_LOG_INFO,
                           "%s: SANITIZE action '%s' was successful.",
                           san->device_path,
                           nwipe_gui_se_ata_action_str( san->sanact ) );
                logged = 1;
            }
        }
        else if( san->state == NWIPE_SE_ATA_STATE_FAILURE )
        {
            mvwprintw( main_window, yy++, tab1, "Action failed with errors." );
            mvwprintw( main_window, yy++, tab1, "Device status: %s", result_status_str );

            if( san->destructive_sanact )
            {
                /* We only update global secure erase state if it was a sanitize action */
                ctx->secure_erase_status = NWIPE_SECURE_ERASE_FAILURE; /* Global state */
            }

            if( !logged )
            {
                nwipe_log( NWIPE_LOG_ERROR,
                           "%s: SANITIZE action '%s' has failed.",
                           san->device_path,
                           nwipe_gui_se_ata_action_str( san->sanact ) );
                logged = 1;
            }
        }
        else
        {
            mvwprintw( main_window, yy++, tab1, "Action completed." );
            mvwprintw( main_window, yy++, tab1, "The device did not return a success or failure." );
            mvwprintw( main_window, yy++, tab1, "Device status: %s", result_status_str );

            if( san->destructive_sanact )
            {
                /* We only update global secure erase state if it was a sanitize action */
                ctx->secure_erase_status = NWIPE_SECURE_ERASE_SUCCESS; /* Global state */
            }

            if( !logged )
            {
                nwipe_log( NWIPE_LOG_INFO,
                           "%s: SANITIZE action '%s' has completed.",
                           san->device_path,
                           nwipe_gui_se_ata_action_str( san->sanact ) );
                logged = 1;
            }
        }

        yy++;
        if( !user_aborted && san->destructive_sanact )
            mvwprintw( main_window, yy++, tab1, "Don't forget to follow up with a regular wipe." );
        mvwprintw( main_window, yy++, tab1, "Press Enter to leave this screen..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_ENTER:
            case 10:
            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */
                return;
        }
    } while( terminate_signal != 1 );
} /* nwipe_gui_se_ata_monitor */

static int nwipe_gui_se_ata_prompt_in_progress( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr = "M=Monitor ESC=Cancel";

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->sanact );
        yy++;

        mvwprintw( main_window, yy++, tab1, "An action is already RUNNING on this device!" );
        yy++;

        mvwprintw( main_window, yy++, tab1, "Last reported progress: %d%%", san->progress_pct );
        nwipe_gui_se_ata_progress_bar( main_window, yy++, tab1, 40, san->progress_pct );
        yy++;

        mvwprintw( main_window, yy++, tab1, "Press M to monitor the running action," );
        mvwprintw( main_window, yy++, tab1, "or alternatively ESC to leave this screen..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case 'm':
            case 'M':
                return 1;

            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */
                return 0;
        }

    } while( terminate_signal != 1 );

    return 0;
} /* nwipe_gui_se_ata_prompt_in_progress */

static void nwipe_gui_se_ata_show_frozen_state( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr = "Enter=Return";

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, NULL );
        yy++;

        mvwprintw( main_window, yy++, tab1, "The device is currently in FROZEN state!" );
        mvwprintw( main_window, yy++, tab1, "Device status: %s", nwipe_gui_se_ata_status_str( san ) );
        yy++;

        mvwprintw( main_window, yy++, tab1, "Sanitize commands cannot be issued while frozen." );
        mvwprintw( main_window, yy++, tab1, "A power cycle of the device is typically needed" );
        mvwprintw( main_window, yy++, tab1, "to clear a frozen state (e.g. reconnecting device)" );

        yy++;
        mvwprintw( main_window, yy++, tab1, "Press Enter to leave this screen..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_ENTER:
            case 10:
            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */
                return;
        }
    } while( terminate_signal != 1 );
} /* nwipe_gui_se_ata_show_frozen_state */

static void nwipe_gui_se_ata_show_failed_state( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr = "Enter=Continue";

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->sanact );
        yy++;

        mvwprintw( main_window, yy++, tab1, "The most recent action has FAILED!" );
        mvwprintw( main_window, yy++, tab1, "Device status: %s", nwipe_gui_se_ata_status_str( san ) );
        yy++;

        mvwprintw( main_window, yy++, tab1, "The device may be in sanitize-failed mode." );
        mvwprintw( main_window, yy++, tab1, "You can use 'Exit Failure Mode' from the next menu" );
        mvwprintw( main_window, yy++, tab1, "to clear this state before starting a new sanitize." );

        yy++;
        mvwprintw( main_window, yy++, tab1, "Press Enter to continue to the action menu..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case KEY_ENTER:
            case 10:
            case KEY_BACKSPACE:
            case KEY_BREAK:
            case 27: /* ESC */
                return;
        }
    } while( terminate_signal != 1 );
} /* nwipe_gui_se_ata_show_failed_state */

static int nwipe_gui_se_ata_confirm( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    const char* ftr = "E=Execute ESC=Cancel";

    werase( footer_window );
    nwipe_gui_title( footer_window, ftr );
    wrefresh( footer_window );

    do
    {
        int yy = 2;
        int keystroke;
        const int tab1 = 2;

        werase( main_window );
        nwipe_gui_create_all_windows_on_terminal_resize( 0, ftr );

        nwipe_gui_se_ata_print_device( ctx, san, main_window, &yy, tab1, &san->planned_sanact );
        yy++;

        if( san->planned_sanact == NWIPE_SE_ATA_SANACT_OVERWRITE )
        {
            mvwprintw( main_window, yy++, tab1, "  Passes : %d", san->owpass + 1 );
            mvwprintw( main_window, yy++, tab1, "  Pattern: 0x%08X", san->ovrpat );
            yy++;
            mvwprintw( main_window, yy++, tab1, "Beware with some devices overwrite is blocking;" );
            mvwprintw( main_window, yy++, tab1, "possibly severely stalling until all completed." );
            mvwprintw( main_window, yy++, tab1, "Strongly recommended to just do a regular wipe," );
            mvwprintw( main_window, yy++, tab1, "or use other secure erase methods if available." );
            yy++;
        }

        if( san->destructive_sanact )
        {
            mvwprintw( main_window, yy++, tab1, "WARNING: All DATA on the device will be DESTROYED!" );
            yy++;
        }

        mvwprintw( main_window, yy++, tab1, "Beware that this is the FINAL CONFIRMATION screen." );
        mvwprintw( main_window, yy++, tab1, "Press 'e' to execute now or ESC to cancel instead..." );

        box( main_window, 0, 0 );
        nwipe_gui_title( main_window, nwipe_gui_se_ata_title );
        wrefresh( main_window );

        timeout( 250 );
        keystroke = getch();
        timeout( -1 );

        switch( keystroke )
        {
            case 'e':
            case 'E':
                return 1;

            case 27:
            case KEY_BACKSPACE:
            case KEY_BREAK:
                return 0;
        }

    } while( terminate_signal != 1 );

    return 0;
} /* nwipe_gui_se_ata_confirm */

/* Public entry-point for ATA Sanitize GUI */
void nwipe_gui_se_ata_sanitize( nwipe_context_t* ctx, nwipe_se_ata_ctx* san )
{
    /* Open the ATA device */
    if( nwipe_se_ata_open( san ) != 0 )
    {
        nwipe_gui_se_ata_show_error( ctx, san, "Failed to open ATA device.", san->error_msg );
        return;
    }

    /* Get the current status first so we can decide the entry point */
    if( nwipe_se_ata_poll( san ) != 0 )
    {
        nwipe_gui_se_ata_show_error( ctx, san, "Failed to read the sanitize status.", san->error_msg );
        nwipe_se_ata_close( san );
        return;
    }

    /* If the device is frozen, tell the user and bail out */
    if( san->state == NWIPE_SE_ATA_STATE_FROZEN )
    {
        nwipe_gui_se_ata_show_frozen_state( ctx, san );
        nwipe_se_ata_close( san );
        return;
    }

    /* If a sanitize is in progress, offer to monitor its progress */
    if( san->state == NWIPE_SE_ATA_STATE_IN_PROGRESS )
    {
        if( nwipe_gui_se_ata_prompt_in_progress( ctx, san ) )
        {
            /* User wanted to monitor its progress */
            nwipe_gui_se_ata_monitor( ctx, san );
        }
        nwipe_se_ata_close( san );
        return;
    }

    /* If a sanitize has failed, advise user how to clear failure state */
    if( san->state == NWIPE_SE_ATA_STATE_FAILURE )
    {
        nwipe_gui_se_ata_show_failed_state( ctx, san );
    }

    /* Now let the user select a sanitize action */
    if( !nwipe_gui_se_ata_select_action( ctx, san ) )
    {
        /* User wanted to cancel */
        nwipe_se_ata_close( san );
        return;
    }

    /* If it's the overwrite method, show the overwrite options */
    if( san->planned_sanact == NWIPE_SE_ATA_SANACT_OVERWRITE )
    {
        if( !nwipe_gui_se_ata_overwrite_opts( ctx, san ) )
        {
            /* User wanted to cancel */
            nwipe_se_ata_close( san );
            return;
        }
    }
    /* Otherwise clear overwrite-specific fields */
    else
    {
        san->owpass = 0;
        san->ovrpat = 0;
    }

    /* Final confirmation screen before sanitize operation */
    if( !nwipe_gui_se_ata_confirm( ctx, san ) )
    {
        /* User wanted to cancel */
        nwipe_se_ata_close( san );
        return;
    }

    /* Issue the sanitize command */
    if( nwipe_se_ata_sanitize( san ) != 0 )
    {
        nwipe_gui_se_ata_show_error( ctx, san, "Sanitize command was rejected by device.", san->error_msg );
        nwipe_se_ata_close( san );
        return;
    }

    /* Monitor the results */
    nwipe_gui_se_ata_monitor( ctx, san );

    nwipe_se_ata_close( san );
} /* nwipe_gui_se_ata_sanitize */
