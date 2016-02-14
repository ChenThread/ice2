#include "common.h"

int get_average_rect_in(int layer, int bx, int by, int bw, int bh)
{
	assert(layer >= 0 && layer < 3);
	int x, y;

	uint8_t *sptr = (uint8_t *)rawinbuf[layer];

	assert(bx >= 0 && bx+bw <= VW);
	assert(by >= 0 && by+bh <= VH);

	int avg_size = bw*bh;
	assert(avg_size > 0);

	int avg;
	if(bw >= 4)
	{
		__m128i acc_l = _mm_setzero_si128();
		__m128i cmp_endmask = get_end_mask(bw);

		sptr += VW*by;
		sptr += bx;
		for(y = by; y < by+bh; y++, sptr += VW)
		{
			for(x = 0; x < bw-15; x += 16)
			{
				__m128i src_l  = _mm_loadu_si128((__m128i *)(sptr + x));
				acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, _mm_setzero_si128()));
			}

			if(x < bw)
			{
				__m128i src_l = _mm_loadu_si128((__m128i *)(sptr + x));
				src_l  = _mm_and_si128(src_l, cmp_endmask);
				acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, _mm_setzero_si128()));
			}
		}

		acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_l = _mm_hadd_epi32(acc_l, acc_l);
		avg = _mm_cvtsi128_si32(acc_l);
	} else {
		avg = 0;

		sptr += VW*by;
		for(y = by; y < by+bh; y++, sptr += VW)
		for(x = bx; x < bx+bw; x++)
			avg += sptr[x];
	}

	avg = (avg + (avg_size>>1))/avg_size;
	return avg;
}

int compare_rect_copy_layer(int layer, int sx, int sy, int rw, int rh, int dx, int dy)
{
	// HOTSPOT: called 31*31*3 times every frame
	assert(layer >= 0 && layer < 3);

	__m128i acc_l = _mm_setzero_si128();
	__m128i cmp_endmask = get_end_mask(rw);

	int y;
	for(y = 0; y < rh; y++)
	{
		// build chain
		__m128i *sv_l = (__m128i *)&rawlastbuf_mvec[layer][y+sy][sx];
		__m128i *dv_l = (__m128i *)&rawinbuf_mvec[layer][y+dy][dx];

		// source:
		// +1 +2 +3 +4 | -1
		// =0 +1 +2 +3 | =0
		// -1 =0 +1 +2 | +1

		// TODO: clean up + optimise left+right cases
		int x;
		for(x = 0; x < rw-15; x += 16)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}

		if(x < rw)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			src_l  = _mm_and_si128(src_l,  cmp_endmask);
			dest_l = _mm_and_si128(dest_l, cmp_endmask);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
	}

	acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
	acc_l = _mm_hadd_epi32(acc_l, acc_l);
	return _mm_cvtsi128_si32(acc_l);
}

int compare_rect_fill_layer_small(int layer, int dx, int dy, int rw, int rh, int ref)
{
	assert(layer >= 0 && layer < 3);
	// use this for smaller things
	int acc = 0;

	int x, y;
	for(y = 0; y < rh; y++)
	for(x = 0; x < rw; x++)
	{
		int diff = rawinbuf[layer][y+dy][x+dx] - ref;

		if(diff < 0)
			diff = -diff;

		acc += diff;
	}

	return acc;
}

int compare_rect_fill_layer(int layer, int dx, int dy, int rw, int rh, int ref)
{
	assert(layer >= 0 && layer < 3);
	assert(dx >= 0 && dy >= 0);
	assert(dx+rw <= VW);
	assert(dy+rh <= VH);
	__m128i acc_l = _mm_setzero_si128();

	__m128i cmp_endmask = get_end_mask(rw);
	__m128i src_l  = _mm_set1_epi8(ref);
	__m128i alt_src_l = _mm_and_si128(src_l, cmp_endmask);

	int y;
	for(y = 0; y < rh; y++)
	{
		// build chain
		__m128i *dv_l = (__m128i *)&rawinbuf[layer][y+dy][dx];

		// source:
		// +1 +2 +3 +4 | -1
		// =0 +1 +2 +3 | =0
		// -1 =0 +1 +2 | +1

		int x;
		for(x = 0; x < rw-15; x += 16)
		{
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}

		if(x < rw)
		{
			__m128i dest_l = _mm_loadu_si128(dv_l);
			dest_l = _mm_and_si128(dest_l, cmp_endmask);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(alt_src_l, dest_l));
		}
	}

	acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
	acc_l = _mm_hadd_epi32(acc_l, acc_l);
	int ret = _mm_cvtsi128_si32(acc_l);
	//assert(compare_rect_fill_layer_small(layer, dx, dy, rw, rh, ref) == ret);
	return ret;
}

