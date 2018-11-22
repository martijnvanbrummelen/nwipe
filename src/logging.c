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

#define _POSIX_SOURCE

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "logging.h"

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

	char **result;
	char *malloc_result;
	char message_buffer[MAX_LOG_LINE_CHARS * sizeof(char)];
	int chars_written;
	
	int message_buffer_length;

	/* A time buffer. */
	time_t t;

	/* A pointer to the system time struct. */
	struct tm* p;

	/* Get the current time. */
	t = time( NULL );
	p = localtime( &t );

	pthread_mutex_lock( &mutex1 );

	/* Position of writing to current log string */
	int line_current_pos = 0;
	
	/* Print the date. The rc script uses the same format. */
	  chars_written = snprintf( message_buffer, MAX_LOG_LINE_CHARS, "[%i/%02i/%02i %02i:%02i:%02i] nwipe: ", \
	  1900 + p->tm_year, 1 + p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec );
	  
	/* Has the end of the buffer been reached ?, snprintf returns the number of characters that would have been 
	 * written if MAX_LOG_LINE_CHARS had not been reached, it does not return the actual characters written in
	 * all circumstances, hence why we need to check whether it's greater than MAX_LOG_LINE_CHARS and if so set
	 * it to MAX_LOG_LINE_CHARS, preventing a buffer overrun further down this function.
	 */

	/* check if there was a complete failure to write this part of the message, in which case return */
	if ( chars_written < 0 )
	{
		fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
		pthread_mutex_unlock( &mutex1 );
		return;
	}
	else
	{
		if ( (line_current_pos + chars_written) > MAX_LOG_LINE_CHARS )
		{
			fprintf( stderr, "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n", MAX_LOG_LINE_CHARS );
			line_current_pos = MAX_LOG_LINE_CHARS;
		}
		else
		{
			line_current_pos += chars_written;
		}
	}

	if ( line_current_pos < MAX_LOG_LINE_CHARS )
	{
		switch( level )
		{

			case NWIPE_LOG_NONE:
				/* Do nothing. */
				break;

			case NWIPE_LOG_DEBUG:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "debug: " );
				break;

			case NWIPE_LOG_INFO:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "info: " );
				break;

			case NWIPE_LOG_NOTICE:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "notice: " );
				break;

			case NWIPE_LOG_WARNING:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "warning: " );
				break;

			case NWIPE_LOG_ERROR:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "error: " );
				break;

			case NWIPE_LOG_FATAL:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "fatal: " );
				break;

			case NWIPE_LOG_SANITY:
				/* TODO: Request that the user report the log. */
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "sanity: " );
				break;

			default:
				chars_written = snprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos, "level %i: ", level );
		}
	
		/* Has the end of the buffer been reached ?
		*/
		if ( chars_written < 0 )
		{
			fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
			pthread_mutex_unlock( &mutex1 );
			return;
		}
		else
		{
			if ( (line_current_pos + chars_written) > MAX_LOG_LINE_CHARS )
			{
				fprintf( stderr, "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n", MAX_LOG_LINE_CHARS );
				line_current_pos = MAX_LOG_LINE_CHARS;
			}
			else
			{
				line_current_pos += chars_written;
			}
		}
	}

	/* The variable argument pointer. */
	va_list ap;

	/* Fetch the argument list. */
	va_start( ap, format );
	
	
	/* Print the event. */
	if ( line_current_pos < MAX_LOG_LINE_CHARS )
	{
		chars_written = vsnprintf( message_buffer + line_current_pos, MAX_LOG_LINE_CHARS - line_current_pos -1, format, ap );
	
		if ( chars_written < 0 )
		{
			fprintf( stderr, "nwipe_log: snprintf error when writing log line to memory.\n" );
			pthread_mutex_unlock( &mutex1 );
			return;
		}
		else
		{
			if ( (line_current_pos + chars_written) > MAX_LOG_LINE_CHARS )
			{
				fprintf( stderr, "nwipe_log: Warning! The log line has been truncated as it exceeded %i characters\n", MAX_LOG_LINE_CHARS );
				line_current_pos = MAX_LOG_LINE_CHARS;
			}
			else
			{
				line_current_pos += chars_written;
			}
		}
	}

	/* Increase the current log element pointer - we will write here */
	if (log_current_element == log_elements_allocated) {
		log_elements_allocated++;
		result = realloc (log_lines, (log_elements_allocated) * sizeof(char *));
		if ( result == NULL )
		{
			fprintf( stderr, "nwipe_log: realloc failed when adding a log line.\n" );
			pthread_mutex_unlock( &mutex1 );
			return;
		}
		log_lines = result;

		message_buffer_length = strlen( message_buffer ) * sizeof(char);
		malloc_result = malloc((message_buffer_length + 1) * sizeof(char));
		if (malloc_result == NULL)
		{
			fprintf( stderr, "nwipe_log: malloc failed when adding a log line.\n" );
			pthread_mutex_unlock( &mutex1 );
			return;
		}
		log_lines[log_current_element] = malloc_result;
	}

	strcpy ( log_lines[log_current_element], message_buffer );

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
