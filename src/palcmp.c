#include "common.h"

__m128i pal_list_l[32];
__m128i pal_list_cb[32];
__m128i pal_list_cr[32];
void pal_to_rgb(int pal, int *restrict r, int *restrict g, int *restrict b)
{
#ifdef PIXEL15
	assert(pal >= 0 && pal <= 32767);

	//*r = ((pal&31)*255)/31;
	//*g = (((pal>>5)&31)*255)/31;
	//*b = (((pal>>10)&31)*255)/31;

	// much easier to calc with :)
	*r = ((pal&31)<<3)+4;
	*g = (((pal>>5)&31)<<3)+4;
	*b = (((pal>>10)&31)<<3)+4;
#else
	assert(pal >= 0 && pal <= 255);

	if(pal < 16)
	{
		*r = *g = *b = ((pal+1)*255)/(GREY_RAMP-1);
		//fprintf(stderr, "%i %02X\n", pal, *r);

	} else {
		pal -= 16;
		*g = ((pal%8)*255)/7;
		*r = (((pal/8)%6)*255)/5;
		*b = (((pal/8)/6)*255)/4;

	}
#endif

	assert(*r >= 0 && *r <= 255);
	assert(*g >= 0 && *g <= 255);
	assert(*b >= 0 && *b <= 255);
}

#ifndef PIXEL15
void init_pal_list(void)
{
	int i, j;

	for(i = 0; i < 32; i++)
	{
		uint16_t vl[8];
		uint16_t vcb[8];
		uint16_t vcr[8];

		for(j = 0; j < 8; j++)
		{
			int r, g, b;
			int l, cb, cr;

			pal_to_rgb(i*8+j, &r, &g, &b);
			to_ycbcr(r, g, b, &l, &cb, &cr);

			vl[j] = l;
			vcb[j] = cb;
			vcr[j] = cr;
		}

		pal_list_l[i]  = _mm_load_si128((__m128i *)vl);
		pal_list_cb[i] = _mm_load_si128((__m128i *)vcb);
		pal_list_cr[i] = _mm_load_si128((__m128i *)vcr);
	}

}
#endif

#if 0
int rgb_to_pal_exact_refimp(int *r, int *g, int *b)
{
	int i;

	// check if r,g,b within bounds
	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);

	// convert base RGB to YCbCr
	int l, cb, cr;
	to_ycbcr(*r, *g, *b, &l, &cb, &cr);

	// compare each
	int bdiff = -1;
	int bbest = 0;
	for(i = 0; i < 256; i++)
	{
		// pick appropriate r,g,b
		int xr, xg, xb;
		pal_to_rgb(i, &xr, &xg, &xb);

		assert(xr <= 255);
		assert(xg <= 255);
		assert(xb <= 255);

		// convert
		int xl, xcb, xcr;
		to_ycbcr(xr, xg, xb, &xl, &xcb, &xcr);

		// compare
		xl  -= l;
		xcb -= cb;
		xcr -= cr;
		xcr <<= 1;
		int xdiff = PAL_WTL*xl*xl + PAL_WTC*(xcb*xcb + xcr*xcr);

		// check if nearest is best
		if(bdiff == -1 || xdiff < bdiff)
		{
			bbest = i;
			bdiff = xdiff;
		}
	}

	pal_to_rgb(bbest, r, g, b);
	return bbest;
}
#endif