int compare_rect_still_small(int layer, int dx, int dy, int rw, int rh)
{
	assert(layer >= 0 && layer < 3);
	// use this for smaller things
	int acc = 0;

	int x, y;
	for(y = 0; y < rh; y++)
	for(x = 0; x < rw; x++)
	{
		int diff = rawinbuf[layer][y+dy][x+dx]
			- rawcurbuf[layer][y+dy][x+dx];

		if(diff < 0)
			diff = -diff;

		acc += diff;
	}

	return acc;
}

int compare_full_screen(int32_t *rows)
{
#if (VW&15) != 0
#error "image width must be a multiple of 16"
#endif

	int ret = 0;
	__m128i *sv_l = (__m128i *)&rawcurbuf[0][0][0];
	__m128i *sv_cb = (__m128i *)&rawcurbuf[1][0][0];
	__m128i *sv_cr = (__m128i *)&rawcurbuf[2][0][0];
	__m128i *dv_l = (__m128i *)&rawinbuf[0][0][0];
	__m128i *dv_cb = (__m128i *)&rawinbuf[1][0][0];
	__m128i *dv_cr = (__m128i *)&rawinbuf[2][0][0];

	int y;
	for(y = 0; y < VH; y++)
	{
		__m128i acc_l = _mm_setzero_si128();
		__m128i acc_cb = _mm_setzero_si128();
		__m128i acc_cr = _mm_setzero_si128();

		int x;
		for(x = (VW>>4); x != 0; x--)
		{
			__m128i src_l  = _mm_load_si128(sv_l++);
			__m128i dest_l = _mm_load_si128(dv_l++);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
		for(x = (VW>>4); x != 0; x--)
		{
			__m128i src_cb  = _mm_load_si128(sv_cb++);
			__m128i dest_cb = _mm_load_si128(dv_cb++);
			acc_cb = _mm_add_epi32(acc_cb, _mm_sad_epu8(src_cb, dest_cb));
		}
		for(x = (VW>>4); x != 0; x--)
		{
			__m128i src_cr  = _mm_load_si128(sv_cr++);
			__m128i dest_cr = _mm_load_si128(dv_cr++);
			acc_cr = _mm_add_epi32(acc_cr, _mm_sad_epu8(src_cr, dest_cr));
		}

		acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_l = _mm_hadd_epi32(acc_l, acc_l);
		acc_cb = _mm_shuffle_epi32(acc_cb, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_cb = _mm_hadd_epi32(acc_cb, acc_cb);
		acc_cr = _mm_shuffle_epi32(acc_cr, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_cr = _mm_hadd_epi32(acc_cr, acc_cr);
		int v_l = _mm_cvtsi128_si32(acc_l);
		int v_cb = _mm_cvtsi128_si32(acc_cb);
		int v_cr = _mm_cvtsi128_si32(acc_cr);
		int v = v_l*WTL + v_cb*WTCB + v_cr*WTCR;
		rows[y] = v;
		ret += v;
	}

	return ret;

}

int compare_rect_still_rows(int dx, int dy, int rw, int rh, int32_t *rows)
{
	// this does not need to strictly match the box
	// as it is always used as a comparison
	// so we can actually just overflow and also not mask anything

	//__m128i cmp_endmask = get_end_mask(rw);
	int ret = 0;

	// align to left
	rw += (dx&15);
	dx &= ~15;

	int y;
	for(y = 0; y < rh; y++)
	{
		__m128i acc_l = _mm_setzero_si128();
		__m128i acc_cb = _mm_setzero_si128();
		__m128i acc_cr = _mm_setzero_si128();

		// build chain
		__m128i *sv_l = (__m128i *)&rawcurbuf[0][y+dy][dx];
		__m128i *sv_cb = (__m128i *)&rawcurbuf[1][y+dy][dx];
		__m128i *sv_cr = (__m128i *)&rawcurbuf[2][y+dy][dx];
		__m128i *dv_l = (__m128i *)&rawinbuf[0][y+dy][dx];
		__m128i *dv_cb = (__m128i *)&rawinbuf[1][y+dy][dx];
		__m128i *dv_cr = (__m128i *)&rawinbuf[2][y+dy][dx];

		int x;
		for(x = 0; x < rw; x += 16)
		{
			__m128i src_l  = _mm_load_si128(sv_l++);
			__m128i dest_l = _mm_load_si128(dv_l++);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
		for(x = 0; x < rw; x += 16)
		{
			__m128i src_cb  = _mm_load_si128(sv_cb++);
			__m128i dest_cb = _mm_load_si128(dv_cb++);
			acc_cb = _mm_add_epi32(acc_cb, _mm_sad_epu8(src_cb, dest_cb));
		}
		for(x = 0; x < rw; x += 16)
		{
			__m128i src_cr  = _mm_load_si128(sv_cr++);
			__m128i dest_cr = _mm_load_si128(dv_cr++);
			acc_cr = _mm_add_epi32(acc_cr, _mm_sad_epu8(src_cr, dest_cr));
		}

		acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_l = _mm_hadd_epi32(acc_l, acc_l);
		acc_cb = _mm_shuffle_epi32(acc_cb, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_cb = _mm_hadd_epi32(acc_cb, acc_cb);
		acc_cr = _mm_shuffle_epi32(acc_cr, 0 | (2<<2) | (1<<4) | (1<<6));
		acc_cr = _mm_hadd_epi32(acc_cr, acc_cr);
		int v_l = _mm_cvtsi128_si32(acc_l);
		int v_cb = _mm_cvtsi128_si32(acc_cb);
		int v_cr = _mm_cvtsi128_si32(acc_cr);
		int v = v_l*WTL + v_cb*WTCB + v_cr*WTCR;
		rows[y] = v;
		ret += v;
	}

	//assert(ret == compare_rect_still_small(layer, dx, dy, rw, rh));
	return ret;
}

int compare_rect_still(int layer, int dx, int dy, int rw, int rh)
{
	assert(layer >= 0 && layer < 3);
	__m128i acc_l = _mm_setzero_si128();
	__m128i cmp_endmask = get_end_mask(rw);

	int y;
	for(y = 0; y < rh; y++)
	{
		// build chain
		__m128i *sv_l = (__m128i *)&rawcurbuf[layer][y+dy][dx];
		__m128i *dv_l = (__m128i *)&rawinbuf[layer][y+dy][dx];

		// source:
		// +1 +2 +3 +4 | -1
		// =0 +1 +2 +3 | =0
		// -1 =0 +1 +2 | +1

		// TODO: clean up + optimise left+right cases
		int x;
		for(x = 0; x < rw-15; x += 16)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}

		if(x < rw)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			src_l  = _mm_and_si128(src_l,  cmp_endmask);
			dest_l = _mm_and_si128(dest_l, cmp_endmask);
			acc_l = _mm_add_epi32(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
	}

	acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
	acc_l = _mm_hadd_epi32(acc_l, acc_l);
	int ret = _mm_cvtsi128_si32(acc_l);
	//assert(ret == compare_rect_still_small(layer, dx, dy, rw, rh));
	return ret;
}

int compare_rect_still_pal(int dx, int dy, int rw, int rh)
{
	//__m128i acc_l = _mm_setzero_si128();
	//__m128i cmp_endmask = get_end_mask(rw);

	int y;
	for(y = 0; y < rh; y++)
	{
#if 1
		if(memcmp(&rawcurbuf_pal[y+dy][dx], &rawinbuf_pal[y+dy][dx], rw*sizeof(pixeltyp)))
			return 1;
#else
		// build chain
		__m128i *sv_l = (__m128i *)&rawcurbuf_pal[y+dy][dx];
		__m128i *dv_l = (__m128i *)&rawinbuf_pal[y+dy][dx];

		// source:
		// +1 +2 +3 +4 | -1
		// =0 +1 +2 +3 | =0
		// -1 =0 +1 +2 | +1

		// TODO: clean up + optimise left+right cases
		// TODO: bail on difference
		int x;
#ifdef PIXEL15
		// what's important is that the result is either 0 or not 0
		// thus, sad_epu8 is fine
		for(x = 0; x < rw-7; x += 8)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			acc_l = _mm_add_epi64(acc_l, _mm_sad_epu8(src_l, dest_l));
		}

		if(x < rw)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			src_l  = _mm_and_si128(src_l,  cmp_endmask);
			dest_l = _mm_and_si128(dest_l, cmp_endmask);
			acc_l = _mm_add_epi64(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
#else
		for(x = 0; x < rw-15; x += 16)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			acc_l = _mm_add_epi64(acc_l, _mm_sad_epu8(src_l, dest_l));
		}

		if(x < rw)
		{
			__m128i src_l  = _mm_loadu_si128(sv_l++);
			__m128i dest_l = _mm_loadu_si128(dv_l++);
			src_l  = _mm_and_si128(src_l,  cmp_endmask);
			dest_l = _mm_and_si128(dest_l, cmp_endmask);
			acc_l = _mm_add_epi64(acc_l, _mm_sad_epu8(src_l, dest_l));
		}
#endif
#endif
	}

#if 1
	return 0;
#else
	acc_l = _mm_shuffle_epi32(acc_l, 0 | (2<<2) | (1<<4) | (1<<6));
	acc_l = _mm_hadd_epi32(acc_l, acc_l);
	int ret = _mm_cvtsi128_si32(acc_l);
	return ret;
#endif
}

