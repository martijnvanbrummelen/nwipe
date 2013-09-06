/*
 *  pass.c: Routines that read and write patterns to block devices.
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

/* Why is this needed? Segfaults without it */
#include <netinet/in.h>

#include "nwipe.h"
#include "context.h"
#include "method.h"
#include "prng.h"
#include "options.h"
#include "pass.h"
#include "logging.h"


int nwipe_random_verify( nwipe_context_t* c )
{
/**
 * Verifies that a random pass was correctly written to the device.
 *
 */

	/* The result holder. */
	int r;

	/* The IO size. */
	size_t blocksize;

	/* The result buffer for calls to lseek. */
	loff_t offset;

	/* The input buffer. */
	char* b;

	/* The pattern buffer that is used to check the input buffer. */
	char* d;

	/* The number of bytes remaining in the pass. */
	u64 z = c->device_size;


	if( c->prng_seed.s == NULL )
	{
		nwipe_log( NWIPE_LOG_SANITY, "Null seed pointer." );
		return -1;
	}

	if( c->prng_seed.length <= 0 )
	{
		nwipe_log( NWIPE_LOG_SANITY, "The entropy length member is %i.", c->prng_seed.length );
		return -1;
	}

	/* Create the input buffer. */
	b = malloc( c->device_stat.st_blksize );

	/* Check the memory allocation. */
	if( ! b )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the input buffer." );
		return -1;
	}

	/* Create the pattern buffer */
	d = malloc( c->device_stat.st_blksize  );

	/* Check the memory allocation. */
	if( ! d )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the pattern buffer." );
		return -1;
	}

	/* Reset the file pointer. */
	offset = lseek( c->device_fd, 0, SEEK_SET );

	/* Reset the pass byte counter. */
	c->pass_done = 0;

	if( offset == (loff_t)-1 )
	{
		nwipe_perror( errno, __FUNCTION__, "lseek" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
		return -1;
	}

	if( offset != 0 )
	{
		/* This is system insanity. */
		nwipe_log( NWIPE_LOG_SANITY, "lseek() returned a bogus offset on '%s'.", c->device_name );
		return -1;
	}

	/* Tell our parent that we are syncing the device. */
	c->sync_status = 1;

	/* Sync the device. */
	r = fdatasync( c->device_fd );

	/* Tell our parent that we have finished syncing the device. */
	c->sync_status = 0;

	if( r != 0 )
	{
		/* FIXME: Is there a better way to handle this? */
		nwipe_perror( errno, __FUNCTION__, "fdatasync" );
		nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
	}

	/* Reseed the PRNG. */
	c->prng->init( &c->prng_state, &c->prng_seed );

	while( z > 0 )
	{
		if( c->device_stat.st_blksize <= z )
		{
			blocksize = c->device_stat.st_blksize;
		}
		else
		{
			/* This is a seatbelt for buggy drivers and programming errors because */
			/* the device size should always be an even multiple of its blocksize. */
			blocksize = z;
			nwipe_log( NWIPE_LOG_WARNING,
			  "%s: The size of '%s' is not a multiple of its block size %i.",
			  __FUNCTION__, c->device_name, c->device_stat.st_blksize );
		}

		/* Fill the output buffer with the random pattern. */
		c->prng->read( &c->prng_state, d, blocksize );

		/* Read the buffer in from the device. */
		r = read( c->device_fd, b, blocksize );

		/* Check the result. */
		if( r < 0 )
		{
			nwipe_perror( errno, __FUNCTION__, "read" );
			nwipe_log( NWIPE_LOG_ERROR, "Unable to read from '%s'.", c->device_name );
			return -1;
		}

		/* Check for a partial read. */
		if( r != blocksize )
		{
			/* TODO: Handle a partial read. */

			/* The number of bytes that were not read. */
			int s = blocksize - r;

			nwipe_log( NWIPE_LOG_WARNING, "%s: Partial read from '%s', %i bytes short.", __FUNCTION__, c->device_name, s );
			
			/* Increment the error count. */
			c->verify_errors += 1;

			/* Bump the file pointer to the next block. */
			offset = lseek( c->device_fd, s, SEEK_CUR );

			if( offset == (loff_t)-1 )
			{
				nwipe_perror( errno, __FUNCTION__, "lseek" );
				nwipe_log( NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial read.", c->device_name );
				return -1;
			}

		} /* partial read */

		/* Compare buffer contents. */
		if( memcmp( b, d, blocksize ) != 0 ) { c->verify_errors += 1; }

		/* Decrement the bytes remaining in this pass. */
		z -= r;

		/* Increment the total progress counters. */
		c->pass_done += r;
		c->round_done += r;

		pthread_testcancel();
		
	} /* while bytes remaining */

	/* Release the buffers. */
	free( b );
	free( d );

	/* We're done. */
	return 0;

} /* nwipe_random_verify */



int nwipe_random_pass( NWIPE_METHOD_SIGNATURE )
{
/**
 * Writes a random pattern to the device.
 *
 */

	/* The result holder. */
	int r;

	/* The IO size. */
	size_t blocksize;

	/* The result buffer for calls to lseek. */
	loff_t offset;

	/* The output buffer. */
	char* b;

	/* The number of bytes remaining in the pass. */
	u64 z = c->device_size;


	if( c->prng_seed.s == NULL )
	{
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: Null seed pointer." );
		return -1;
	}

	if( c->prng_seed.length <= 0 )
	{
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: The entropy length member is %i.", c->prng_seed.length );
		return -1;
	}


	/* Create the output buffer. */
	b = malloc( c->device_stat.st_blksize );

	/* Check the memory allocation. */
	if( ! b )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the output buffer." );
		return -1;
	}

	/* Seed the PRNG. */
	c->prng->init( &c->prng_state, &c->prng_seed );

	/* Reset the file pointer. */
	offset = lseek( c->device_fd, 0, SEEK_SET );

	/* Reset the pass byte counter. */
	c->pass_done = 0;

	if( offset == (loff_t)-1 )
	{
		nwipe_perror( errno, __FUNCTION__, "lseek" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
		return -1;
	}

	if( offset != 0 )
	{
		/* This is system insanity. */
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: lseek() returned a bogus offset on '%s'.", c->device_name );
		return -1;
	}


	while( z > 0 )
	{
		if( c->device_stat.st_blksize <= z )
		{
			blocksize = c->device_stat.st_blksize;
		}
		else
		{
			/* This is a seatbelt for buggy drivers and programming errors because */
			/* the device size should always be an even multiple of its blocksize. */
			blocksize = z;
			nwipe_log( NWIPE_LOG_WARNING,
			  "%s: The size of '%s' is not a multiple of its block size %i.",
			  __FUNCTION__, c->device_name, c->device_stat.st_blksize );
		}

		/* Fill the output buffer with the random pattern. */
		c->prng->read( &c->prng_state, b, blocksize );

		/* Write the next block out to the device. */
		r = write( c->device_fd, b, blocksize );

		/* Check the result for a fatal error. */
		if( r < 0 )
		{
			nwipe_perror( errno, __FUNCTION__, "write" );
			nwipe_log( NWIPE_LOG_FATAL, "Unable to read from '%s'.", c->device_name );
			return -1;
		}

		/* Check for a partial write. */
		if( r != blocksize )
		{
			/* TODO: Handle a partial write. */

			/* The number of bytes that were not written. */
			int s = blocksize - r;
			
			/* Increment the error count by the number of bytes that were not written. */
			c->pass_errors += s;

			nwipe_log( NWIPE_LOG_WARNING, "Partial write on '%s', %i bytes short.", c->device_name, s );

			/* Bump the file pointer to the next block. */
			offset = lseek( c->device_fd, s, SEEK_CUR );

			if( offset == (loff_t)-1 )
			{
				nwipe_perror( errno, __FUNCTION__, "lseek" );
				nwipe_log( NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial write.", c->device_name );
				return -1;
			}

		} /* partial write */

		/* Decrement the bytes remaining in this pass. */
		z -= r;

		/* Increment the total progress counters. */
		c->pass_done += r;
		c->round_done += r;

		pthread_testcancel();

	} /* remaining bytes */

	/* Release the output buffer. */
	free( b );

	/* Tell our parent that we are syncing the device. */
	c->sync_status = 1;

	/* Sync the device. */
	r = fdatasync( c->device_fd );

	/* Tell our parent that we have finished syncing the device. */
	c->sync_status = 0;

	if( r != 0 )
	{
		/* FIXME: Is there a better way to handle this? */
		nwipe_perror( errno, __FUNCTION__, "fdatasync" );
		nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
	}

	/* We're done. */
	return 0;	

} /* nwipe_random_pass */



int nwipe_static_verify( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
/**
 * Verifies that a static pass was correctly written to the device.
 *
 */

	/* The result holder. */
	int r;

	/* The IO size. */
	size_t blocksize;

	/* The result buffer for calls to lseek. */
	loff_t offset;

	/* The input buffer. */
	char* b;

	/* The pattern buffer that is used to check the input buffer. */
	char* d;

	/* A pointer into the pattern buffer. */
	char* q;

	/* The pattern buffer window offset. */
	int w = 0;

	/* The number of bytes remaining in the pass. */
	u64 z = c->device_size;

	if( pattern == NULL )
	{
		/* Caught insanity. */	
		nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: Null entropy pointer." );
		return -1;
	}

	if( pattern->length <= 0 )
	{
		/* Caught insanity. */	
		nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: The pattern length member is %i.", pattern->length );
		return -1;
	}

	/* Create the input buffer. */
	b = malloc( c->device_stat.st_blksize  );

	/* Check the memory allocation. */
	if( ! b )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the input buffer." );
		return -1;
	}

	/* Create the pattern buffer */
	d = malloc( c->device_stat.st_blksize  + pattern->length * 2 );

	/* Check the memory allocation. */
	if( ! d )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the pattern buffer." );
		return -1;
	}

	for( q = d ; q < d + c->device_stat.st_blksize  + pattern->length ; q += pattern->length )
	{
		/* Fill the pattern buffer with the pattern. */
		memcpy( q, pattern->s, pattern->length );
	}

	/* Tell our parent that we are syncing the device. */
	c->sync_status = 1;

	/* Sync the device. */
	r = fdatasync( c->device_fd );

	/* Tell our parent that we have finished syncing the device. */
	c->sync_status = 0;

	if( r != 0 )
	{
		/* FIXME: Is there a better way to handle this? */
		nwipe_perror( errno, __FUNCTION__, "fdatasync" );
		nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
	}


	/* Reset the file pointer. */
	offset = lseek( c->device_fd, 0, SEEK_SET );

	/* Reset the pass byte counter. */
	c->pass_done = 0;

	if( offset == (loff_t)-1 )
	{
		nwipe_perror( errno, __FUNCTION__, "lseek" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
		return -1;
	}

	if( offset != 0 )
	{
		/* This is system insanity. */
		nwipe_log( NWIPE_LOG_SANITY, "nwipe_static_verify: lseek() returned a bogus offset on '%s'.", c->device_name );
		return -1;
	}


	while( z > 0 )
	{
		if( c->device_stat.st_blksize <= z )
		{
			blocksize = c->device_stat.st_blksize ;
		}
		else
		{
			/* This is a seatbelt for buggy drivers and programming errors because */
			/* the device size should always be an even multiple of its blocksize. */
			blocksize = z;
			nwipe_log( NWIPE_LOG_WARNING,
			  "%s: The size of '%s' is not a multiple of its block size %i.",
			  __FUNCTION__, c->device_name, c->device_stat.st_blksize );
		}

		/* Fill the output buffer with the random pattern. */
		/* Read the buffer in from the device. */
		r = read( c->device_fd, b, blocksize );

		/* Check the result. */
		if( r < 0 )
		{
			nwipe_perror( errno, __FUNCTION__, "read" );
			nwipe_log( NWIPE_LOG_ERROR, "Unable to read from '%s'.", c->device_name );
			return -1;
		}

		/* Check for a partial read. */
		if( r == blocksize )
		{
			/* Check every byte in the buffer. */
			if( memcmp( b, &d[w], r ) != 0 ) { c->verify_errors += 1; }
		}
		else
		{
			/* The number of bytes that were not read. */
			int s = blocksize - r;

			/* TODO: Handle a partial read. */

			/* Increment the error count. */
			c->verify_errors += 1;
			
			nwipe_log( NWIPE_LOG_WARNING, "Partial read on '%s', %i bytes short.", c->device_name, s );

			/* Bump the file pointer to the next block. */
			offset = lseek( c->device_fd, s, SEEK_CUR );

			if( offset == (loff_t)-1 )
			{
				nwipe_perror( errno, __FUNCTION__, "lseek" );
				nwipe_log( NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial read.", c->device_name );
				return -1;
			}

		} /* partial read */

		/* Adjust the window. */
		w = ( c->device_stat.st_blksize  + w ) % pattern->length;

		/* Intuition check: 
		 *   If the pattern length evenly divides the block size
		 *   then ( w == 0 ) always.
		 */

		/* Decrement the bytes remaining in this pass. */
		z -= r;

		/* Increment the total progress counters. */
		c->pass_done += r;
		c->round_done += r;

		pthread_testcancel();

	} /* while bytes remaining */

	/* Release the buffers. */
	free( b );
	free( d );

	/* We're done. */
	return 0;
		
} /* nwipe_static_verify */



int nwipe_static_pass( NWIPE_METHOD_SIGNATURE, nwipe_pattern_t* pattern )
{
/**
 * Writes a static pattern to the device.
 *
 */

	/* The result holder. */
	int r;

	/* The IO size. */
	size_t blocksize;

	/* The result buffer for calls to lseek. */
	loff_t offset;

	/* The output buffer. */
	char* b;

	/* A pointer into the output buffer. */
	char* p;

	/* The output buffer window offset. */
	int w = 0;

	/* The number of bytes remaining in the pass. */
	u64 z = c->device_size;

	if( pattern == NULL )
	{
		/* Caught insanity. */	
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: Null pattern pointer." );
		return -1;
	}

	if( pattern->length <= 0 )
	{
		/* Caught insanity. */	
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: The pattern length member is %i.", pattern->length );
		return -1;
	}

	/* Create the output buffer. */
	b = malloc( c->device_stat.st_blksize  + pattern->length * 2 );

	/* Check the memory allocation. */
	if( ! b )
	{
		nwipe_perror( errno, __FUNCTION__, "malloc" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to allocate memory for the pattern buffer." );
		return -1;
	}

	for( p = b ; p < b + c->device_stat.st_blksize  + pattern->length ; p += pattern->length )
	{
		/* Fill the output buffer with the pattern. */
		memcpy( p, pattern->s, pattern->length ); 
	}
///
	/* Reset the file pointer. */
	offset = lseek( c->device_fd, 0, SEEK_SET );

	/* Reset the pass byte counter. */
	c->pass_done = 0;

	if( offset == (loff_t)-1 )
	{
		nwipe_perror( errno, __FUNCTION__, "lseek" );
		nwipe_log( NWIPE_LOG_FATAL, "Unable to reset the '%s' file offset.", c->device_name );
		return -1;
	}

	if( offset != 0 )
	{
		/* This is system insanity. */
		nwipe_log( NWIPE_LOG_SANITY, "__FUNCTION__: lseek() returned a bogus offset on '%s'.", c->device_name );
		return -1;
	}


	while( z > 0 )
	{
		if( c->device_stat.st_blksize <= z )
		{
			blocksize = c->device_stat.st_blksize ;
		}
		else
		{
			/* This is a seatbelt for buggy drivers and programming errors because */
			/* the device size should always be an even multiple of its blocksize. */
			blocksize = z;
			nwipe_log( NWIPE_LOG_WARNING,
			  "%s: The size of '%s' is not a multiple of its block size %i.",
			  __FUNCTION__, c->device_name, c->device_stat.st_blksize );
		}

		/* Fill the output buffer with the random pattern. */
		/* Write the next block out to the device. */
		r = write( c->device_fd, &b[w], blocksize );

		/* Check the result for a fatal error. */
		if( r < 0 )
		{
			nwipe_perror( errno, __FUNCTION__, "write" );
			nwipe_log( NWIPE_LOG_FATAL, "Unable to write to '%s'.", c->device_name );
			return -1;
		}

		/* Check for a partial write. */
		if( r != blocksize )
		{
			/* TODO: Handle a partial write. */

			/* The number of bytes that were not written. */
			int s = blocksize - r;
			
			/* Increment the error count. */
			c->pass_errors += s;

			nwipe_log( NWIPE_LOG_WARNING, "Partial write on '%s', %i bytes short.", c->device_name, s );

			/* Bump the file pointer to the next block. */
			offset = lseek( c->device_fd, s, SEEK_CUR );

			if( offset == (loff_t)-1 )
			{
				nwipe_perror( errno, __FUNCTION__, "lseek" );
				nwipe_log( NWIPE_LOG_ERROR, "Unable to bump the '%s' file offset after a partial write.", c->device_name );
				return -1;
			}

		} /* partial write */


		/* Adjust the window. */
		w = ( c->device_stat.st_blksize  + w ) % pattern->length;

		/* Intuition check: 
		 *
		 *   If the pattern length evenly divides the block size
		 *   then ( w == 0 ) always.
		 */

		/* Decrement the bytes remaining in this pass. */
		z -= r;

		/* Increment the total progress counterr. */
		c->pass_done += r;
		c->round_done += r;

		pthread_testcancel();

	} /* remaining bytes */

	/* Tell our parent that we are syncing the device. */
	c->sync_status = 1;

	/* Sync the device. */
	r = fdatasync( c->device_fd );

	/* Tell our parent that we have finished syncing the device. */
	c->sync_status = 0;

	if( r != 0 )
	{
		/* FIXME: Is there a better way to handle this? */
		nwipe_perror( errno, __FUNCTION__, "fdatasync" );
		nwipe_log( NWIPE_LOG_WARNING, "Buffer flush failure on '%s'.", c->device_name );
	}

	/* Release the output buffer. */
	free( b );
	
	/* We're done. */
	return 0;	

} /* nwipe_static_pass */

/* eof */