#ifndef PIXEL15
int rgb_to_pal_exact(int *r, int *g, int *b)
{
	int i;

	// check if r,g,b within bounds
	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);

	// convert base RGB to YCbCr
	__m128i l, cb, cr;
	/*
	to_ycbcr_x8(
		_mm_set1_epi16(*r),
		_mm_set1_epi16(*g),
		_mm_set1_epi16(*b),
		&l, &cb, &cr);
	*/
	int il, icb, icr;
	to_ycbcr(*r, *g, *b, &il, &icb, &icr);
	l  = _mm_set1_epi16(il);
	cb = _mm_set1_epi16(icb);
	cr = _mm_set1_epi16(icr);

	// compare each
	__m128i bdiff = _mm_setzero_si128();
	__m128i bbest = _mm_setzero_si128();
	__m128i iv;
	const __m128i ivdiff = _mm_set1_epi16(8);
	//for(i = 0; i < 256; i++)
	for(i = 0, iv = _mm_setr_epi16(0,1,2,3,4,5,6,7); i < 256; i += 8, iv = _mm_add_epi16(iv, ivdiff))
	{
		// pick appropriate r,g,b
		__m128i xl  =  pal_list_l[i>>3];
		__m128i xcb = pal_list_cb[i>>3];
		__m128i xcr = pal_list_cr[i>>3];

		// compare
		const __m128i c0x8000 = _mm_set1_epi16(0x8000);
		xl  = _mm_sub_epi16(xl,  l);
		xcb = _mm_sub_epi16(xcb, cb);
		xcr = _mm_sub_epi16(xcr, cr);
		xl  = _mm_mullo_epi16(xl,  xl);
		xcb = _mm_mullo_epi16(xcb, xcb);
		xcr = _mm_mullo_epi16(xcr, xcr);
		xcr = _mm_adds_epu16(xcr, xcr);
		xcr = _mm_adds_epu16(xcr, xcr);

		// XXX: assumes PAL_WTL and PAL_WTC are both 1
		// RATIONALE FOR SATURATION: the final result for the diff won't saturate
		// mostly left as conjecture but you can (dis?)prove it if you want
		__m128i xdiff = _mm_adds_epu16(xl, _mm_adds_epu16(xcb, xcr));
		xdiff = _mm_sub_epi16(xdiff, c0x8000);

		// check if nearest is best
		if(i == 0)
		{
			bbest = iv;
			bdiff = xdiff;
		} else {
			// xdiff < bdiff
			__m128i mask = _mm_cmpgt_epi16(bdiff, xdiff);
			__m128i delta_best = _mm_and_si128(_mm_xor_si128(bbest, iv), mask);
			__m128i delta_diff = _mm_and_si128(_mm_xor_si128(bdiff, xdiff), mask);
			bbest = _mm_xor_si128(bbest, delta_best);
			bdiff = _mm_xor_si128(bdiff, delta_diff);
		}
	}

	// find the very best, like noone ever was
	int16_t vbest[8];
	int16_t vdiff[8];
	_mm_store_si128((__m128i *)vbest, bbest);
	_mm_store_si128((__m128i *)vdiff, bdiff);
	int ibest = vbest[0], idiff = vdiff[0];
	for(i = 1; i < 8; i++)
	{
		//fprintf(stderr, "%i %i %i\n", i, vbest[i], vdiff[i]);

		// the second comparison is to ensure we have
		// EXACTLY THE SAME OUTPUT as the reference implementation.
		// seriously, enable that assert down there.
		if(vdiff[i] < idiff || (vdiff[i] == idiff && vbest[i] < ibest))
		{
			idiff = vdiff[i];
			ibest = vbest[i];
		}
	}

	//if(ibest >= 8 || ibest < 0) fprintf(stderr, "%04X %04X\n", idiff, ibest);
	//fprintf(stderr, "%i #%02X%02X%02X\n", ibest, *r, *g, *b);
	//assert(ibest == rgb_to_pal_exact_refimp(r, g, b)); // <-- that assert
	pal_to_rgb(ibest, r, g, b);
	return ibest;

}
#endif

int rgb_to_pal_approx(int *r, int *g, int *b)
{
	int i;

	// check if r,g,b within bounds
	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);

#ifdef PIXEL15
	// get lower + upper bounds on each
	int r0 = (*r*31)>>8;
	int g0 = (*g*31)>>8;
	int b0 = (*b*31)>>8;

	int r1 = r0+1;
	int g1 = g0+1;
	int b1 = b0+1;

	// check if bounds are correct
	assert(r0 >= 0); assert(g0 >= 0); assert(b0 >= 0);
	assert(r1 < 32); assert(g1 < 32); assert(b1 < 32);
