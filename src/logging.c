/*
 *  logging.c:  Logging facilities for nwipe.
 *
 *  Copyright Darik Horn <dajhorn-dban@vanadac.com>.
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

int const MAX_LOG_LINE_CHARS = 512;

/* Global array to hold log values to print when logging to STDOUT */
char **log_lines;
int log_current_element = 0;
int log_elements_allocated = 0;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

void nwipe_log( nwipe_log_t level, const char* format, ... )
{
/**
 *  Writes a message to the program log file.
 *
 */


	/* A time buffer. */
	time_t t;

	/* A pointer to the system time struct. */
	struct tm* p;

	/* Get the current time. */
	t = time( NULL );
	p = gmtime( &t );

	pthread_mutex_lock( &mutex1 );

	/* Increase the current log element pointer - we will write here */
	if (log_current_element == log_elements_allocated) {
		log_elements_allocated++;
		log_lines = (char **) realloc (log_lines, (log_elements_allocated) * sizeof(char *));
		log_lines[log_current_element] = malloc(MAX_LOG_LINE_CHARS * sizeof(char));
	}

	/* Position of writing to current log string */
	int line_current_pos = 0;
	
	/* Print the date. The rc script uses the same format. */
	line_current_pos = snprintf( log_lines[log_current_element], MAX_LOG_LINE_CHARS, "[%i/%02i/%02i %02i:%02i:%02i] nwipe: ", \
	  1900 + p->tm_year, 1 + p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec );

	switch( level )
	{

		case NWIPE_LOG_NONE:
			/* Do nothing. */
			break;

		case NWIPE_LOG_DEBUG:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "debug: " );
			break;

		case NWIPE_LOG_INFO:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "info: " );
			break;

		case NWIPE_LOG_NOTICE:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "notice: " );
			break;

		case NWIPE_LOG_WARNING:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "warning: " );
			break;

		case NWIPE_LOG_ERROR:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "error: " );
			break;

		case NWIPE_LOG_FATAL:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "fatal: " );
			break;

		case NWIPE_LOG_SANITY:
			/* TODO: Request that the user report the log. */
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "sanity: " );
			break;

		default:
			line_current_pos += snprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, "level %i: ", level );

	}

	/* The variable argument pointer. */
	va_list ap;

	/* Fetch the argument list. */
	va_start( ap, format );

	/* Print the event. */
	line_current_pos += vsnprintf( log_lines[log_current_element] + line_current_pos, MAX_LOG_LINE_CHARS, format, ap );

/*
	if( level >= NWIPE_LOG_WARNING )
	{
		vfprintf( stderr, format, ap );
	}
*/

	/* Release the argument list. */
	va_end( ap );

/*
	if( level >= NWIPE_LOG_WARNING )
	{
		fprintf( stderr, "\n" );
	}
*/

	/* A result buffer. */
	int r;

	/* The log file pointer. */
	FILE* fp;

	/* The log file descriptor. */
	int fd;


	if (nwipe_options.logfile[0] == '\0')
	{
		if (nwipe_options.nogui)
		{
			printf( "%s\n", log_lines[log_current_element] );
		}
		else
		{
			log_current_element++;
		}
	} else
	{
		/* Open the log file for appending. */
		fp = fopen( nwipe_options.logfile, "a" );

		if( fp == NULL )
		{
			fprintf( stderr, "nwipe_log: Unable to open '%s' for logging.\n", nwipe_options.logfile );
			return;
		}
		
		/* Get the file descriptor of the log file. */
		fd = fileno( fp );

		/* Block and lock. */
		r = flock( fd, LOCK_EX );

		if( r != 0 )
		{
			perror( "nwipe_log: flock:" );
			fprintf( stderr, "nwipe_log: Unable to lock '%s' for logging.\n", nwipe_options.logfile );
			return;
		}

		fprintf( fp, "%s\n", log_lines[log_current_element] );

		/* Unlock the file. */
		r = flock( fd, LOCK_UN );

		if( r != 0 )
		{
			perror( "nwipe_log: flock:" );
			fprintf( stderr, "Error: Unable to unlock '%s' after logging.\n", nwipe_options.logfile );
		}

		/* Close the stream. */
		r = fclose( fp );

		if( r != 0 )
		{
			perror( "nwipe_log: fclose:" );
			fprintf( stderr, "Error: Unable to close '%s' after logging.\n", nwipe_options.logfile );
		}
	}
	
	pthread_mutex_unlock( &mutex1 );
	

} /* nwipe_log */


void nwipe_perror( int nwipe_errno, const char* f, const char* s )
{
/**
 * Wrapper for perror().
 *
 * We may wish to tweak or squelch this later. 
 *
 */

	nwipe_log( NWIPE_LOG_ERROR, "%s: %s: %s", f, s, strerror( nwipe_errno ) );

} /* nwipe_perror */

/* eof */
