/*
------------------------------------------------------------------------------
isaac64.h: definitions for a random number generator
Bob Jenkins, 1996, Public Domain
------------------------------------------------------------------------------
*/
#ifndef ISAAC64
#define ISAAC64

#include "isaac_standard.h"

struct rand64ctx
{
 ub8 randrsl[RANDSIZ], randcnt;
 ub8 mm[RANDSIZ];
 ub8 aa, bb, cc;
};
typedef struct rand64ctx rand64ctx;

/*
------------------------------------------------------------------------------
 If (flag==TRUE), then use the contents of randrsl[0..255] as the seed.
------------------------------------------------------------------------------
*/
void randinit64(/*_ rand64ctx *r, word flag _*/);

void isaac64();


/*
------------------------------------------------------------------------------
 Call rand64() to retrieve a single 64-bit random value
------------------------------------------------------------------------------
*/
#define isaac64_rand() \
   (!(r)->randcnt-- ? \
     (isaac64(r), (r)->randcnt=RANDSIZ-1, (r)->randrsl[(r)->>randcnt]) : \
     (r)->randrsl[(r)->randcnt])

#endif  /* ISAAC64 */

