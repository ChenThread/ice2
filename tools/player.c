#define VIDSCALE 1
#define VIDOUTSCALE 1

#define SOFTWARE_BLIT_MODE

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <unistd.h>

#include <SDL.h>

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
#ifdef SOFTWARE_BLIT_MODE
SDL_Texture *texblit = NULL;
#else
SDL_Texture *texlist[2] = {NULL, NULL};
int curtex = 0;
#endif


__attribute__((noreturn))
void abort_msg(const char *msg)
{
	fprintf(stderr, "ERROR: %s\n", msg);
	fflush(stderr);
	abort();
}

int main(int argc, char *argv[])
{
	int x, y;

	// Open file
	FILE *fp = fopen(argv[1], "rb");

	// Read header
	if(fgetc(fp) != 'I') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != 'C') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != 'E') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != '2') abort_msg("not a valid ICE2 video");

	int fcls = fgetc(fp);
	if(fcls != 0x01) abort_msg("format class not supported");

	// TODO: OC format
	int fscls = fgetc(fp);
	if(fscls != 0x02) abort_msg("format subclass not supported");

	int fver = fgetc(fp);
	fver |= fgetc(fp)<<8;
	if(fver != 0x01) abort_msg("format version not supported");

	int vidw = fgetc(fp);
	vidw |= fgetc(fp)<<8;
	int vidh = fgetc(fp);
	vidh |= fgetc(fp)<<8;
	if(vidw < 1) abort_msg("invalid width");
	if(vidh < 1) abort_msg("invalid height");

	int fps = fgetc(fp);
	if(fps < 1) abort_msg("invalid framerate");

	fgetc(fp); fgetc(fp); fgetc(fp);

	fprintf(stderr, "size: %i x %i\n", vidw, vidh);
	fprintf(stderr, "framerate: %i FPS\n", fps);

	// Read audio data
	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);
	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);
	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);
	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);

	// TODO: audio
	SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);

	// Set up window + renderer
	window = SDL_CreateWindow("- intelligent cirno experience - player -"
		, SDL_WINDOWPOS_UNDEFINED
		, SDL_WINDOWPOS_UNDEFINED
		, vidw*VIDSCALE*VIDOUTSCALE, vidh*VIDSCALE*VIDOUTSCALE
		, 0);
	renderer = SDL_CreateRenderer(window, -1,
		SDL_RENDERER_SOFTWARE);

#ifdef SOFTWARE_BLIT_MODE
	// Set up texture for streaming
	texblit = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_RGB565,
		SDL_TEXTUREACCESS_STREAMING,
		vidw, vidh);
#else
	// Set up textures as render targets
	texlist[0] = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_UNKNOWN,
		SDL_TEXTUREACCESS_TARGET,
		vidw*VIDSCALE, vidh*VIDSCALE);
	texlist[1] = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_UNKNOWN,
		SDL_TEXTUREACCESS_TARGET,
		vidw*VIDSCALE, vidh*VIDSCALE);
#endif

	Uint32 ticks_now = SDL_GetTicks();
	Uint32 ticks_later = ticks_now;
	int ticks_resid = 0;
	int ticks_resid_inc = 1000000/fps;
#ifdef SOFTWARE_BLIT_MODE
	uint16_t *scrbuf = malloc(vidw*vidh*2);
	memset(scrbuf, 0, vidw*vidh*2);
