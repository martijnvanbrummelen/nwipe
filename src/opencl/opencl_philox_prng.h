#ifndef OPENCL_PHILOX_PRNG_H
#define OPENCL_PHILOX_PRNG_H

#include "../prng.h"

int nwipe_opencl_philox_prng_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_opencl_philox_prng_read( NWIPE_PRNG_READ_SIGNATURE );
int nwipe_opencl_philox_prng_available( void );
int nwipe_opencl_philox_set_device_selector( const char* selector );
const char* nwipe_opencl_philox_prng_status( void );
void nwipe_opencl_philox_prng_free( void** state );

#endif /* OPENCL_PHILOX_PRNG_H */
