/*
 * mt19937ar-cok.h:  The Mersenne Twister PRNG implementation for nwipe.
 *
 */

#ifndef MT19937AR_H_
#define MT19937AR_H_

/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UMASK 0x80000000UL /* most significant w-r bits */
#define LMASK 0x7fffffffUL /* least significant r bits */
#define MIXBITS(u,v) ( ((u) & UMASK) | ((v) & LMASK) )
#define TWIST(u,v) ((MIXBITS(u,v) >> 1) ^ ((v)&1UL ? MATRIX_A : 0UL))

typedef struct twister_state_t_
{
	unsigned long array[N];
	int left;
	int initf;
	unsigned long *next;
} twister_state_t; 

/* Initialize the MT state. ( 0 < key_length <= 624 ). */
void twister_init( twister_state_t* state, unsigned long init_key[], unsigned long key_length);

/* Generate a random integer on the [0,0xffffffff] interval. */
unsigned long twister_genrand_int32( twister_state_t* state );

#endif /* MT19937AR_H_ */