#endif
	// Now draw things!
	for(;;)
	{
		// Get mocomp info
		int mocx = (int)(int8_t)fgetc(fp);
		int mocy = (int)(int8_t)fgetc(fp);

#ifndef SOFTWARE_BLIT_MODE
		// Set render target
		curtex ^= 1;
		SDL_SetRenderTarget(renderer, texlist[curtex]);
		curtex ^= 1;

		// Do straight copy
		SDL_RenderCopy(renderer, texlist[curtex], NULL, NULL);
#endif

		// Do mocomp copy
		if(mocx != 0 || mocy != 0)
		{
			int mocw = vidw-(mocx<0?-mocx:mocx);
			int moch = vidh-(mocy<0?-mocy:mocy);

#ifdef SOFTWARE_BLIT_MODE
			int srcx=(mocx<0?-mocx:0);
			int srcy=(mocy<0?-mocy:0);
			int dstx=(mocx>0? mocx:0);
			int dsty=(mocy>0? mocy:0);

			if(mocy < 0)
			{
				for(y = 0; y < moch; y++)
				{
					memcpy(
						scrbuf + dstx + (y+dsty)*vidw,
						scrbuf + srcx + (y+srcy)*vidw,
						mocw*2);
				}

			} else if(mocy > 0) {
				for(y = moch-1; y >= 0; y--)
				{
					memcpy(
						scrbuf + dstx + (y+dsty)*vidw,
						scrbuf + srcx + (y+srcy)*vidw,
						mocw*2);
				}
			} else {
				for(y = 0; y < moch; y++)
				{
					memmove(
						scrbuf + dstx + (y+dsty)*vidw,
						scrbuf + srcx + (y+srcy)*vidw,
						mocw*2);
				}

			}

			// TODO: copy
#else
			SDL_Rect srcrect = {.x=(mocx<0?-mocx:0), .y=(mocy<0?-mocy:0),
				.w=mocw*VIDSCALE, .h=moch*VIDSCALE};
			SDL_Rect dstrect = {.x=(mocx>0? mocx:0)*VIDSCALE, .y=(mocy>0? mocy:0)*VIDSCALE,
				.w=mocw*VIDSCALE, .h=moch*VIDSCALE};

			SDL_RenderCopy(renderer, texlist[curtex], &srcrect, &dstrect);
#endif
		}

#ifndef SOFTWARE_BLIT_MODE
		curtex ^= 1;
#endif

		// Draw rectangles
		for(;;)
		{
			int rw, rh;

			// Get dims
			if(vidh >= 248 || vidw >= 256)
			{
				// 16-bit size mode
				rh = fgetc(fp);
				rh |= fgetc(fp)<<8;
				assert(rh > 0);
				if((rh>>8) == 0xFF) break;
				rw = fgetc(fp);
				rw |= fgetc(fp)<<8;
				assert(rw > 0);

			} else {
				// 8-bit size mode
				rh = fgetc(fp);
				rw = fgetc(fp);
				assert(rh > 0);
				assert(rw > 0);
				if(rh == 0xFF) break;
			}

			// Get offs
			int offs;
			offs = fgetc(fp);
			offs |= fgetc(fp)<<8;
			if(vidw*vidh >= 0x10000)
			{
				// 32-bit offset mode
				offs |= fgetc(fp)<<16;
				offs |= fgetc(fp)<<24;
			}
			assert(offs >= 0);
			assert(offs < vidw*vidh);

			// Turn offs into coords
			int rx = offs%vidw;
			int ry = offs/vidw;
			assert(rx+rw <= vidw);
			assert(ry+rh <= vidh);

			// Get colour
			uint16_t col = fgetc(fp);
			col |= fgetc(fp)<<8;

#ifdef SOFTWARE_BLIT_MODE
			// Convert to 16bpp + R/B swap
			col = (col & 0x001F)|((col & ~0x001F)<<1);
			col = (col & 0x07E0)|(col>>11)|(col<<11);

			// Expand to 32 bits
			uint32_t col32 = col;
			col32 |= col32<<16;

			// Do left border
			if(offs & 1)
			{
				for(y = 0; y < rh; y++)
					scrbuf[offs+y*vidw] = col;
				offs++;
				rw--;
			}

			// Do right border
			if(rw & 1)
			{
				for(y = 0; y < rh; y++)
					scrbuf[offs+(rw-1)+y*vidw] = col;
				rw--;
			}

			// Do centre
			uint32_t *p = (uint32_t *)&scrbuf[offs];
			for(y = 0; y < rh; y++, p += (vidw>>1))
			{
				for(x = 0; x < (rw>>1); x++)
					p[x] = col32;
			}

#else
			// Split into RGB
			int cb = ((col>>10)&31)<<3;
			int cg = ((col>>5)&31)<<3;
			int cr = ((col>>0)&31)<<3;

			// Draw rect
			SDL_Rect r = {.x=rx*VIDSCALE, .y=ry*VIDSCALE, .w=rw*VIDSCALE, .h=rh*VIDSCALE};
			SDL_SetRenderDrawColor(renderer, cr, cg, cb, SDL_ALPHA_OPAQUE);
			SDL_RenderFillRect(renderer, &r);
#endif
		}

		// Blit
#ifdef SOFTWARE_BLIT_MODE
		uint16_t *targ = NULL;
		int pitch = 0;
		SDL_LockTexture(texblit, NULL, (void **)&targ, &pitch);
		assert(targ != NULL);
		if(pitch == vidw*2)
		{
			memcpy(targ, scrbuf, 2*vidw*vidh);
		} else {
			for(y = 0; y < vidh; y++)
				memcpy(targ + y*(pitch>>1), scrbuf + y*vidw, 2*vidw);
		}
		SDL_UnlockTexture(texblit);
		SDL_RenderCopy(renderer, texblit, NULL, NULL);
#else
		SDL_SetRenderTarget(renderer, NULL);
		SDL_RenderCopy(renderer, texlist[curtex], NULL, NULL);
#endif
		SDL_RenderPresent(renderer);

		// Wait
		ticks_resid += ticks_resid_inc;
		ticks_later += (ticks_resid/10000)*10;
		ticks_resid %= 10000;
		ticks_now = SDL_GetTicks();
		if((int32_t)(ticks_later - ticks_now) >= 10)
			SDL_Delay(ticks_later - ticks_now);

		// Poll
		int doret = 0;
		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		switch(ev.type)
		{
			case SDL_QUIT:
				doret = 1;
				break;
		}

		if(doret) break;
	}

#ifdef SOFTWARE_BLIT_MODE
	free(scrbuf);
#endif
	return 0;
}

