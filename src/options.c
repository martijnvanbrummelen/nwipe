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

#include "options.h"
#include "context.h"
#include "logging.h"
#include "method.h"
#include "nwipe.h"
#include "prng.h"
#include "version.h"

/* The global options struct. */
nwipe_options_t nwipe_options;

int nwipe_options_parse( int argc, char** argv )
{
	extern char* optarg; /* The working getopt option argument. */
	extern int   optind; /* The working getopt index into argv. */
	extern int   optopt; /* The last unhandled getopt option.   */
	extern int   opterr; /* The last getopt error number.       */

	extern nwipe_prng_t nwipe_twister;
	extern nwipe_prng_t nwipe_isaac;

	/* The getopt() result holder. */
	int nwipe_opt;

	/* Excluded drive indexes */
	int idx_drive_chr;
	int idx_optarg;
	int idx_drive;

	/* Array index variable. */
	int i;

	/* The list of acceptable short options. */
	char nwipe_options_short[] = "Vhl:hm:p:r:e:";

	/* The list of acceptable long options. */
	static struct option nwipe_options_long[] = {
	    /* Set when the user wants to wipe without a confirmation prompt. */
	    {"autonuke", no_argument, 0, 0},

	    /* A GNU standard option. Corresponds to the 'h' short option. */
	    {"help", no_argument, 0, 'h'},

	    /* The wipe method. Corresponds to the 'm' short option. */
	    {"method", required_argument, 0, 'm'},

	    /* Log file. Corresponds to the 'l' short option. */
	    {"logfile", required_argument, 0, 'l'},

	    /* Exclude devices, comma separated list */
	    {"exclude", required_argument, 0, 'e'},

	    /* The Pseudo Random Number Generator. */
	    {"prng", required_argument, 0, 'p'},

	    /* The number of times to run the method. */
	    {"rounds", required_argument, 0, 'r'},

	    /* Whether to blank the disk after wiping. */
	    {"noblank", no_argument, 0, 0},

	    /* Whether to exit after wiping or wait for a keypress. */
	    {"nowait", no_argument, 0, 0},

	    /* Whether to allow signals to interrupt a wipe. */
	    {"nosignals", no_argument, 0, 0},

	    /* Whether to exit after wiping or wait for a keypress. */
	    {"nogui", no_argument, 0, 0},

	    /* A flag to indicate whether the devices whould be opened in sync mode. */
	    {"sync", required_argument, 0, 0},

	    /* Verify that wipe patterns are being written to the device. */
	    {"verify", required_argument, 0, 0},

	    /* Display program version. */
	    {"version", no_argument, 0, 'V'},

	    /* Requisite padding for getopt(). */
	    {0, 0, 0, 0}};

	/* Set default options. */
	nwipe_options.autonuke  = 0;
	nwipe_options.method    = &nwipe_dodshort;
	nwipe_options.prng      = &nwipe_twister;
	nwipe_options.rounds    = 1;
	nwipe_options.noblank   = 0;
	nwipe_options.nowait    = 0;
	nwipe_options.nosignals = 0;
	nwipe_options.nogui     = 0;
	nwipe_options.sync      = 0;
	nwipe_options.verify    = NWIPE_VERIFY_LAST;
	memset( nwipe_options.logfile, '\0', sizeof( nwipe_options.logfile ) );

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

				if( strcmp( nwipe_options_long[i].name, "noblank" ) == 0 )
				{
					nwipe_options.noblank = 1;
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
					nwipe_options.nogui  = 1;
					nwipe_options.nowait = 1;
					break;
				}

				if( strcmp( nwipe_options_long[i].name, "sync" ) == 0 )
				{
					if( sscanf( optarg, " %i", &nwipe_options.sync ) != 1 \
				      || nwipe_options.sync < 1
				    )
					{
				  	fprintf( stderr, "Error: The sync argument must be a positive integer.\n" );
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

				/* Else we do not know this wipe method. */
				fprintf( stderr, "Error: Unknown wipe method '%s'.\n", optarg );
				exit( EINVAL );

			case 'l': /* Log file option. */

				nwipe_options.logfile[strlen( optarg )] = '\0';
				strncpy( nwipe_options.logfile, optarg, sizeof( nwipe_options.logfile ) );
				break;

			case 'e': /* exclude drives option */

				idx_drive_chr = 0;
				idx_optarg    = 0;
				idx_drive     = 0;

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
						idx_drive_chr					  = 0;
						idx_optarg++;
					} else
					{
						if( idx_drive_chr < MAX_DRIVE_PATH_LENGTH )
						{
							nwipe_options.exclude[idx_drive][idx_drive_chr++] =
							    optarg[idx_optarg++];
						} else
						{ /* This section deals with file names that exceed
						     MAX_DRIVE_PATH_LENGTH */
							nwipe_options.exclude[idx_drive][idx_drive_chr] = 0;
							while( optarg[idx_optarg] != 0 || optarg[idx_optarg] != ',' )
							{
								idx_optarg++;
							}
						}
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

				/* Else we do not know this PRNG. */
				fprintf( stderr, "Error: Unknown prng '%s'.\n", optarg );
				exit( EINVAL );

			case 'r': /* Rounds option. */

				if( sscanf( optarg, " %i", &nwipe_options.rounds ) != 1 || nwipe_options.rounds < 1 )
				{
					fprintf( stderr, "Error: The rounds argument must be a positive integer.\n" );
					exit( EINVAL );
				}

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

} /* nwipe_options_parse */

void nwipe_options_log( void )
{
	/**
	 *  Prints a manifest of options to the log.
	 *
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
	nwipe_log( NWIPE_LOG_NOTICE, "  method   = %s", nwipe_method_label( nwipe_options.method ) );
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

} /* nwipe_options_log */

/**
 * display_help
 * displays the help section to STDOUT and exits
 */
void display_help()
{
  printf("Usage: %s [options] [device1] [device2] ...\n", program_name);
  printf("Options:\n"                    );
  /* Limit line length to a maximum of 80 characters so it looks good in 80x25 terminals i.e shredos */
  /* ___12345678901234567890123456789012345678901234567890123456789012345678901234567890< Do not exceed */
  puts("  -V, --version           Prints the version number\n");
  puts("  -h, --help              Prints this help\n");
  puts("      --autonuke          If no devices have been specified on the command line,");
  puts("                          starts wiping all devices immediately. If devices have");
  puts("                          been specified, starts wiping only those specified");
  puts("                          devices immediately.\n");
  puts("      --sync=NUM          Will preform a sync after NUM writes (default: 0)");
  puts("                          0 - fdatasync after the disk is completely written");
  puts("                          1 - fdatasync after every write");
  puts("                          1000000 - fdatasync after 1000000 writes ect.");
  puts("      --verify=TYPE       Whether to perform verification of erasure");
  puts("                          (default: last)");
  puts("                          off   - Do not verify");
  puts("                          last  - Verify after the last pass");
  puts("                          all   - Verify every pass\n");  
  puts("  -m, --method=METHOD     The wiping method. See man page for more details.");
  puts("                          (default: dodshort)");
  puts("                          dod522022m / dod       - 7 pass DOD 5220.22-M method");
  puts("                          dodshort / dod3pass    - 3 pass DOD method");
  puts("                          gutmann                - Peter Gutmann's Algorithm");
  puts("                          ops2                   - RCMP TSSIT OPS-II");
  puts("                          random / prng / stream - PRNG Stream");
  puts("                          zero / quick           - Overwrite with zeros\n");
  puts("  -l, --logfile=FILE      Filename to log to. Default is STDOUT\n");
  puts("  -p, --prng=METHOD       PRNG option (mersenne|twister|isaac)\n");
  puts("  -r, --rounds=NUM        Number of times to wipe the device using the selected");
  puts("                          method (default: 1)\n" );
  puts("      --noblank           Do not blank disk after wipe");
  puts("                          (default is to complete a final blank pass)\n" );
  puts("      --nowait            Do not wait for a key before exiting");
  puts("                          (default is to wait)\n" );
  puts("      --nosignals         Do not allow signals to interrupt a wipe");
  puts("                          (default is to allow)\n" );
  puts("      --nogui             Do not show the GUI interface. Automatically invokes" );
  puts("                          the nowait option. Must be used with the --autonuke");
  puts("                          option. Send SIGUSR1 to log current stats\n");
  puts("  -e, --exclude=DEVICES   Up to ten comma separted devices to be excluded");
  puts("                          --exclude=/dev/sdc");
  puts("                          --exclude=/dev/sdc,/dev/sdd");
  puts("                          --exclude=/dev/sdc,/dev/sdd,/dev/mapper/cryptswap1\n");
  puts("");
  exit( EXIT_SUCCESS );
}

/* eof */
