#include "common.h"

void to_ycbcr(int r, int g, int b, int *restrict l, int *restrict cb, int *restrict cr)
{
#ifdef NOYUV
	//*l  = (r*2+g*5+b+4)>>3;
	//*cb = ((b-*l)) + 0x80;
	//*cr = ((r-*l+1)>>1) + 0x80;
	*l = g-4;
	*cb = b-4;
	*cr = r-4;
#else
	*l  = ( 76*r + 150*g +  29*b +128)>>8;
	*cb = ((-37*r -  74*g + 111*b +128)>>8) + 0x80;
	//*cr = ((157*r - 131*g -  26*b +128)>>8) + 0x80;
	*cr = ((78*r - 65*g -  13*b +128)>>8) + 0x80;
#endif

	if(*l < 0) *l = 0;
	if(*cb < 0) *cb = 0;
	if(*cr < 0) *cr = 0;

	if(*l > 255) *l = 255;
	if(*cb > 255) *cb = 255;
	if(*cr > 255) *cr = 255;
}

// keeping just in case we need it
#if 0
static void to_ycbcr_x8(__m128i r, __m128i g, __m128i b, __m128i *l, __m128i *cb, __m128i *cr)
{
	const __m128i  l_r = _mm_set1_epi16(76);
	const __m128i  l_g = _mm_set1_epi16(150);
	const __m128i  l_b = _mm_set1_epi16(29);
	const __m128i cb_r = _mm_set1_epi16(37); // neg
	const __m128i cb_g = _mm_set1_epi16(74); // neg
	const __m128i cb_b = _mm_set1_epi16(111);
	const __m128i cr_r = _mm_set1_epi16(157/2);
	const __m128i cr_g = _mm_set1_epi16(131/2); // neg
	const __m128i cr_b = _mm_set1_epi16(26/2); // neg
	const __m128i c0x80 = _mm_set1_epi16(0x80);

	// check input ranges
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(r,_mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(g,_mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(b,_mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),r)) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),g)) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),b)) == 0);

	// XXX: cr bounds are out of range, so we have to saturate
	*l = _mm_srli_epi16(_mm_add_epi16(
			_mm_add_epi16(
				_mm_mullo_epi16(l_r, r),
				_mm_mullo_epi16(l_g, g)
			),
			_mm_add_epi16(
				_mm_mullo_epi16(l_b, b),
				c0x80
			)
		), 8);

	*cb = _mm_add_epi16(c0x80,
		_mm_srai_epi16(_mm_sub_epi16(
			_mm_add_epi16(
				_mm_mullo_epi16(cb_b, b),
				c0x80
			),
			_mm_add_epi16(
				_mm_mullo_epi16(cb_r, r),
				_mm_mullo_epi16(cb_g, g)
			)
		), 8));

	*cr = _mm_add_epi16(c0x80,
		_mm_srai_epi16(_mm_sub_epi16(
			_mm_add_epi16(
				_mm_mullo_epi16(cr_r, r),
				c0x80
			),
			_mm_add_epi16(
				_mm_mullo_epi16(cr_b, b),
				_mm_mullo_epi16(cr_g, g)
			)
		), 8));
	// saturate
	// step not necessary provided r,g,b are within bounds and algo correct
	/*
	*l  = _mm_unpacklo_epi8(_mm_packus_epi16(*l, *l),  _mm_setzero_si128());
	*cb = _mm_unpacklo_epi8(_mm_packus_epi16(*cb,*cb), _mm_setzero_si128());
	*cr = _mm_unpacklo_epi8(_mm_packus_epi16(*cr,*cr), _mm_setzero_si128());
	*/

	// check output ranges
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(*l, _mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(*cb,_mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(*cr,_mm_set1_epi16(255))) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),*l )) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),*cb)) == 0);
	assert(_mm_movemask_epi8(_mm_cmpgt_epi16(_mm_set1_epi16(0),*cr)) == 0);
}
#endif

void from_ycbcr(int l, int cb, int cr, int *restrict r, int *restrict g, int *restrict b)
{
#ifdef NOYUV
	/*
	cr <<= 1;
	*r = l + cr;
	*g = l;
	*b = l + cb;
	*/
	*g = l+4;
	*b = cb+4;
	*r = cr+4;
#else
	cb -= 0x80;
	cr -= 0x80;

	cr <<= 1;
	*r = ( 256*l +  0*cb +292*cr + 128)>>8;
	*g = ( 256*l -101*cb -148*cr + 128)>>8;
	*b = ( 256*l +520*cb +  0*cr + 128)>>8;
#endif

	//*r = *g = *b = l;

	if(*r < 0x00) *r = 0x00;
	if(*r > 0xFF) *r = 0xFF;
	if(*g < 0x00) *g = 0x00;
	if(*g > 0xFF) *g = 0xFF;
	if(*b < 0x00) *b = 0x00;
	if(*b > 0xFF) *b = 0xFF;
}

