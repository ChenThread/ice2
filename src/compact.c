#include "common.h"

void gpu_compact(void)
{
	int ptr = 0;
	static int16_t boxres[VH][VW];
	static uint8_t op_is_ok[GOP_MAX];
#if VW > 255 || VH > 255
	static uint16_t op_dims[GOP_MAX][4];
#else
	static uint8_t op_dims[GOP_MAX][4];
#endif
	int x, y;
	gop_s *g;

	// see if any ops have any importance
	//
	// FIXME: this will break once we have copies in place
	// they may need to shift a location before it gets shat on!
	memset(boxres, 0xFF, VW*VH*2);
	for(ptr = 0, g = &gop[0]; ptr < gop_count; ptr++, g++)
	{
		assert(g->bx >= 0);
		assert(g->by >= 0);
		assert(g->bx + g->bw <= VW);
		assert(g->by + g->bh <= VH);

		for(y = g->by; y < g->by+g->bh; y++)
		for(x = g->bx; x < g->bx+g->bw; x++)
			boxres[y][x] = ptr;
	}

	// now check if all of these ops haven't been screwed over
	memset(op_is_ok, 0, GOP_MAX);
	for(y = 0; y < VH; y++)
	for(x = 0; x < VW; x++)
	{
		ptr = boxres[y][x];

		if(ptr == 0xFFFF)
			continue;

		if(op_is_ok[ptr] == 0)
		{
			op_is_ok[ptr] = 1;
			op_dims[ptr][0] = x;
			op_dims[ptr][1] = y;
			op_dims[ptr][2] = x;
			op_dims[ptr][3] = y;
		} else {
			if(op_dims[ptr][0] > x) op_dims[ptr][0] = x;
			if(op_dims[ptr][1] > y) op_dims[ptr][1] = y;
			if(op_dims[ptr][2] < x) op_dims[ptr][2] = x;
			if(op_dims[ptr][3] < y) op_dims[ptr][3] = y;
		}
	}

	// update op colours
	for(ptr = 0, g = &gop[0]; ptr < gop_count; ptr++, g++)
	{
		if(op_is_ok[ptr])
		{
			// get dims
			if(g->op == GOP_COPY) continue;

			g->bx = op_dims[ptr][0];
			g->by = op_dims[ptr][1];
			g->bw = op_dims[ptr][2] - g->bx + 1;
			g->bh = op_dims[ptr][3] - g->by + 1;

			// converge closer for fills
			if(g->op == GOP_FILL_FG || g->op == GOP_FILL_BG)
			{
				int al = 0;
				int acb = 0;
				int acr = 0;
				int acount = 0;

				// recalc if we have to
				// XXX: it's entirely likely that this actually makes things worse.
				if(op_is_ok[ptr] == 2)
				{
					op_is_ok[ptr] = 0;

					assert(g->bx+g->bw <= VW);
					assert(g->by+g->bh <= VH);

					for(y = g->by; y < g->by+g->bh; y++)
					for(x = g->bx; x < g->bx+g->bw; x++)
					{
						ptr = boxres[y][x];

						if(ptr == 0xFFFF)
							continue;

						if(op_is_ok[ptr] == 0)
						{
							op_is_ok[ptr] = 1;
							op_dims[ptr][0] = x;
							op_dims[ptr][1] = y;
							op_dims[ptr][2] = x;
							op_dims[ptr][3] = y;
						} else {
							if(op_dims[ptr][0] > x) op_dims[ptr][0] = x;
							if(op_dims[ptr][1] > y) op_dims[ptr][1] = y;
							if(op_dims[ptr][2] < x) op_dims[ptr][2] = x;
							if(op_dims[ptr][3] < y) op_dims[ptr][3] = y;
						}
					}

					// if we don't have anything in this region, destroy this op
					if(!op_is_ok[ptr])
					{
						fprintf(stderr, "DEMOTED\n");
						continue;
					}
				}

				// sum + take count
				for(y = 0; y < g->bh; y++)
				for(x = 0; x < g->bw; x++)
				{
					if(boxres[y+g->by][x+g->bx] == ptr)
					{
						al  += rawinbuf[0][y+g->by][x+g->bx];
						acb += rawinbuf[1][y+g->by][x+g->bx];
						acr += rawinbuf[2][y+g->by][x+g->bx];

						acount++;
					}
				}

				// ensure our math is correct
				assert(acount > 0);

				// average
				al  = (al  + (acount/2)) / acount;
				acb = (acb + (acount/2)) / acount;
				acr = (acr + (acount/2)) / acount;

				// get new colour
				int ar, ag, ab, apal;
				from_ycbcr(al, acb, acr, &ar, &ag, &ab);
				apal = rgb_to_pal(&ar, &ag, &ab);

				// if different, set new colour
				if(apal != g->pal)
				{
					g->pal = apal;
					to_ycbcr(ar, ag, ab, &al, &acb, &acr);
					g->l = al;
					g->cb = acb;
					g->cr = acr;
					gpu_compact_pal_changes++;
					for(y = 0; y < g->bh; y++)
					for(x = 0; x < g->bw; x++)
					{
						int b = boxres[y+g->by][x+g->bx];
						if(boxres[y+g->by][x+g->bx] == ptr)
						{
							rawcurbuf[0][y+g->by][x+g->bx] = al;
							rawcurbuf[1][y+g->by][x+g->bx] = acb;
							rawcurbuf[2][y+g->by][x+g->bx] = acr;
							rawcurbuf_pal[y+g->by][x+g->bx] = apal;
						} else if(0 && gop[b].pal == g->pal) {
							//fprintf(stderr, "demote %i %i %i %i\n" , ptr, b , x+g->bx, y+g->by);

							boxres[y+g->by][x+g->bx] = ptr;

							if(op_is_ok[b])
								op_is_ok[b] = 2;

						}
					}
				}

			}
		}
	}

	// delete ops that are declared "useless"
	// while creating new boxes for the "useful" ops
	int delete_count = 0;

	for(ptr = 0, g = &gop[0]; ptr < gop_count; ptr++, g++)
	{
		//int lbx = g->bx;
		//int lby = g->by;

		if(!op_is_ok[ptr])
		{
			// XXX: how do we optimise this?
			delete_count++;
			memmove(g, g+1, sizeof(gop_s)*(gop_count-(ptr+1)));
			g--;
		}
	}

	gop_count -= delete_count;

	// move relatively full fill ops to end
#if COST_FG > 0 || COST_BG > 0
	gop_s tmpg;
	int full_peak = gop_count;
	int full_groups = 0;
	for(;;)
	{
		int full_count = 0;
		for(ptr = 0, g = &gop[0]; ptr < full_peak - full_count; ptr++, g++)
		{
			int ct = 0;
			for(y = 0; y < g->bh; y++)
			for(x = 0; x < g->bw; x++)
			if(boxres[y+g->by][x+g->bx] == ptr || boxres[y+g->by][x+g->bx] >= full_peak)
				ct++;

			if(ct == g->bw*g->bh)
			{
				// update stats
				full_count++;

				// swap
				memmove(&tmpg, &gop[full_peak-full_count], sizeof(gop_s));
				memmove(&gop[full_peak-full_count], g, sizeof(gop_s));
				memmove(g, &tmpg, sizeof(gop_s));
			}
		}

		// sort ops
		if(full_count >= 2)
			qsort(gop + full_peak - full_count, full_count, sizeof(gop_s), gpu_compar_palidx);

		// recalc boxres
		memset(boxres, 0xFF, VW*VH*2);
		for(ptr = 0, g = &gop[0]; ptr < gop_count; ptr++, g++)
		{
			assert(g->bx >= 0);
			assert(g->by >= 0);
			assert(g->bx + g->bw <= VW);
			assert(g->by + g->bh <= VH);

			for(y = g->by; y < g->by+g->bh; y++)
			for(x = g->bx; x < g->bx+g->bw; x++)
				boxres[y][x] = ptr;
		}

		// advance full peak
		full_peak -= full_count;
		full_groups++;

		// TODO: more than one iteration tends to yield bad results
		// basically, a lot of BLOAT messages w/ no real op count improvement
		//if(full_count <= 0)
			break;
	}
#endif

	//fprintf(stderr, "full groups: %3i - peak: %3i/%3i\n", full_groups, gop_count-full_peak, gop_count);

	// recount costs
	gpu_pal_fg = gpu_pal_fg_fbeg;
	gpu_pal_bg = gpu_pal_bg_fbeg;
	gpu_pal_next_is_bg = gpu_pal_next_is_bg_fbeg;
	int gpu_usage_pre = gpu_usage;
	gpu_usage = 0;
	for(ptr = 0, g = &gop[0]; ptr < gop_count; ptr++, g++)
	{
		switch(g->op)
		{
			case GOP_COPY:
				g->cost = COST_COPY;
				break;

			case GOP_FILL_FG:
			case GOP_FILL_BG:
#ifdef PIXEL15
				//g->cost = (g->bh*g->bw <= 2 ? COST_SET
					//: g->bw <= 8 && g->bh <= 8 ? COST_FILL-1 : COST_FILL);
				//g->cost = (g->bh*g->bw <= 2 ? COST_SET : COST_FILL);
				g->cost = COST_FILL;
#else
				g->cost = (g->bh == 1 ? COST_SET : COST_FILL);
#endif

				if(g->pal == gpu_pal_fg)
					g->op = GOP_FILL_FG;
				else if(g->pal == gpu_pal_bg)
					g->op = GOP_FILL_BG;
				else {
					if(gpu_pal_next_is_bg)
					{
						g->op = GOP_FILL_BG;
						g->cost += COST_BG;
						gpu_pal_bg = g->pal;

					} else {
						g->op = GOP_FILL_FG;
						g->cost += COST_FG;
						gpu_pal_fg = g->pal;
					}

					gpu_pal_next_is_bg = !gpu_pal_next_is_bg;

				}

				break;

			default:
				fprintf(stderr, "EDOOFUS: INVALID GOP\n");
				abort();
				break;
		}

		gpu_usage += g->cost;

		if(gpu_usage > GPU_BUDGET_HARD_MAX)
		{
			gop_bloat++;
			gop_bloat_cost += g->cost;

		}
	}

	int gpu_usage_post = gpu_usage;
	//fprintf(stderr, "gpu usage: %i -> %i - ops: %i - old-saving: %i\n", gpu_usage_pre, gpu_usage_post, gop_count, gpu_usage_saving);
	gpu_usage_saving += gpu_usage_pre - gpu_usage_post;
	// FIXME: sometimes happens
	//assert(gpu_usage_post <= gpu_usage_pre);
	if(!(gpu_usage_post <= gpu_usage_pre))
	{
		fprintf(stderr, "BLOAT! %i -> %i - ops: %i - op skip: %i\n", gpu_usage_pre, gpu_usage_post, gop_count, gop_bloat);
		fflush(stderr);
		//assert(gpu_usage <= GPU_BUDGET_HARD_MAX);

	}
}