#else
	// get lower + upper bounds on each
	int r0 = (*r*5)>>8;
	int g0 = (*g*7)>>8;
	int b0 = (*b*4)>>8;

	int r1 = r0+1;
	int g1 = g0+1;
	int b1 = b0+1;

	// check if bounds are correct
	assert(r0 >= 0); assert(g0 >= 0); assert(b0 >= 0);
	assert(r1 <  6); assert(g1 <  8); assert(b1 <  5);
#endif

	// convert base RGB to YCbCr
	int l, cb, cr;
	to_ycbcr(*r, *g, *b, &l, &cb, &cr);

#ifndef PIXEL15
	// get grey palette value
	int grey_pal = (l*(GREY_RAMP-1) + 128)>>8;
	int grey_l_base = (grey_pal*(GREY_RAMP-1))/255;
	int grey_l = grey_l_base - l;
	cb -= 128;
	cr -= 128;
	int grey_diff = PAL_WTL*grey_l*grey_l + PAL_WTC_GREY*(cb*cb + cr*cr);
	int bdiff_grey = -1;
#endif

	// compare each
	int bdiff = -1;
	int bbest = 0;
	for(i = 0; i < 8; i++)
	{
		// pick appropriate r,g,b
#ifdef PIXEL15
		int xr = (((i&1) ? r1 : r0)*255)/32;
		int xg = (((i&2) ? g1 : g0)*255)/32;
		int xb = (((i&4) ? b1 : b0)*255)/32;
#else
		int xr = (((i&1) ? r1 : r0)*255)/6;
		int xg = (((i&2) ? g1 : g0)*255)/8;
		int xb = (((i&4) ? b1 : b0)*255)/5;
#endif

		assert(xr <= 255);
		assert(xg <= 255);
		assert(xb <= 255);

		// convert
		int xl, xcb, xcr;
		to_ycbcr(xr, xg, xb, &xl, &xcb, &xcr);

		// compare
		xl  -= l  + 128;
		xcb -= cb + 128;
		xcr -= cr + 128;
		int xdiff = PAL_WTL*xl*xl + PAL_WTC*(xcb*xcb + xcr*xcr);

		// check if nearest is best
		if(bdiff == -1 || xdiff < bdiff)
		{
			bbest = i;
			bdiff = xdiff;
#ifndef PIXEL15
			bdiff_grey = PAL_WTL*xl*xl + PAL_WTC_GREY*(xcb*xcb + xcr*xcr);
#endif
		}
	}

#ifndef PIXEL15
	// check if we have a grey
	if(grey_diff < bdiff_grey)
	{
		// decode best grey
		*r = *g = *b = grey_l_base;

		if(grey_pal == 0)
		{
			pal_to_rgb(16, r, g, b);
			return 16;

		} else if(grey_pal <= 16) {
			pal_to_rgb(grey_pal-1, r, g, b);
			return grey_pal-1;
		}

		// otherwise fall through
	}
#endif

	// decode best colour
	int br = ((bbest&1) ? r1 : r0);
	int bg = ((bbest&2) ? g1 : g0);
	int bb = ((bbest&4) ? b1 : b0);

#ifdef PIXEL15
	int pal = (bb<<10)|(bg<<5)|br;
#else
	int pal = 16+(bg + 8*(br + 6*bb));
#endif
	/*
	*r = (br*255)/6;
	*g = (bg*255)/8;
	*b = (bb*255)/5;
	*/
	pal_to_rgb(pal, r, g, b);
	return pal;
}

#ifdef PIXEL15
int rgb_to_pal_noyuv(int *r, int *g, int *b)
{
	// check if r,g,b within bounds
	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);

	// get nearest RGB point
	//int rc = (*r*31)/255;
	//int gc = (*g*31)/255;
	//int bc = (*b*31)/255;
	int rc = (*r)>>3;
	int gc = (*g)>>3;
	int bc = (*b)>>3;

	// check if bounds are correct
	assert(rc >= 0); assert(gc >= 0); assert(bc >= 0);
	assert(rc < 32); assert(gc < 32); assert(bc < 32);

	int pal = (bc<<10)|(gc<<5)|rc;
	pal_to_rgb(pal, r, g, b);
	//fprintf(stderr, "#%02X%02X%02X -> #%02X%02X%02X (%04X)\n"
		//, rc, gc, bc, *r, *g, *b, pal);
	return pal;
}

