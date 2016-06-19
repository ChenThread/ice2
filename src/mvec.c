#include "common.h"

int calc_mvec(int mx, int my)
{
	// calculate rectangle geometry
#if 1
	int rw = VW - (mx < 0 ? -mx : mx);
	int rh = VH - (my < 0 ? -my : my);
	int dx = (mx < 0 ? 0 : mx);
	int dy = (my < 0 ? 0 : my);
	int sx = (mx > 0 ? 0 : -mx);
	int sy = (my > 0 ? 0 : -my);
#else
	int rw = VW-32;
	int rh = VH-32;
	int sx = 16;
	int sy = 16;
	int dx = 16+mx;
	int dy = 16+my;
#endif

	// get layers
	int dl  = compare_rect_copy_layer(0, sx, sy, rw, rh, dx, dy);
	int dcb = compare_rect_copy_layer(1, sx, sy, rw, rh, dx, dy);
	int dcr = compare_rect_copy_layer(2, sx, sy, rw, rh, dx, dy);

	return WTL*dl + WTCB*dcb + WTCR*dcr;
}

void *calc_motion_comp(void *tdat)
{
	struct tdat_calc_motion_comp *T = (struct tdat_calc_motion_comp *)tdat;

	int x, y;

	// compare motion vector
	T->move_x = 0;
	T->move_y = 0;
#ifdef NOMOCOMP
	return T;
#endif

	int move_diff, init_diff;
	move_diff = init_diff = calc_mvec(0, 0);

#ifdef LAZY_MOCOMP
	for(y = -15; y <= 15; y++)
	{
		if(y == 0) continue;
		int cdiff = calc_mvec(0, y);

		if(cdiff < move_diff)
		{
			T->move_y = y;
			move_diff = cdiff;
		}
	}

#ifdef PIXEL15
	for(x = -16; x <= 16; x+=2)
#else
	for(x = -15; x <= 15; x++)
#endif
	{
		if(x == 0) continue;
		int cdiff = calc_mvec(x, T->move_y);

		if(cdiff < move_diff)
		{
			T->move_x = x;
			move_diff = cdiff;
		}
	}

	for(y = -15; y <= 15; y++)
	{
		if(y == 0) continue;
		int cdiff = calc_mvec(T->move_x, y);

		if(cdiff < move_diff)
		{
			T->move_y = y;
			move_diff = cdiff;
		}
	}

	//
	// end of LAZY_MOCOMP
	//
#else
	for(y = -15; y <= 15; y++)
#ifdef PIXEL15
	// target platform's gonna be hella slow if we can't LDMIA/STMIA it
	// and that requires 32-bit alignment
	for(x = -16; x <= 16; x+=2)
#else
	for(x = -15; x <= 15; x++)
#endif
	{
		if(x == 0) continue;
		int cdiff = calc_mvec(x, y);

		//cdiff += (x < 0 ? -x : x)*VH*16;
		//cdiff += (y < 0 ? -y : y)*VW*16;

		if(cdiff < move_diff)
		{
			T->move_x = x;
			T->move_y = y;
			move_diff = cdiff;
		}
	}
#endif

	//fprintf(stderr, "MVEC %4i %4i %9i\n", move_x, move_y, move_diff);

	// calculate
	//if(0)
#ifdef PIXEL15
	//if(0) // supported, just damn slow (on the target platform that is)
	if(1) // just make the damn thing trigger anyway
#else
	if(1)
	//if(move_diff < 28*VW*VH)
	//if(move_diff < 40*VW*VH)
#endif
	{
		// do nothing
	} else {
		T->move_x = 0;
		T->move_y = 0;

	}

	return (void *)T;
}
