// REQUIRES AN SSSE3-CAPABLE X86 OR AMD64 CPU
// (might not quite be right but you SHOULD have at LEAST SSSE3)

#include "common.h"

//define GPU_BUDGET_MAX 2000
FILE *fp;

int gpu_usage_saving2 = 0;
int gpu_usage_saving = 0; // FIXME: memory error somewhere and valgrind can't be fucked finding it
int gpu_usage = 0;
int gpu_pal_bg_fbeg = 16;
int gpu_pal_fg_fbeg = 255;
int gpu_pal_bg = 16;
int gpu_pal_fg = 255;
int gpu_pal_next_is_bg = 0;
int gpu_pal_next_is_bg_fbeg = 0;
int gpu_compact_pal_changes = 0;

uint8_t realrawinbuf[VH][VW][3];
uint8_t realrawoutbuf[VH][VW][3];
uint8_t rawinbuf[3][VH][VW];
uint8_t precurbuf[3][VH][VW];
uint8_t rawcurbuf[3][VH][VW];

uint8_t rawlastbuf_mvec[3][VH][VW];
uint8_t rawinbuf_mvec[3][VH][VW];

int32_t rowoldbuf[VH];
int32_t rownewbuf[VH];
int32_t rowcmpbuf[VH];
int32_t rowcmprange;

pixeltyp rawinbuf_new_pal[VH][VW];
pixeltyp rawinbuf_pal[VH][VW];
pixeltyp precurbuf_pal[VH][VW];
pixeltyp rawcurbuf_pal[VH][VW];

gop_s gop[GOP_MAX];
int gop_count = 0;
int gop_bloat = 0;
int gop_bloat_cost = 0;

#ifdef FAST_LARGE_BOXES
static int calc_fill_boost_largebox(int x1, int y1, int x2, int y2, int al, int acb, int acr)
{
	assert(x2 > x1);
	assert(y2 > y1);

	if(x2-x1 > 4)
	{
		int bl, bcb, bcr;
		bl  = compare_rect_still(0, x1, y1, x2-x1, y2-y1);
		bcb = compare_rect_still(1, x1, y1, x2-x1, y2-y1);
		bcr = compare_rect_still(2, x1, y1, x2-x1, y2-y1);
		int bcmb = WTL*bl + WTCB*bcb + WTCR*bcr;

		int fl, fcb, fcr;
		fl  = compare_rect_fill_layer(0, x1, y1, x2-x1, y2-y1, al);
		fcb = compare_rect_fill_layer(1, x1, y1, x2-x1, y2-y1, acb);
		fcr = compare_rect_fill_layer(2, x1, y1, x2-x1, y2-y1, acr);
		int fcmb = WTL*fl + WTCB*fcb + WTCR*fcr;

		return bcmb - fcmb;

	} else {

		int bl, bcb, bcr;
		bl  = compare_rect_still_small(0, x1, y1, x2-x1, y2-y1);
		bcb = compare_rect_still_small(1, x1, y1, x2-x1, y2-y1);
		bcr = compare_rect_still_small(2, x1, y1, x2-x1, y2-y1);
		int bcmb = WTL*bl + WTCB*bcb + WTCR*bcr;

		int fl, fcb, fcr;
		fl  = compare_rect_fill_layer_small(0, x1, y1, x2-x1, y2-y1, al);
		fcb = compare_rect_fill_layer_small(1, x1, y1, x2-x1, y2-y1, acb);
		fcr = compare_rect_fill_layer_small(2, x1, y1, x2-x1, y2-y1, acr);
		int fcmb = WTL*fl + WTCB*fcb + WTCR*fcr;

		return bcmb - fcmb;
	}
}
#endif

