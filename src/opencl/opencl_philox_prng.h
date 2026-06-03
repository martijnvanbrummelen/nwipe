#ifndef OPENCL_PHILOX_PRNG_H
#define OPENCL_PHILOX_PRNG_H

#include "../prng.h"

int nwipe_opencl_philox_prng_init( NWIPE_PRNG_INIT_SIGNATURE );
int nwipe_opencl_philox_prng_read( NWIPE_PRNG_READ_SIGNATURE );
int nwipe_opencl_philox_prng_available( void );

#endif /* OPENCL_PHILOX_PRNG_H */
