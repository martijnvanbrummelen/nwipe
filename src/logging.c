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

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include "stdio.h"
#include "stdlib.h"
#include "stdarg.h"
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
   int r; /* result buffer */

	/* A time buffer. */
	time_t t;

	/* A pointer to the system time struct. */
	struct tm* p;
   r = pthread_mutex_lock( &mutex1 );
	if ( r !=0 )
   {
      fprintf( stderr, "nwipe_log: pthread_mutex_lock failed. Code %i \n", r );
      return;
   }

	/* Get the current time. */
	t = time( NULL );
	p = localtime( &t );

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
		r = pthread_mutex_unlock( &mutex1 );
      if ( r !=0 )
      {
         fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
         return;
      }
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
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            return;
         }
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
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            va_end( ap );
            return;
         }
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

	/* Increase the current log element pointer - we will write here, deallocation is done in cleanup() in nwipe.c */
	if (log_current_element == log_elements_allocated) {
		log_elements_allocated++;
		result = realloc (log_lines, (log_elements_allocated) * sizeof(char *));
		if ( result == NULL )
		{
			fprintf( stderr, "nwipe_log: realloc failed when adding a log line.\n" );
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            va_end( ap );
            return;
         }
		}
		log_lines = result;

		/* Allocate memory for storing a single log message, deallocation is done in cleanup() in nwipe.c */
		message_buffer_length = strlen( message_buffer ) * sizeof(char);
		malloc_result = malloc((message_buffer_length + 1) * sizeof(char));
		if (malloc_result == NULL)
		{
			fprintf( stderr, "nwipe_log: malloc failed when adding a log line.\n" );
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            va_end( ap );
            return;
         }
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
	} else
	{
		/* Open the log file for appending. */
		fp = fopen( nwipe_options.logfile, "a" );

		if( fp == NULL )
		{
			fprintf( stderr, "nwipe_log: Unable to open '%s' for logging.\n", nwipe_options.logfile );
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
            return;
         }
		}
		
		/* Get the file descriptor of the log file. */
		fd = fileno( fp );

		/* Block and lock. */
		r = flock( fd, LOCK_EX );

		if( r != 0 )
		{
			perror( "nwipe_log: flock:" );
			fprintf( stderr, "nwipe_log: Unable to lock '%s' for logging.\n", nwipe_options.logfile );
			r = pthread_mutex_unlock( &mutex1 );
         if ( r !=0 )
         {
            fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );

            /* Unlock the file. */
            r = flock( fd, LOCK_UN );
            fclose( fp );
            return;
         }
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
	
	log_current_element++;
	
	r = pthread_mutex_unlock( &mutex1 );
   if ( r !=0 )
   {
      fprintf( stderr, "nwipe_log: pthread_mutex_unlock failed. Code %i \n", r );
   }
   return;
	

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

int nwipe_log_sysinfo()
{
   FILE *fp;
   char path[256];
   char cmd[50];
   int len;
   int r;         /* A result buffer. */

   /* Remove or add keywords to be searched, depending on what information is to
      be logged, making sure the last entry in the array is a NULL string. To remove
      an entry simply comment out the keyword with // */
   char dmidecode_keywords[][24] = {
   "bios-version",
   "bios-release-date",
   "system-manufacturer",
   "system-product-name",
   "system-version",
   "system-serial-number",
   "system-uuid",
   "baseboard-manufacturer",
   "baseboard-product-name",
   "baseboard-version",
   "baseboard-serial-number",
   "baseboard-asset-tag",
   "chassis-manufacturer",
   "chassis-type",
   "chassis-version",
   "chassis-serial-number",
   "chassis-asset-tag",
   "processor-family",
   "processor-manufacturer",
   "processor-version",
   "processor-frequency",
   "" //terminates the keyword array. DO NOT REMOVE
   };
   unsigned int keywords_idx;

   keywords_idx = 0;

   /* Run the dmidecode command to retrieve each dmidecode keyword, one at a time */
   while ( dmidecode_keywords[keywords_idx][0] != 0 )
   {
      sprintf(cmd,"dmidecode -s %s", &dmidecode_keywords[keywords_idx][0] );
      fp = popen(cmd, "r");
      if (fp == NULL ) {
         nwipe_log( NWIPE_LOG_INFO, "nwipe_log_sysinfo: Failed to create stream to %s", cmd );
         return 1;
      }
      /* Read the output a line at a time - output it. */
      while (fgets(path, sizeof(path)-1, fp) != NULL) 
      {
         /* Remove any trailing return from the string, as nwipe_log automatically adds a return */
         len = strlen(path);
         if( path[len-1] == '\n' ) {
            path[len-1] = 0;
         }
         nwipe_log( NWIPE_LOG_INFO, "%s = %s", &dmidecode_keywords[keywords_idx][0], path );
      }
      /* close */
      r = pclose(fp);
      if( r > 0 )
      {
         nwipe_log( NWIPE_LOG_INFO, "nwipe_log_sysinfo(): dmidecode failed, \"%s\" exit status = %u", cmd, WEXITSTATUS( r ));
         return 1;
      }
      keywords_idx++;
   }
   return 0;
}


/* eof */