int rgb_to_pal_walk(int *r, int *g, int *b)
{
	// check if r,g,b within bounds
	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);

	// get YUV
	int lt, ut, vt;
	to_ycbcr(*r, *g, *b, &lt, &ut, &vt);

	// get nearest RGB point
	int rc = (*r*31)/255;
	int gc = (*g*31)/255;
	int bc = (*b*31)/255;

	// check if bounds are correct
	assert(rc >= 0); assert(gc >= 0); assert(bc >= 0);
	assert(rc < 32); assert(gc < 32); assert(bc < 32);

	// get matrix
	int arl, aru, arv;
	int agl, agu, agv;
	int abl, abu, abv;

	to_ycbcr(1, 0, 0, &arl, &aru, &arv);
	to_ycbcr(0, 1, 0, &agl, &agu, &agv);
	to_ycbcr(0, 0, 1, &abl, &abu, &abv);

	// loop
	int is_improved = 0;
	do
	{
		is_improved = 0;

		int lc, uc, vc;
		to_ycbcr(rc*255/31, gc*255/31, bc*255/31, &lc, &uc, &vc);
		lc -= lt; uc -= ut; vc -= vt;
		int xd = lc*lc + uc*uc + vc*vc;

		int zr, zg, zb;

		/*
		int rlo = (rc == 0  || vr > 0 ? 0 : -1);
		int rhi = (rc == 31 || vr < 0 ? 0 :  1);
		int glo = (gc == 0  || vg > 0 ? 0 : -1);
		int ghi = (gc == 31 || vg < 0 ? 0 :  1);
		int blo = (bc == 0  || vb > 0 ? 0 : -1);
		int bhi = (bc == 31 || vb < 0 ? 0 :  1);
		*/
		int rlo = (rc == 0  ? 0 : -1);
		int rhi = (rc == 31 ? 0 :  1);
		int glo = (gc == 0  ? 0 : -1);
		int ghi = (gc == 31 ? 0 :  1);
		int blo = (bc == 0  ? 0 : -1);
		int bhi = (bc == 31 ? 0 :  1);

		for(zr = rlo; zr <= rhi; zr++)
		for(zg = glo; zg <= ghi; zg++)
		for(zb = blo; zb <= bhi; zb++)
		{
			// get colour
			int sr = rc+zr;
			int sg = gc+zg;
			int sb = bc+zb;

			// reject if out of range
			if(sr >= 32 || sr < 0) continue;
			if(sg >= 32 || sg < 0) continue;
			if(sb >= 32 || sb < 0) continue;

			// get YUV
			int ls, us, vs;
			// the to_ycbcr route is faster but much slower
			ls = lc + arl*zr + agl*zg + abl*zb;
			us = uc + aru*zr + agu*zg + abu*zb;
			vs = vc + arv*zr + agv*zg + abv*zb;
			//to_ycbcr(sr*255/31, sg*255/31, sb*255/31, &ls, &us, &vs);

			// get distance
			ls -= lt; us -= ut; vs -= vt;
			int sd = ls*ls + us*us + vs*vs;

			// check if improved
			if(sd < xd)
			{
				is_improved = 1;
				rc = sr; gc = sg; bc = sb;
				//vr = zr; vg = zg; vb = zb;
			}
		}
	} while(is_improved);

	int pal = (bc<<10)|(gc<<5)|rc;
	pal_to_rgb(pal, r, g, b);
	//fprintf(stderr, "#%02X%02X%02X -> #%02X%02X%02X (%04X)\n"
		//, rc, gc, bc, *r, *g, *b, pal);
	return pal;
}
#endif

