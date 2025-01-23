/*
 *  options.h: Command line processing routines for nwipe.
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

#ifndef OPTIONS_H_
#define OPTIONS_H_

/* Program knobs. */
#define NWIPE_KNOB_ENTROPY "/dev/urandom"
#define NWIPE_KNOB_IDENTITY_SIZE 512
#define NWIPE_KNOB_LABEL_SIZE 128
#define NWIPE_KNOB_LOADAVG "/proc/loadavg"
#define NWIPE_KNOB_LOG_BUFFERSIZE 1024  // Maximum length of a log event.
#define NWIPE_KNOB_PARTITIONS "/proc/partitions"
#define NWIPE_KNOB_PARTITIONS_PREFIX "/dev/"
#define NWIPE_KNOB_PRNG_STATE_LENGTH 512  // 128 words
#define NWIPE_KNOB_SCSI "/proc/scsi/scsi"
#define NWIPE_KNOB_SLEEP 1
#define NWIPE_KNOB_STAT "/proc/stat"
#define MAX_NUMBER_EXCLUDED_DRIVES 32
#define MAX_DRIVE_PATH_LENGTH 200  // e.g. /dev/sda is only 8 characters long, so 200 should be plenty.
#define DEFAULT_SYNC_RATE 100000
#define PATHNAME_MAX 2048

/* Function prototypes for loading options from the environment and command line. */
int nwipe_options_parse( int argc, char** argv );
void nwipe_options_log( void );

/* Function to display help text */
void display_help();

typedef struct
{
    int autonuke;  // Do not prompt the user for confirmation when set.
    int autopoweroff;  // Power off on completion of wipe
    int noblank;  // Do not perform a final blanking pass.
    int nousb;  // Do not show or wipe any USB devices.
    int nowait;  // Do not wait for a final key before exiting.
    int nosignals;  // Do not allow signals to interrupt a wipe.
    int nogui;  // Do not show the GUI.
    char* banner;  // The product banner shown on the top line of the screen.
    void* method;  // A function pointer to the wipe method that will be used.
    char logfile[FILENAME_MAX];  // The filename to log the output to.
    char PDFreportpath[PATHNAME_MAX];  // The path to write the PDF report to.
    char exclude[MAX_NUMBER_EXCLUDED_DRIVES][MAX_DRIVE_PATH_LENGTH];  // Drives excluded from the search.
    nwipe_prng_t* prng;  // The pseudo random number generator implementation. pointer to the function.
    int quiet;  // Anonymize serial numbers
    int rounds;  // The number of times that the wipe method should be called.
    int sync;  // A flag to indicate whether and how often writes should be sync'd.
    int verbose;  // Make log more verbose
    int PDF_enable;  // 0=PDF creation disabled, 1=PDF creation enabled
    int PDF_preview_details;  // 0=Disable preview Org/Cust/date/time before drive selection, 1=Enable Preview
    nwipe_verify_t verify;  // A flag to indicate whether writes should be verified.
} nwipe_options_t;

extern nwipe_options_t nwipe_options;

#endif /* OPTIONS_H_ */
