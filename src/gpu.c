#include "common.h"

void gpu_start(void)
{
	gpu_usage_saving = 0;
	gpu_usage = gop_bloat_cost;
	gop_bloat_cost = 0;

	if(gop_bloat > 0)
	{
		assert(!"BUG: this shouldn't happen");
		memmove(&gop[0], &gop[gop_count - gop_bloat], sizeof(gop_s)*gop_bloat);
		gop_count = gop_bloat;
		gop_bloat = 0;

	} else {
		gop_count = 0;
	}

	gpu_pal_fg_fbeg = gpu_pal_fg;
	gpu_pal_bg_fbeg = gpu_pal_bg;
	gpu_pal_next_is_bg_fbeg = gpu_pal_next_is_bg;
	gpu_compact_pal_changes = 0;
}

void gpu_emit(void)
{
	fprintf(stderr, "cost: %5i - compact: %5i - pal improv: %5i - ops: %5i\n", gpu_usage, gpu_usage_saving, gpu_compact_pal_changes, gop_count);
	fflush(stderr);
	gop_count -= gop_bloat;
	gop_bloat = 0;
}

int gpu_compar_palidx(const void *av, const void *bv)
{
	const gop_s *a = (const gop_s *)av;
	const gop_s *b = (const gop_s *)bv;

	return ((int)(b->pal)) - (int)(a->pal);
}

void gpu_fill(int bx, int by, int bw, int bh, int l, int cb, int cr, int pal, int add_op)
{
	int y;

	assert(bw >= 1);
	assert(bh >= 1);
	assert(bx >= 0 && bx < VW);
	assert(by >= 0 && by < VH);
	assert(bx+bw-1 >= 0 && bx+bw-1 < VW);
	assert(by+bh-1 >= 0 && by+bh-1 < VH);

	int cost = 0;

	if(add_op && pal != gpu_pal_fg && pal != gpu_pal_bg)
	{
		if(gpu_pal_next_is_bg)
		{
			gpu_pal_bg = pal;
			cost += COST_BG;

		} else {
			gpu_pal_fg = pal;
			cost += COST_FG;

		}

		gpu_pal_next_is_bg = !gpu_pal_next_is_bg;

	}

	assert(bx >= 0 && bx+bw <= VW);
	for(y = by; y < bh+by; y++)
	{
		assert(y >= 0 && y < VH);

		memset(&rawcurbuf[0][y][bx], l, bw);
		memset(&rawcurbuf[1][y][bx], cb, bw);
		memset(&rawcurbuf[2][y][bx], cr, bw);
		int x;
		for(x = bx; x < bx+bw; x++)
			rawcurbuf_pal[y][x] = pal;
	}

	if(!add_op) return;

#ifdef PIXEL15
	//cost += (bh*bw <= 2 ? COST_SET : bw <= 8 && bh <= 8 ? COST_FILL-1 : COST_FILL);
	//cost += (bh*bw <= 2 ? COST_SET : COST_FILL);
	cost += COST_FILL;
#else
	cost += (bh == 1 ? COST_SET : COST_FILL);
#endif
	gpu_usage += cost;

	assert(gop_count < GOP_MAX);

	gop_s *g = &gop[gop_count++];
	g->op = (pal == gpu_pal_fg ? GOP_FILL_FG : GOP_FILL_BG);
	g->bx = bx; g->bw = bw;
	g->by = by; g->bh = bh;
	g->l  = l;
	g->cb = cb;
	g->cr = cr;
	g->pal = pal;
	g->cost = cost;
}

void gpu_copy(int bx, int by, int bw, int bh, int dx, int dy, int add_op)
{
	int x, y, nx, ny;

	assert(bw >= 1);
	assert(bh >= 1);
	assert(dx != 0 || dy != 0);

	x = bx;
	nx = bx+dx;
	assert(x >= 0 && x+bw <= VW);
	assert(nx >= 0 && nx+bw <= VW);

	if(dy < 0)
	{
		for(y = by, ny = by+dy; y < bh+by; y++, ny++)
		{
			assert(y >= 0 && y < VH);
			assert(ny >= 0 && ny < VH);

			memmove(&rawcurbuf[0][ny][nx], &rawcurbuf[0][y][x], bw);
			memmove(&rawcurbuf[1][ny][nx], &rawcurbuf[1][y][x], bw);
			memmove(&rawcurbuf[2][ny][nx], &rawcurbuf[2][y][x], bw);
			memmove(&rawcurbuf_pal[ny][nx], &rawcurbuf_pal[y][x], bw*sizeof(pixeltyp));
		}

	} else {
		for(y = by+bh-1, ny = by+bh-1+dy; y >= by; y--, ny--)
		{
			assert(y >= 0 && y < VH);
			assert(ny >= 0 && ny < VH);

			memmove(&rawcurbuf[0][ny][nx], &rawcurbuf[0][y][x], bw);
			memmove(&rawcurbuf[1][ny][nx], &rawcurbuf[1][y][x], bw);
			memmove(&rawcurbuf[2][ny][nx], &rawcurbuf[2][y][x], bw);
			memmove(&rawcurbuf_pal[ny][nx], &rawcurbuf_pal[y][x], bw*sizeof(pixeltyp));
		}
	}

	if(!add_op) return;

	gpu_usage += COST_COPY;

	assert(gop_count < GOP_MAX);

	gop_s *g = &gop[gop_count++];
	g->op = GOP_COPY;
	g->bx = bx+dx; g->bw = bw;
	g->by = by+dy; g->bh = bh;
	g->rsx = bx;
	g->rsy = by;
	g->cost = COST_COPY;
}

