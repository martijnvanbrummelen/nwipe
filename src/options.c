/*
 *  options.c:  Command line processing routines for nwipe.
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

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "logging.h"
#include "version.h"
#include "conf.h"

/* The global options struct. */
nwipe_options_t nwipe_options;

int nwipe_options_parse( int argc, char** argv )
{
    extern char* optarg;  // The working getopt option argument.
    extern int optind;  // The working getopt index into argv.
    extern int optopt;  // The last unhandled getopt option.
    extern int opterr;  // The last getopt error number.

    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;
    extern nwipe_prng_t nwipe_aes_ctr_prng;

    /* The getopt() result holder. */
    int nwipe_opt;

    /* Excluded drive indexes */
    int idx_drive_chr;
    int idx_optarg;
    int idx_drive;

    /* Array index variable. */
    int i;

    /* The list of acceptable short options. */
    char nwipe_options_short[] = "Vvhl:P:m:p:qr:e:";

    /* Used when reading value fron nwipe.conf */
    const char* read_value = NULL;

    int ret;

    /* The list of acceptable long options. */
    static struct option nwipe_options_long[] = {
        /* Set when the user wants to wipe without a confirmation prompt. */
        { "autonuke", no_argument, 0, 0 },

        /* Set when the user wants to have the system powerdown on completion of wipe. */
        { "autopoweroff", no_argument, 0, 0 },

        /* A GNU standard option. Corresponds to the 'h' short option. */
        { "help", no_argument, 0, 'h' },

        /* The wipe method. Corresponds to the 'm' short option. */
        { "method", required_argument, 0, 'm' },

        /* Log file. Corresponds to the 'l' short option. */
        { "logfile", required_argument, 0, 'l' },

        /* PDFreport path. Corresponds to the 'P' short option. */
        { "PDFreportpath", required_argument, 0, 'P' },

        /* Exclude devices, comma separated list */
        { "exclude", required_argument, 0, 'e' },

        /* The Pseudo Random Number Generator. */
        { "prng", required_argument, 0, 'p' },

        /* The number of times to run the method. */
        { "rounds", required_argument, 0, 'r' },

        /* Whether to blank the disk after wiping. */
        { "noblank", no_argument, 0, 0 },

        /* Whether to ignore all USB devices. */
        { "nousb", no_argument, 0, 0 },

        /* Whether to exit after wiping or wait for a keypress. */
        { "nowait", no_argument, 0, 0 },

        /* Whether to allow signals to interrupt a wipe. */
        { "nosignals", no_argument, 0, 0 },

        /* Whether to display the gui. */
        { "nogui", no_argument, 0, 0 },

        /* Whether to anonymize the serial numbers. */
        { "quiet", no_argument, 0, 'q' },

        /* A flag to indicate whether the devices would be opened in sync mode. */
        { "sync", required_argument, 0, 0 },

        /* Verify that wipe patterns are being written to the device. */
        { "verify", required_argument, 0, 0 },

        /* Display program version. */
        { "verbose", no_argument, 0, 'v' },

        /* Display program version. */
        { "version", no_argument, 0, 'V' },

        /* Requisite padding for getopt(). */
        { 0, 0, 0, 0 } };

    /* Set default options. */
    nwipe_options.autonuke = 0;
    nwipe_options.autopoweroff = 0;
    nwipe_options.method = &nwipe_random;
    nwipe_options.prng =
        ( sizeof( unsigned long int ) >= 8 ) ? &nwipe_xoroshiro256_prng : &nwipe_add_lagg_fibonacci_prng;
    nwipe_options.rounds = 1;
    nwipe_options.noblank = 0;
    nwipe_options.nousb = 0;
    nwipe_options.nowait = 0;
    nwipe_options.nosignals = 0;
    nwipe_options.nogui = 0;
    nwipe_options.quiet = 0;
    nwipe_options.sync = DEFAULT_SYNC_RATE;
    nwipe_options.verbose = 0;
    nwipe_options.verify = NWIPE_VERIFY_LAST;
    memset( nwipe_options.logfile, '\0', sizeof( nwipe_options.logfile ) );
    memset( nwipe_options.PDFreportpath, '\0', sizeof( nwipe_options.PDFreportpath ) );
    strncpy( nwipe_options.PDFreportpath, ".", 2 );

    /* Read PDF settings from nwipe.conf if available  */
    if( ( ret = nwipe_conf_read_setting( "PDF_Certificate.PDF_Enable", &read_value ) ) )
    {
        /* error occurred */
        nwipe_log( NWIPE_LOG_ERROR,
                   "nwipe_conf_read_setting():Error reading PDF_Certificate.PDF_Enable from nwipe.conf, ret code %i",
                   ret );

        /* Use default values */
        nwipe_options.PDF_enable = 1;
    }
    else
    {
        if( !strcmp( read_value, "ENABLED" ) )
        {
            nwipe_options.PDF_enable = 1;
        }
        else
        {
            if( !strcmp( read_value, "DISABLED" ) )
            {
                nwipe_options.PDF_enable = 0;
            }
            else
            {
                // error occurred
                nwipe_log(
                    NWIPE_LOG_ERROR,
                    "PDF_Certificate.PDF_Enable in nwipe.conf returned a value that was neither ENABLED or DISABLED" );
                nwipe_options.PDF_enable = 1;  // Default to Enabled
            }
        }
    }

    /* PDF Preview enable/disable */
    if( ( ret = nwipe_conf_read_setting( "PDF_Certificate.PDF_Preview", &read_value ) ) )
    {
        /* error occurred */
        nwipe_log( NWIPE_LOG_ERROR,
                   "nwipe_conf_read_setting():Error reading PDF_Certificate.PDF_Preview from nwipe.conf, ret code %i",
                   ret );

        /* Use default values */
        nwipe_options.PDF_enable = 1;
    }
    else
    {
        if( !strcmp( read_value, "ENABLED" ) )
        {
            nwipe_options.PDF_preview_details = 1;
        }
        else
        {
            if( !strcmp( read_value, "DISABLED" ) )
            {
                nwipe_options.PDF_preview_details = 0;
            }
            else
            {
                /* error occurred */
                nwipe_log(
                    NWIPE_LOG_ERROR,
                    "PDF_Certificate.PDF_Preview in nwipe.conf returned a value that was neither ENABLED or DISABLED" );
                nwipe_options.PDF_preview_details = 1; /* Default to Enabled */
            }
        }
    }

    /* Initialise each of the strings in the excluded drives array */
    for( i = 0; i < MAX_NUMBER_EXCLUDED_DRIVES; i++ )
    {
        nwipe_options.exclude[i][0] = 0;
    }

    /* Parse command line options. */
    while( 1 )
    {
        /* Get the next command line option with (3)getopt. */
        nwipe_opt = getopt_long( argc, argv, nwipe_options_short, nwipe_options_long, &i );

        /* Break when we have processed all of the given options. */
        if( nwipe_opt < 0 )
        {
            break;
        }

        switch( nwipe_opt )
        {
            case 0: /* Long options without short counterparts. */

                if( strcmp( nwipe_options_long[i].name, "autonuke" ) == 0 )
                {
                    nwipe_options.autonuke = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "autopoweroff" ) == 0 )
                {
                    nwipe_options.autopoweroff = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "noblank" ) == 0 )
                {
                    nwipe_options.noblank = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "nousb" ) == 0 )
                {
                    nwipe_options.nousb = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "nowait" ) == 0 )
                {
                    nwipe_options.nowait = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "nosignals" ) == 0 )
                {
                    nwipe_options.nosignals = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "nogui" ) == 0 )
                {
                    nwipe_options.nogui = 1;
                    nwipe_options.nowait = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "verbose" ) == 0 )
                {
                    nwipe_options.verbose = 1;
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "sync" ) == 0 )
                {
                    if( sscanf( optarg, " %i", &nwipe_options.sync ) != 1 || nwipe_options.sync < 0 )
                    {
                        fprintf( stderr, "Error: The sync argument must be a positive integer or zero.\n" );
                        exit( EINVAL );
                    }
                    break;
                }

                if( strcmp( nwipe_options_long[i].name, "verify" ) == 0 )
                {

                    if( strcmp( optarg, "0" ) == 0 || strcmp( optarg, "off" ) == 0 )
                    {
                        nwipe_options.verify = NWIPE_VERIFY_NONE;
                        break;
                    }

                    if( strcmp( optarg, "1" ) == 0 || strcmp( optarg, "last" ) == 0 )
                    {
                        nwipe_options.verify = NWIPE_VERIFY_LAST;
                        break;
                    }

                    if( strcmp( optarg, "2" ) == 0 || strcmp( optarg, "all" ) == 0 )
                    {
                        nwipe_options.verify = NWIPE_VERIFY_ALL;
                        break;
                    }

                    /* Else we do not know this verification level. */
                    fprintf( stderr, "Error: Unknown verification level '%s'.\n", optarg );
                    exit( EINVAL );
                }

                /* getopt_long should raise on invalid option, so we should never get here. */
                exit( EINVAL );

            case 'm': /* Method option. */

                if( strcmp( optarg, "dod522022m" ) == 0 || strcmp( optarg, "dod" ) == 0 )
                {
                    nwipe_options.method = &nwipe_dod522022m;
                    break;
                }

                if( strcmp( optarg, "dodshort" ) == 0 || strcmp( optarg, "dod3pass" ) == 0 )
                {
                    nwipe_options.method = &nwipe_dodshort;
                    break;
                }

                if( strcmp( optarg, "gutmann" ) == 0 )
                {
                    nwipe_options.method = &nwipe_gutmann;
                    break;
                }

                if( strcmp( optarg, "ops2" ) == 0 )
                {
                    nwipe_options.method = &nwipe_ops2;
                    break;
                }

                if( strcmp( optarg, "random" ) == 0 || strcmp( optarg, "prng" ) == 0
                    || strcmp( optarg, "stream" ) == 0 )
                {
                    nwipe_options.method = &nwipe_random;
                    break;
                }

                if( strcmp( optarg, "zero" ) == 0 || strcmp( optarg, "quick" ) == 0 )
                {
                    nwipe_options.method = &nwipe_zero;
                    break;
                }

                if( strcmp( optarg, "one" ) == 0 )
                {
                    nwipe_options.method = &nwipe_one;
                    break;
                }

                if( strcmp( optarg, "verify_zero" ) == 0 )
                {
                    nwipe_options.method = &nwipe_verify_zero;
                    break;
                }

                if( strcmp( optarg, "verify_one" ) == 0 )
                {
                    nwipe_options.method = &nwipe_verify_one;
                    break;
                }

                if( strcmp( optarg, "is5enh" ) == 0 )
                {
                    nwipe_options.method = &nwipe_is5enh;
                    break;
                }

                /* Else we do not know this wipe method. */
                fprintf( stderr, "Error: Unknown wipe method '%s'.\n", optarg );
                exit( EINVAL );

            case 'l': /* Log file option. */

                nwipe_options.logfile[strlen( optarg )] = '\0';
                strncpy( nwipe_options.logfile, optarg, sizeof( nwipe_options.logfile ) );
                break;

            case 'P': /* PDFreport path option. */

                nwipe_options.PDFreportpath[strlen( optarg )] = '\0';
                strncpy( nwipe_options.PDFreportpath, optarg, sizeof( nwipe_options.PDFreportpath ) );

                /* Command line options will override what's in nwipe.conf */
                if( strcmp( nwipe_options.PDFreportpath, "noPDF" ) == 0 )
                {
                    nwipe_options.PDF_enable = 0;
                    nwipe_conf_update_setting( "PDF_Certificate.PDF_Enable", "DISABLED" );
                }
                else
                {
                    if( strcmp( nwipe_options.PDFreportpath, "." ) )
                    {
                        /* and if the user has specified a PDF path then enable PDF */
                        nwipe_options.PDF_enable = 1;
                        nwipe_conf_update_setting( "PDF_Certificate.PDF_Enable", "ENABLED" );
                    }
                }

                break;

            case 'e': /* exclude drives option */

                idx_drive_chr = 0;
                idx_optarg = 0;
                idx_drive = 0;

                /* Create an array of excluded drives from the comma separated string */
                while( optarg[idx_optarg] != 0 && idx_drive < MAX_NUMBER_EXCLUDED_DRIVES )
                {
                    /* drop the leading '=' character if used */
                    if( optarg[idx_optarg] == '=' && idx_optarg == 0 )
                    {
                        idx_optarg++;
                        continue;
                    }

                    if( optarg[idx_optarg] == ',' )
                    {
                        /* terminate string and move onto next drive */
                        nwipe_options.exclude[idx_drive++][idx_drive_chr] = 0;
                        idx_drive_chr = 0;
                        idx_optarg++;
                    }
                    else
                    {
                        if( idx_drive_chr < MAX_DRIVE_PATH_LENGTH )
                        {
                            nwipe_options.exclude[idx_drive][idx_drive_chr++] = optarg[idx_optarg++];
                        }
                        else
                        {
                            /* This section deals with file names that exceed MAX_DRIVE_PATH_LENGTH */
                            nwipe_options.exclude[idx_drive][idx_drive_chr] = 0;
                            while( optarg[idx_optarg] != 0 && optarg[idx_optarg] != ',' )
                            {
                                idx_optarg++;
                            }
                        }
                    }
                    if( idx_drive == MAX_NUMBER_EXCLUDED_DRIVES )
                    {
                        fprintf(
                            stderr,
                            "The number of excluded drives has reached the programs configured limit, aborting\n" );
                        exit( 130 );
                    }
                }
                break;

            case 'h': /* Display help. */

                display_help();
                break;

            case 'p': /* PRNG option. */

                if( strcmp( optarg, "mersenne" ) == 0 || strcmp( optarg, "twister" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_twister;
                    break;
                }

                if( strcmp( optarg, "isaac" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_isaac;
                    break;
                }

                if( strcmp( optarg, "isaac64" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_isaac64;
                    break;
                }
                if( strcmp( optarg, "add_lagg_fibonacci_prng" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_add_lagg_fibonacci_prng;
                    break;
                }
                if( strcmp( optarg, "xoroshiro256_prng" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_xoroshiro256_prng;
                    break;
                }
                if( strcmp( optarg, "aes_ctr_prng" ) == 0 )
                {
                    nwipe_options.prng = &nwipe_aes_ctr_prng;
                    break;
                }

                /* Else we do not know this PRNG. */
                fprintf( stderr, "Error: Unknown prng '%s'.\n", optarg );
                exit( EINVAL );

            case 'q': /* Anonymize serial numbers */

                nwipe_options.quiet = 1;
                break;

            case 'r': /* Rounds option. */

                if( sscanf( optarg, " %i", &nwipe_options.rounds ) != 1 || nwipe_options.rounds < 1 )
                {
                    fprintf( stderr, "Error: The rounds argument must be a positive integer.\n" );
                    exit( EINVAL );
                }

                break;

            case 'v': /* verbose */

                nwipe_options.verbose = 1;
                break;

            case 'V': /* Version option. */

                printf( "%s version %s\n", program_name, version_string );
                exit( EXIT_SUCCESS );

            default:

                /* Bogus command line argument. */
                display_help();
                exit( EINVAL );

        } /* method */

    } /* command line options */

    /* Return the number of options that were processed. */
    return optind;
}

void nwipe_options_log( void )
{
    extern nwipe_prng_t nwipe_twister;
    extern nwipe_prng_t nwipe_isaac;
    extern nwipe_prng_t nwipe_isaac64;
    extern nwipe_prng_t nwipe_add_lagg_fibonacci_prng;
    extern nwipe_prng_t nwipe_xoroshiro256_prng;
    extern nwipe_prng_t nwipe_aes_ctr_prng;

    /**
     *  Prints a manifest of options to the log.
     */

    nwipe_log( NWIPE_LOG_NOTICE, "Program options are set as follows..." );

    if( nwipe_options.autonuke )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  autonuke = %i (on)", nwipe_options.autonuke );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  autonuke = %i (off)", nwipe_options.autonuke );
    }

    if( nwipe_options.autopoweroff )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  autopoweroff = %i (on)", nwipe_options.autopoweroff );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  autopoweroff = %i (off)", nwipe_options.autopoweroff );
    }

    if( nwipe_options.noblank )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  do not perform a final blank pass" );
    }

    if( nwipe_options.nowait )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  do not wait for a key before exiting" );
    }

    if( nwipe_options.nosignals )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  do not allow signals to interrupt a wipe" );
    }

    if( nwipe_options.nogui )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  do not show GUI interface" );
    }

    nwipe_log( NWIPE_LOG_NOTICE, "  banner   = %s", banner );

    if( nwipe_options.prng == &nwipe_twister )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = Mersenne Twister" );
    }
    else if( nwipe_options.prng == &nwipe_add_lagg_fibonacci_prng )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = Lagged Fibonacci generator (EXPERIMENTAL!)" );
    }
    else if( nwipe_options.prng == &nwipe_xoroshiro256_prng )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = XORoshiro-256 (EXPERIMENTAL!)" );
    }
    else if( nwipe_options.prng == &nwipe_aes_ctr_prng )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = AES-CTR New Instructions (EXPERIMENTAL!)" );
    }
    else if( nwipe_options.prng == &nwipe_isaac )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = Isaac" );
    }
    else if( nwipe_options.prng == &nwipe_isaac64 )
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = Isaac64" );
    }
    else
    {
        nwipe_log( NWIPE_LOG_NOTICE, "  prng     = Undefined" );
    }

    nwipe_log( NWIPE_LOG_NOTICE, "  method   = %s", nwipe_method_label( nwipe_options.method ) );
    nwipe_log( NWIPE_LOG_NOTICE, "  quiet    = %i", nwipe_options.quiet );
    nwipe_log( NWIPE_LOG_NOTICE, "  rounds   = %i", nwipe_options.rounds );
    nwipe_log( NWIPE_LOG_NOTICE, "  sync     = %i", nwipe_options.sync );

    switch( nwipe_options.verify )
    {
        case NWIPE_VERIFY_NONE:
            nwipe_log( NWIPE_LOG_NOTICE, "  verify   = %i (off)", nwipe_options.verify );
            break;

        case NWIPE_VERIFY_LAST:
            nwipe_log( NWIPE_LOG_NOTICE, "  verify   = %i (last pass)", nwipe_options.verify );
            break;

        case NWIPE_VERIFY_ALL:
            nwipe_log( NWIPE_LOG_NOTICE, "  verify   = %i (all passes)", nwipe_options.verify );
            break;

        default:
            nwipe_log( NWIPE_LOG_NOTICE, "  verify   = %i", nwipe_options.verify );
            break;
    }
}

