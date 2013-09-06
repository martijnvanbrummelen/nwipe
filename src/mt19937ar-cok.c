/* 
	This code is modified for use in nwipe.

   A C-program for MT19937, with initialization improved 2002/2/10.
   Coded by Takuji Nishimura and Makoto Matsumoto.
   This is a faster version by taking Shawn Cokus's optimization,
   Matthe Bellew's simplification, Isaku Wada's real version.

   Before using, initialize the state by using init_genrand(seed) 
   or init_by_array(init_key, key_length).

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.                          

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote 
        products derived from this software without specific prior written 
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


   Any feedback is very welcome.
   http://www.math.keio.ac.jp/matumoto/emt.html
   email: matumoto@math.keio.ac.jp
*/

#include <stdio.h>
#include "mt19937ar-cok.h"

/* initializes state[N] with a seed */
void init_genrand( twister_state_t* state, unsigned long s)
{
	int j;
	state->array[0]= s & 0xffffffffUL;
	for( j = 1; j < N; j++ )
	{
		state->array[j] = (1812433253UL * (state->array[j-1] ^ (state->array[j-1] >> 30)) + j); 
		state->array[j] &= 0xffffffffUL;  /* for >32 bit machines */
	}
	state->left = 1;
	state->initf = 1;
}


void twister_init( twister_state_t* state, unsigned long init_key[], unsigned long key_length )
{
    int i = 1;
	 int j = 0;
    int k = ( N > key_length ? N : key_length );

    init_genrand( state, 19650218UL );

    for( ; k; k-- )
	 {
		state->array[i] = (state->array[i] ^ ((state->array[i-1] ^ (state->array[i-1] >> 30)) * 1664525UL)) + init_key[j] + j;
		state->array[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
		++i;
		++j;

		if ( i >= N )
		{
			state->array[0] = state->array[N-1];
			i = 1;
		}

		if ( j >= key_length )
		{
			j = 0;
		}
    }

    for( k = N -1; k; k-- )
	 {
		state->array[i] = (state->array[i] ^ ((state->array[i-1] ^ (state->array[i-1] >> 30)) * 1566083941UL)) - i;
		state->array[i] &= 0xffffffffUL;
		++i;

		if ( i >= N )
		{
			state->array[0] = state->array[N-1];
			i = 1;
		}
    }

	state->array[0] = 0x80000000UL; /* MSB is 1; assuring non-zero initial array */ 
	state->left = 1;
	state->initf = 1;
}

static void next_state( twister_state_t* state )
{
    unsigned long *p = state->array;
    int j;

    if( state->initf == 0) { init_genrand( state, 5489UL ); }
    state->left = N;
    state->next = state->array;
    for( j = N - M + 1; --j; p++ ) { *p = p[M]   ^ TWIST(p[0], p[1]); }
    for( j = M; --j; p++ )         { *p = p[M-N] ^ TWIST(p[0], p[1]); }
    *p = p[M-N] ^ TWIST(p[0], state->array[0]);
}

/* generates a random number on [0,0xffffffff]-interval */
unsigned long twister_genrand_int32( twister_state_t* state )
{
    unsigned long y;

    if ( --state->left == 0 ) { next_state( state ); }
    y = *state->next++;

    /* Tempering */
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);

    return y;
}