static int calc_fill_boost(int x1, int y1, int x2, int y2)
{
	// HOTSPOT: called a lot of times for every rect fill calculation

	//fprintf(stderr, "%i %i %i %i\n", x1, y1, x2, y2);
	assert(x2 > x1);
	assert(y2 > y1);

	int bw = x2-x1;
	int bh = y2-y1;

	// FIXME: needs profiling
	int do_small = (bw <= 4 || bh <= 2 || bw*bh <= 40);

	int bl, bcb, bcr;
	if(do_small)
	{
		bl  = compare_rect_still_small(0, x1, y1, x2-x1, y2-y1);
		bcb = compare_rect_still_small(1, x1, y1, x2-x1, y2-y1);
		bcr = compare_rect_still_small(2, x1, y1, x2-x1, y2-y1);

	} else {
		bl  = compare_rect_still(0, x1, y1, x2-x1, y2-y1);
		bcb = compare_rect_still(1, x1, y1, x2-x1, y2-y1);
		bcr = compare_rect_still(2, x1, y1, x2-x1, y2-y1);

	}

	int bcmb = WTL*bl + WTCB*bcb + WTCR*bcr;

	int al, acb, acr;
	al  = get_average_rect_in(0, x1, y1, x2-x1, y2-y1);
	acb = get_average_rect_in(1, x1, y1, x2-x1, y2-y1);
	acr = get_average_rect_in(2, x1, y1, x2-x1, y2-y1);
	int ar, ag, ab;
	from_ycbcr(al, acb, acr, &ar, &ag, &ab);
	rgb_to_pal(&ar, &ag, &ab);
	to_ycbcr(ar, ag, ab, &al, &acb, &acr);

	int fl, fcb, fcr;
	if(do_small)
	{
		fl  = compare_rect_fill_layer_small(0, x1, y1, x2-x1, y2-y1, al);
		fcb = compare_rect_fill_layer_small(1, x1, y1, x2-x1, y2-y1, acb);
		fcr = compare_rect_fill_layer_small(2, x1, y1, x2-x1, y2-y1, acr);

	} else {
		fl  = compare_rect_fill_layer(0, x1, y1, x2-x1, y2-y1, al);
		fcb = compare_rect_fill_layer(1, x1, y1, x2-x1, y2-y1, acb);
		fcr = compare_rect_fill_layer(2, x1, y1, x2-x1, y2-y1, acr);
	}
	int fcmb = WTL*fl + WTCB*fcb + WTCR*fcr;
	//assert((x2-x1 == 1 && y2-y1 == 1) ? fcmb == 0 : 1);
	//assert((x2-x1 == 1 && y2-y1 == 1) ? bcmb > 0 : 1);

	//fprintf(stderr, "%i %i: %i %i %i\n", x2-x1, y2-y1, bcmb, fcmb, bcmb-fcmb);
	return bcmb - fcmb;

}

static int calc_mvec(int mx, int my)
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

static void *calc_motion_comp(void *tdat)
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
	if(move_diff < 28*VW*VH)
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

