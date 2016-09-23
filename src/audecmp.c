/*
DFPWM1a (Dynamic Filter Pulse Width Modulation) codec - C Implementation
by Ben "GreaseMonkey" Russell, 2012, 2016
Public Domain

Decompression Component
*/

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <immintrin.h>

#ifndef CONST_PREC
#define CONST_PREC 10
#endif
#ifndef CONST_POSTFILT
#define CONST_POSTFILT 140
#endif

// note, len denotes how many compressed bytes there are (uncompressed bytes / 8).

void au_decompress(int *fq, int *q, int *s, int *lt, int fs, int len, int8_t *outbuf, uint8_t *inbuf)
{
	int i,j;
	uint8_t d;
	for(i = 0; i < len; i++)
	{
		// get bits
		d = *(inbuf++);

		for(j = 0; j < 8; j++)
		{
			// set target
			int t = ((d&1) ? 127 : -128);
			d >>= 1;

			// adjust charge
			int nq = *q + ((*s * (t-*q) + (1<<(CONST_PREC-1)))>>CONST_PREC);
			if(nq == *q && nq != t)
				*q += (t == 127 ? 1 : -1);
			int lq = *q;
			*q = nq;

			// adjust strength
			int st = (t != *lt ? 0 : (1<<CONST_PREC)-1);
			int ns = *s;
			if(ns != st)
				ns += (st != 0 ? 1 : -1);
#if CONST_PREC > 8
			if(ns < 1+(1<<(CONST_PREC-8))) ns = 1+(1<<(CONST_PREC-8));
#endif
			*s = ns;

			// FILTER: perform antijerk
			int ov = (t != *lt ? (nq+lq)>>1 : nq);

			// FILTER: perform LPF
			*fq += ((fs*(ov-*fq) + 0x80)>>8);
			ov = *fq;

			// output sample
			*(outbuf++) = ov;

			*lt = t;
		}
	}
}