void display_help()
{
    /**
     * displays the help section to STDOUT and exits.
     */

    printf( "Usage: %s [options] [device1] [device2] ...\n", program_name );
    printf( "Options:\n" );
    /* Limit line length to a maximum of 80 characters so it looks good in 80x25 terminals i.e shredos */
    /*  ___12345678901234567890123456789012345678901234567890123456789012345678901234567890< Do not exceed */
    puts( "  -V, --version           Prints the version number\n" );
    puts( "  -v, --verbose           Prints more messages to the log\n" );
    puts( "  -h, --help              Prints this help\n" );
    puts( "      --autonuke          If no devices have been specified on the command line," );
    puts( "                          starts wiping all devices immediately. If devices have" );
    puts( "                          been specified, starts wiping only those specified" );
    puts( "                          devices immediately.\n" );
    puts( "      --autopoweroff      Power off system on completion of wipe delayed for" );
    puts( "                          for one minute. During this one minute delay you can" );
    puts( "                          abort the shutdown by typing sudo shutdown -c\n" );
    printf( "      --sync=NUM          Will perform a sync after NUM writes (default: %d)\n", DEFAULT_SYNC_RATE );
    puts( "                          0    - fdatasync after the disk is completely written" );
    puts( "                                 fdatasync errors not detected until completion." );
    puts( "                                 0 is not recommended as disk errors may cause" );
    puts( "                                 nwipe to appear to hang" );
    puts( "                          1    - fdatasync after every write" );
    puts( "                                 Warning: Lower values will reduce wipe speeds." );
    puts( "                          1000 - fdatasync after 1000 writes etc.\n" );
    puts( "      --verify=TYPE       Whether to perform verification of erasure" );
    puts( "                          (default: last)" );
    puts( "                          off   - Do not verify" );
    puts( "                          last  - Verify after the last pass" );
    puts( "                          all   - Verify every pass" );
    puts( "                          " );
    puts( "                          Please mind that HMG IS5 enhanced always verifies the" );
    puts( "                          last (PRNG) pass regardless of this option.\n" );
    puts( "  -m, --method=METHOD     The wiping method. See man page for more details." );
    puts( "                          (default: dodshort)" );
    puts( "                          dod522022m / dod       - 7 pass DOD 5220.22-M method" );
    puts( "                          dodshort / dod3pass    - 3 pass DOD method" );
    puts( "                          gutmann                - Peter Gutmann's Algorithm" );
    puts( "                          ops2                   - RCMP TSSIT OPS-II" );
    puts( "                          random / prng / stream - PRNG Stream" );
    puts( "                          zero / quick           - Overwrite with zeros" );
    puts( "                          one                    - Overwrite with ones (0xFF)" );
    puts( "                          verify_zero            - Verifies disk is zero filled" );
    puts( "                          verify_one             - Verifies disk is 0xFF filled" );
    puts( "                          is5enh                 - HMG IS5 enhanced\n" );
    puts( "  -l, --logfile=FILE      Filename to log to. Default is STDOUT\n" );
    puts( "  -P, --PDFreportpath=PATH Path to write PDF reports to. Default is \".\"" );
    puts( "                           If set to \"noPDF\" no PDF reports are written.\n" );
    puts( "  -p, --prng=METHOD       PRNG option "
          "(mersenne|twister|isaac|isaac64|add_lagg_fibonacci_prng|xoroshiro256_prng|aes_ctr_prng)\n" );
    puts( "  -q, --quiet             Anonymize logs and the GUI by removing unique data, i.e." );
    puts( "                          serial numbers, LU WWN Device ID, and SMBIOS/DMI data" );
    puts( "                          XXXXXX = S/N exists, ????? = S/N not obtainable\n" );
    puts( "  -r, --rounds=NUM        Number of times to wipe the device using the selected" );
    puts( "                          method (default: 1)\n" );
    puts( "      --noblank           Do NOT blank disk after wipe" );
    puts( "                          (default is to complete a final blank pass)\n" );
    puts( "      --nowait            Do NOT wait for a key before exiting" );
    puts( "                          (default is to wait)\n" );
    puts( "      --nosignals         Do NOT allow signals to interrupt a wipe" );
    puts( "                          (default is to allow)\n" );
    puts( "      --nogui             Do NOT show the GUI interface. Automatically invokes" );
    puts( "                          the nowait option. Must be used with the --autonuke" );
    puts( "                          option. Send SIGUSR1 to log current stats\n" );
    puts( "      --nousb             Do NOT show or wipe any USB devices whether in GUI" );
    puts( "                          mode, --nogui or --autonuke modes.\n" );
    puts( "  -e, --exclude=DEVICES   Up to ten comma separated devices to be excluded" );
    puts( "                          --exclude=/dev/sdc" );
    puts( "                          --exclude=/dev/sdc,/dev/sdd" );
    puts( "                          --exclude=/dev/sdc,/dev/sdd,/dev/mapper/cryptswap1\n" );
    puts( "" );
    exit( EXIT_SUCCESS );
}