static void *algo_1(void *tdat)
{
	struct tdat_algo_1 *T = (struct tdat_algo_1 *)tdat;

	//fprintf(stderr, "ALGO RUN %p\n", tdat);

	int x, y, i;

	// back up current buffer for algo replay
	memcpy(precurbuf, rawcurbuf, VW*VH*3);
	memcpy(precurbuf_pal, rawcurbuf_pal, VW*VH*sizeof(pixeltyp));

	gpu_start();

	if(T->Tm.move_x != 0 || T->Tm.move_y != 0)
	{
		gpu_copy(
			T->Tm.move_x > 0 ? 0 : -T->Tm.move_x,
			T->Tm.move_y > 0 ? 0 : -T->Tm.move_y,
			VW - (T->Tm.move_x < 0 ? -T->Tm.move_x : T->Tm.move_x),
			VH - (T->Tm.move_y < 0 ? -T->Tm.move_y : T->Tm.move_y),
			T->Tm.move_x, T->Tm.move_y, 1);
	}

	// calculate boost range
	rowcmprange = compare_full_screen(rowcmpbuf);

	// find ops to use
	int attempts_remaining = 30;
	while(attempts_remaining > 0 && (0
		//|| compare_rect_still_pal(0, 0, VW, VH) != 0
		|| rowcmprange > 0
		//|| compare_rect_still(0, 0, 0, VW, VH) != 0
		//|| compare_rect_still(1, 0, 0, VW, VH) != 0
		//|| compare_rect_still(2, 0, 0, VW, VH) != 0
		))
	{
		int x1, y1, x2, y2;

		if(gpu_usage >= GPU_BUDGET_MAX)
		{
#ifndef NOCOMPACT
			gpu_compact();
#endif

			// sacrificing a bit of compactness for speed here in PIXEL15 mode
			// this is because compaction doesn't add *much*
#ifndef PIXEL15
			if(gpu_usage >= GPU_BUDGET_MAX)
#endif
				break;

#ifndef NOCOMPACT
			rowcmprange = compare_full_screen(rowcmpbuf);

			// check if we still have stuff to change
			if(rowcmprange <= 0)
				break;
#endif
		}

		// pick a weighted random point
		int32_t boost_acc = (rand()>>1) % rowcmprange;
		for(y = 0; y < VH; y++)
		{
			//fprintf(stderr, "%i %i %i %i\n", y, boost_rows[y], boost_range, boost_acc);
			if(boost_acc >= rowcmpbuf[y])
			{
				boost_acc -= rowcmpbuf[y];
				continue;
			}

			for(x = 0; x < VW; x++)
			{
				x1 = x;
				y1 = y;

				if(rawcurbuf_pal[y][x] == rawinbuf_pal[y][x])
					continue;

				int kl  = (int)rawcurbuf[0][y][x] - (int)rawinbuf[0][y][x];
				int kcb = (int)rawcurbuf[1][y][x] - (int)rawinbuf[1][y][x];
				int kcr = (int)rawcurbuf[2][y][x] - (int)rawinbuf[2][y][x];
				/*
				fprintf(stderr, "%02X %02X %02X -> %02X %02X %02X\n"
					,rawcurbuf[0][y][x]
					,rawcurbuf[1][y][x]
					,rawcurbuf[2][y][x]
					,rawinbuf[0][y][x]
					,rawinbuf[1][y][x]
					,rawinbuf[2][y][x]);
				*/

				if(kl  < 0) kl  = -kl;
				if(kcb < 0) kcb = -kcb;
				if(kcr < 0) kcr = -kcr;

				int ptchg = WTL*kl + WTCB*kcb + WTCR*kcr;

				//if(ptchg <= 0) continue;
				assert(ptchg > 0);

				boost_acc -= ptchg;
				if(boost_acc <= 0)
					goto selected_random;
			}

			assert(!"shouldn't reach here!");
		}

		//fprintf(stderr, "diff %i %i\n", boost_acc, boost_range);
		assert(!"shouldn't reach here!");
		attempts_remaining--;
		continue;

		selected_random: (void)0;

		// start with 1 pixel
		const int init_w = 1;
		const int init_h = 1;
		x2 = x1+init_w;
		y2 = y1+init_h;

		// zoom in
#ifdef FAST_LARGE_BOXES
		int large_box = 0;
		int large_thres = 20;
		int large_l = 0, large_cb = 0, large_cr = 0;
#endif

		int base_boost = calc_fill_boost(x1, y1, x2, y2);
		for(;;)
		{
			//fprintf(stderr, "%i %i %i %i\n", x1, y1, x2, y2);
			int best_boost = base_boost-1;
			int boost = 0;
			int boost_var = 0;
			int boost_dir = 0;

#ifdef FAST_LARGE_BOXES
			if(large_box)
			{
				int steepness = 0;
				int large_boost = 0;
				int large_var = 0;
				int large_dir = 0;
				if(x1 > 0)
				{
					boost = calc_fill_boost_largebox(x1-1, y1, x1, y2
						, large_l, large_cb, large_cr);
					if(boost > large_boost)
						{ large_boost = boost; large_var = 11; large_dir = -1; }
				}

				if(y1 > 0)
				{
					boost = calc_fill_boost_largebox(x1, y1-1, x2, y1
						, large_l, large_cb, large_cr);
					if(boost > large_boost)
						{ large_boost = boost; large_var = 21; large_dir = -1; }
				}

				if(x2 < VW)
				{
					boost = calc_fill_boost_largebox(x2, y1, x2+1, y2
						, large_l, large_cb, large_cr);
					if(boost > large_boost)
						{ large_boost = boost; large_var = 12; large_dir = 1; }
				}

				if(y2 < VH)
				{
					boost = calc_fill_boost_largebox(x1, y2, x2, y2+1
						, large_l, large_cb, large_cr);
					if(boost > large_boost)
						{ large_boost = boost; large_var = 22; large_dir = 1; }
				}

				if(large_boost > steepness)
				{
					best_boost = base_boost + large_boost;
					boost_var = large_var;
					boost_dir = large_dir;
				}

			} else {
#endif
				if(x1 > 0)
				{
					boost = calc_fill_boost(x1-1, y1, x2, y2);
					if(boost > best_boost)
						{ best_boost = boost; boost_var = 11; boost_dir = -1; }
				}

				if(y1 > 0)
				{
					boost = calc_fill_boost(x1, y1-1, x2, y2);
					if(boost > best_boost)
						{ best_boost = boost; boost_var = 21; boost_dir = -1; }
				}

				if(x2 < VW)
				{
					boost = calc_fill_boost(x1, y1, x2+1, y2);
					if(boost > best_boost)
						{ best_boost = boost; boost_var = 12; boost_dir = 1; }
				}

				if(y2 < VH)
				{
					boost = calc_fill_boost(x1, y1, x2, y2+1);
					if(boost > best_boost)
						{ best_boost = boost; boost_var = 22; boost_dir = 1; }
				}
#ifdef FAST_LARGE_BOXES
			}
#endif

			if(best_boost < base_boost)
				break;

			base_boost = best_boost;
			switch(boost_var)
			{
				case 11: x1 += boost_dir; break;
				case 12: x2 += boost_dir; break;
				case 21: y1 += boost_dir; break;
				case 22: y2 += boost_dir; break;
				default:
					assert(!"DAMMIT");
			}

#ifdef FAST_LARGE_BOXES
			if(!large_box)
			{
				if((x2-x1)*(y2-y1) >= large_thres)
				{
					large_box = 20;

					/*
					// check if in the last few ops
					for(i = 0; i < 5 && i < gop_count; i++)
					{
						if(x1 >= gop[gop_count-1-i].bx)
						if(y1 >= gop[gop_count-1-i].by)
						if(x2 <= x1+gop[gop_count-1-i].bw)
						if(y2 <= y1+gop[gop_count-1-i].bh)
						{
							// this is to make e.g. Hardwired behave
							// w/o bogging the whole thing down
							large_box = 0;
							large_thres *= 3;
							attempts_remaining--;
							break;
						}
					}
					*/

					if(large_box)
					{
						large_l  = get_average_rect_in(0, x1, y1, x2-x1, y2-y1);
						large_cb = get_average_rect_in(1, x1, y1, x2-x1, y2-y1);
						large_cr = get_average_rect_in(2, x1, y1, x2-x1, y2-y1);
					}
				}
			} else {
				// full checks must take place from time to time
				// otherwise you end up with a rather nasty op fighting loop
				// (case in point: Hardwired demo, b-spline in wireframe cube section)
				large_box--;
			}
#endif
		}

		if(base_boost <= 0)
		{
			assert(!"BUGGER");

			attempts_remaining--;
			continue;
		}

		//fprintf(stderr, "OP %i %i %i %i\n", x1, y1, x2, y2); fflush(stderr);

		int al  = get_average_rect_in(0, x1, y1, x2-x1, y2-y1);
		int acb = get_average_rect_in(1, x1, y1, x2-x1, y2-y1);
		int acr = get_average_rect_in(2, x1, y1, x2-x1, y2-y1);
		int ar, ag, ab;
		//fprintf(stderr, "--------\n");
		//fprintf(stderr, "%02X %02X %02X\n", al, acb, acr);
		from_ycbcr(al, acb, acr, &ar, &ag, &ab);
		int apal = rgb_to_pal(&ar, &ag, &ab);
		to_ycbcr(ar, ag, ab, &al, &acb, &acr);
		//fprintf(stderr, "%02X %02X %02X\n", al, acb, acr);

		int oldrange  = compare_rect_still_rows(x1, y1, x2-x1, y2-y1, rowoldbuf + y1);
		gpu_fill(x1, y1, x2-x1, y2-y1, al, acb, acr, apal, 1);
		int newrange  = compare_rect_still_rows(x1, y1, x2-x1, y2-y1, rownewbuf + y1);
		//fprintf(stderr, "oldrange %i %i\n", rowcmprange, oldrange);
		rowcmprange -= oldrange;
		rowcmprange += newrange;
		//fprintf(stderr, "newrange %i %i\n", rowcmprange, newrange);

		//int oldsum = 0;
		//int newsum = 0;
		for(y = y1; y < y2; y++)
		{
			//fprintf(stderr, "oldrow %4i %i %i\n", y, rowcmpbuf[y], rowoldbuf[y]);
			//oldsum += rowoldbuf[y];
			rowcmpbuf[y] -= rowoldbuf[y];
			rowcmpbuf[y] += rownewbuf[y];
			//newsum += rownewbuf[y];
			//fprintf(stderr, "newrow %4i %i %i\n", y, rowcmpbuf[y], rownewbuf[y]);
		}

		//fprintf(stderr, "oldsum %i %i\n", oldsum, oldrange);
		//fprintf(stderr, "newsum %i %i\n", newsum, newrange);
		//assert(oldsum == oldrange);
		//assert(newsum == newrange);
	}

#ifndef NOCOMPACT
	if(gop_bloat == 0) gpu_compact();
#endif

	gpu_emit();
	//fprintf(stderr,"BUDGET: %i (s %i, f %i)\n", gpu_budget, gpu_sets, gpu_fills);

	// restore current buffer for algo replay
	memcpy(rawcurbuf, precurbuf, VW*VH*3);
	memcpy(rawcurbuf_pal, precurbuf_pal, VW*VH*sizeof(pixeltyp));

	// replay algorithm
	gpu_pal_fg = gpu_pal_fg_fbeg;
	gpu_pal_bg = gpu_pal_bg_fbeg;
#ifdef PIXEL15
	if(fp != NULL)
	{
		if(gop[0].op != GOP_COPY)
		{
			fputc(0, fp);
			fputc(0, fp);
		} else {
			fputc((gop[0].bx-gop[0].rsx)&0xFF, fp);
			fputc((gop[0].by-gop[0].rsy)&0xFF, fp);
		}
	}
#endif

	for(i = 0; i < gop_count; i++)
	{
		gop_s *g = &gop[i];
		switch(g->op)
		{
			case GOP_FILL_FG:
			case GOP_FILL_BG:
				if(fp != NULL)
				{
#ifdef PIXEL15
					//if(g->bw*g->bh > 2)
					{
#if VW >= 256 || VH >= 248
						fputc(g->bh&0xFF, fp);
						fputc(g->bh>>8, fp);
						fputc(g->bw&0xFF, fp);
						fputc(g->bw>>8, fp);
#else
						fputc(g->bh, fp);
						fputc(g->bw, fp);
#endif
						assert(g->bh >= 1 && g->bh <= VH);
						assert(g->bw >= 1 && g->bw <= VW);
#if VW*VH > 65536
						fputc((g->bx+g->by*VW)&0xFF, fp);
						fputc(((g->bx+g->by*VW)>>8)&0xFF, fp);
						fputc(((g->bx+g->by*VW)>>16)&0xFF, fp);
						fputc(((g->bx+g->by*VW)>>24)&0xFF, fp);
#else
						fputc((g->bx+g->by*VW)&0xFF, fp);
						fputc((g->bx+g->by*VW)>>8, fp);
#endif
						fputc(g->pal & 0xFF, fp);
						fputc(g->pal>>8, fp);
					}

#else
					fputc(g->bh+(g->op == GOP_FILL_BG ? 0x40 : 0x00), fp);
					if(g->pal == (g->op == GOP_FILL_BG ? gpu_pal_bg : gpu_pal_fg))
					{
						fputc(g->by, fp);
						fputc(g->bw, fp);
						fputc(g->bx, fp);
					} else {
						fputc(g->by+0x40, fp);
						fputc(g->bw, fp);
						fputc(g->bx, fp);
						fputc(g->pal, fp);
						if(g->op == GOP_FILL_BG)
							gpu_pal_bg = g->pal;
						else
							gpu_pal_fg = g->pal;
					}
#endif
				}

				// let's not have a repeat of the 24bpp incident
				int ar, ag, ab;
				int l, cb, cr;
				pal_to_rgb(g->pal, &ar, &ag, &ab);
				to_ycbcr(ar, ag, ab, &l, &cb, &cr);
				assert(l == g->l);
				assert(cb == g->cb);
				assert(cr == g->cr);

				gpu_fill(g->bx, g->by, g->bw, g->bh, g->l, g->cb, g->cr, g->pal, 0);
				break;

			case GOP_COPY:
#ifndef PIXEL15
				if(fp != NULL)
				{
					fputc(g->bh+0x80, fp);
					fputc(g->rsy, fp);
					fputc(g->bw, fp);
					fputc(g->rsx, fp);
					fputc(g->bx-g->rsx, fp);
					fputc(g->by-g->rsy, fp);
				}
#endif
				gpu_copy(g->rsx, g->rsy, g->bw, g->bh, g->bx-g->rsx, g->by-g->rsy, 0);
				break;

			default:
				fprintf(stderr, "EDOOFUS: INVALID GOP\n");
				abort();
				break;
		}
	}

#ifdef PIXEL15
	if(fp != NULL)
	{
		/*
		fputc(0xFE, fp);
		fputc(0xFE, fp);
		for(i = 0; i < gop_count; i++)
		{
			gop_s *g = &gop[i];
			if(g->op != GOP_FILL_FG && g->op != GOP_FILL_BG) break;
			if(g->bw != 2 || g->bh != 1) break;
			fputc((g->bx+g->by*VW)&0xFF, fp);
			fputc((g->bx+g->by*VW)>>8, fp);
			fputc(g->pal & 0xFF, fp);
			fputc(g->pal>>8, fp);
		}

		fputc(0xFE, fp);
		fputc(0xFE, fp);
		for(i = 0; i < gop_count; i++)
		{
			gop_s *g = &gop[i];
			if(g->op != GOP_FILL_FG && g->op != GOP_FILL_BG) break;
			if(g->bw != 1 || g->bh != 2) break;
			fputc((g->bx+g->by*VW)&0xFF, fp);
			fputc((g->bx+g->by*VW)>>8, fp);
			fputc(g->pal & 0xFF, fp);
			fputc(g->pal>>8, fp);
		}

		fputc(0xFE, fp);
		fputc(0xFE, fp);
		for(i = 0; i < gop_count; i++)
		{
			gop_s *g = &gop[i];
			if(g->op != GOP_FILL_FG && g->op != GOP_FILL_BG) break;
			if(g->bw != 1 || g->bh != 1) break;
			fputc((g->bx+g->by*VW)&0xFF, fp);
			fputc((g->bx+g->by*VW)>>8, fp);
			fputc(g->pal & 0xFF, fp);
			fputc(g->pal>>8, fp);
		}
		*/

		fputc(0xFF, fp);
		fputc(0xFF, fp);
	}
#else
	if(fp != NULL) fputc(0xFF, fp);
#endif


	// TESTING:
	// convert to rgb24 + write to stdout
	for(y = 0; y < VH; y++)
	for(x = 0; x < VW; x++)
	{
		int r, g, b;
		/*
		int l = rawcurbuf[0][y][x];
		int cb = rawcurbuf[1][y][x];
		int cr = rawcurbuf[2][y][x];
		from_ycbcr(l, cb, cr, &r, &g, &b);
		*/
		pal_to_rgb(rawcurbuf_pal[y][x], &r, &g, &b);
		//pal_to_rgb(rawinbuf_pal[y][x], &r, &g, &b);

		realrawoutbuf[y][x][0] = r;
		realrawoutbuf[y][x][1] = g;
		realrawoutbuf[y][x][2] = b;
	}

	fwrite(realrawoutbuf, VW*VH*3, 1, stdout);

	//fprintf(stderr, "ALGO DONE %p\n", tdat);

	return tdat;
}

