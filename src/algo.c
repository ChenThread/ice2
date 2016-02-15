#include "common.h"

int debug_output = 0;

uint8_t realrawoutbuf[VH][VW][3];

int32_t rowoldbuf[VH];
int32_t rownewbuf[VH];
int32_t rowcmpbuf[VH];
int32_t rowcmprange;

#ifdef FAST_LARGE_BOXES
int calc_fill_boost_largebox(int x1, int y1, int x2, int y2, int al, int acb, int acr)
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

int calc_fill_boost(int x1, int y1, int x2, int y2)
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

void *algo_1(void *tdat)
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
#if VW*VH >= 65536
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


	if(debug_output)
	{
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
	}

	//fprintf(stderr, "ALGO DONE %p\n", tdat);

	return tdat;
}