int main(int argc, char *argv[])
{
	int x, y;

#ifdef PIXEL15
	//fp = (VH <= 160 && argc > 1 ? fopen(argv[1], "wb") : NULL);
	fp = (argc > 1 ? fopen(argv[1], "wb") : NULL);
#else
	fp = (VH <= 63 && argc > 1 ? fopen(argv[1], "wb") : NULL);
#endif

	if(VH <= 63 && argc > 1)
	{
		if(fp == NULL)
		{
			perror("fopen");
			return 1;
		}
	}

#ifndef PIXEL15
	init_pal_list();
#endif
	memset(rawcurbuf[0], 0, VW*VH);
#ifdef NOYUV
	memset(rawcurbuf[1], 0, VW*VH);
	memset(rawcurbuf[2], 0, VW*VH);
#else
	memset(rawcurbuf[1], 128, VW*VH);
	memset(rawcurbuf[2], 128, VW*VH);
#endif
#ifdef PIXEL15
	memset(rawcurbuf_pal, 0, VW*VH*sizeof(pixeltyp));
#else
	memset(rawcurbuf_pal, 16, VW*VH*sizeof(pixeltyp));
#endif

	// TEST: verify algorithm
	// (test has been run - exact and exact_refimp give IDENTICAL outputs)
#if 0
	{
		int ir, ig, ib;
		int ar, ag, ab;

		for(ir = 0; ir < 256; ir++)
		{
			fprintf(stderr, "%i\n", ir);
			for(ig = 0; ig < 256; ig++)
			for(ib = 0; ib < 256; ib++)
			{
				ar = ir; ag = ig; ab = ib;
				int p0 = rgb_to_pal_exact_refimp(&ar, &ag, &ab);
				ar = ir; ag = ig; ab = ib;
				int p1 = rgb_to_pal_exact(&ar, &ag, &ab);
				assert(p0 == p1);
			}
		}
	}
#endif

	pthread_t algo_thread;
	int fired_algo_thread = 0;
	static struct tdat_algo_1 TaS;

	for(;;)
	{
		// fetch
		if(fread(realrawinbuf, VW*VH*3, 1, stdin) <= 0)
		{
			//for(;;) fputc(rand()>>16, stdout);
			break;
		}

		// convert to YCbCr
		for(y = 0; y < VH; y++)
		for(x = 0; x < VW; x++)
		{
			int r = realrawinbuf[y][x][0];
			int g = realrawinbuf[y][x][1];
			int b = realrawinbuf[y][x][2];

			rawinbuf_new_pal[y][x] = rgb_to_pal_pre(&r, &g, &b);
#ifdef NOYUV
			rawinbuf_mvec[0][y][x] = g-4;
			rawinbuf_mvec[1][y][x] = b-4;
			rawinbuf_mvec[2][y][x] = r-4;
#else
			int l, cb, cr;
			to_ycbcr(r, g, b, &l, &cb, &cr);
			rawinbuf_mvec[0][y][x] = l;
			rawinbuf_mvec[1][y][x] = cb;
			rawinbuf_mvec[2][y][x] = cr;
#endif
		}

		// run motion compensation
		struct tdat_calc_motion_comp TmS;
		struct tdat_calc_motion_comp *Tm = calc_motion_comp(&TmS);

		// run algorithm
		if(fired_algo_thread)
		{
			// get result
			struct tdat_algo_1 *Ta;
#ifdef NO_THREADS
			Ta = algo_1(&TaS);
#else
			int e = pthread_join(algo_thread, (void *)&Ta);
			assert(e == 0);
#endif
		}

		// back up intended buffer for frame compare
		memcpy(rawlastbuf_mvec, rawinbuf_mvec, VW*VH*3);

		// copy new buffers
		memcpy(rawinbuf, rawinbuf_mvec, VW*VH*3);
		memcpy(rawinbuf_pal, rawinbuf_new_pal, VW*VH*sizeof(pixeltyp));

		// fire next run
		memcpy(&TaS.Tm, Tm, sizeof(struct tdat_calc_motion_comp));
#ifndef NO_THREADS
		int e = pthread_create(&algo_thread, NULL, algo_1, &TaS);
		assert(e == 0);
#endif
		fired_algo_thread = 1;
	}
	
	fprintf(stderr, "***** DONE *****\n");

	if(fp != NULL)
		fclose(fp);

	return 0;
}

