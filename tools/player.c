#define VIDSCALE 1
#define VIDOUTSCALE 2

#define AUDIO_RING_BUF (256*1024)
#define AUDIO_SKIP_UPPER (32*1024)
#define AUDIO_SKIP_LOWER (28*1024)
#define AUDIO_TARGET_LATENCY (8*1024)

#define SOFTWARE_BLIT_MODE

#ifndef CONST_PREC
#define CONST_PREC 10
#endif
#ifndef CONST_POSTFILT
#define CONST_POSTFILT 140
#endif

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

uint8_t aring_data[AUDIO_RING_BUF];
int aring_beg = 0; // Thread: audio
int aring_end = 0; // Thread: main
SDL_atomic_t aring_used;

uint16_t ocpal[256];
int vidw, vidh;
uint16_t *scrbuf;

uint8_t input_audio[0x10000];
uint8_t dfpwm_tmp_buffer[0x10000];

__attribute__((noreturn))
void abort_msg(const char *msg)
{
	fprintf(stderr, "ERROR: %s\n", msg);
	fflush(stderr);
	abort();
}

int audecmp_fq = 0;
int audecmp_q = 0;
int audecmp_s = 0;
int audecmp_lt = -128;
void au_decompress(int *fq, int *q, int *s, int *lt, int fs, int len, uint8_t *outbuf, uint8_t *inbuf)
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
			ov += 128;
			ov &= 0xFF;
			*(outbuf++) = ov;

			*lt = t;
		}
	}
}

int has_fired = 0;
void authread(void *ud, Uint8 *stream, int len)
{
	// Clear buf
	memset(stream, 128, len);
	/*
	stream[0] = 255;
	stream[1] = 255;
	stream[2] = 255;
	stream[3] = 255;
	*/

	// Check how much of the buffer is available
	int bufsz = SDL_AtomicGet(&aring_used);
	//fprintf(stderr, "tick %d aring %d\n", len, aring_used);

	// If we haven't fired, we need to wait for the buffer to fill)
	if(!has_fired)
	{
		//if(bufsz <= 16384) return;

		has_fired = 1;
	}

	// If there's nothing, copy nothing
	if(bufsz <= 0)
	{
		has_fired = 0;
		return;
	}

	// Clamp to output buffer size
	int prebufsz = bufsz;
	if(bufsz > len)
		bufsz = len;
	int realbufsz = bufsz;

	// Copy
	Uint8 *sout = stream;
	if(aring_beg + bufsz >= AUDIO_RING_BUF)
	{
		memcpy(sout, aring_data + aring_beg, (AUDIO_RING_BUF - aring_beg));
		int bufdec = AUDIO_RING_BUF - aring_beg;
		bufsz -= bufdec;
		sout += bufdec;
		aring_beg = 0;
	}

	assert(bufsz >= 0);

	// Copy remainder
	if(bufsz > 0)
	{
		memcpy(sout, aring_data + aring_beg, bufsz);
		aring_beg += bufsz;
	}

	// advance + decrement used space
	if(prebufsz >= AUDIO_SKIP_UPPER)
	{
		int skip_amt = prebufsz+AUDIO_SKIP_UPPER-AUDIO_SKIP_LOWER;
		aring_beg = (aring_beg + skip_amt) % AUDIO_RING_BUF;
		realbufsz += skip_amt;
	}

	realbufsz = SDL_AtomicAdd(&aring_used, -realbufsz);
	//fprintf(stderr, "honk %d\n", realbufsz);

	// ensure this is correct
	assert(realbufsz >= 0);

	/*
	stream[0] = 255;
	stream[1] = 255;
	stream[2] = 255;
	stream[3] = 255;
	*/
}

void pal_to_rgb(int pal, int *r, int *g, int *b)
{
	assert(pal >= 0 && pal <= 255);

	if(pal < 16)
	{
#define GREY_RAMP 64
		*r = *g = *b = ((pal+1)*255)/(GREY_RAMP-1);
		//fprintf(stderr, "%i %02X\n", pal, *r);

	} else {
		pal -= 16;
		*g = ((pal%8)*255)/7;
		*r = (((pal/8)%6)*255)/5;
		*b = (((pal/8)/6)*255)/4;

	}

	assert(*r >= 0 && *r <= 255);
	assert(*g >= 0 && *g <= 255);
	assert(*b >= 0 && *b <= 255);
}

void do_mocomp(int mocx, int mocy)
{
	int y;

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

#else
		SDL_Rect srcrect = {.x=(mocx<0?-mocx:0), .y=(mocy<0?-mocy:0),
			.w=mocw*VIDSCALE, .h=moch*VIDSCALE};
		SDL_Rect dstrect = {.x=(mocx>0? mocx:0)*VIDSCALE, .y=(mocy>0? mocy:0)*VIDSCALE,
			.w=mocw*VIDSCALE, .h=moch*VIDSCALE};

		SDL_RenderCopy(renderer, texlist[curtex], &srcrect, &dstrect);
#endif
	}
}

int main(int argc, char *argv[])
{
	int x, y, i;

	// Open file
	FILE *fp = fopen(argv[1], "rb");

	fprintf(stderr, "File open %s %p\n", argv[1], fp);

	// Read header
	if(fgetc(fp) != 'I') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != 'C') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != 'E') abort_msg("not a valid ICE2 video");
	if(fgetc(fp) != '2') abort_msg("not a valid ICE2 video");

	int fcls = fgetc(fp);
	if(fcls != 0x01) abort_msg("format class not supported");

	int fscls = fgetc(fp);
	if(fscls != 0x01 && fscls != 0x02) abort_msg("format subclass not supported");

	int fver = fgetc(fp);
	fver |= fgetc(fp)<<8;
	if(fver != 0x01) abort_msg("format version not supported");

	vidw = fgetc(fp);
	vidw |= fgetc(fp)<<8;
	vidh = fgetc(fp);
	vidh |= fgetc(fp)<<8;
	if(vidw < 1) abort_msg("invalid width");
	if(vidh < 1) abort_msg("invalid height");

	// For OC mode, use double-height to get proper aspect ratio
	if(fscls == 0x01)
	{
		vidh *= 2;

		// Upscale further anyway
		vidw *= 4;
		vidh *= 4;
	}

	int fps = fgetc(fp);
	if(fps < 1) abort_msg("invalid framerate");

	fgetc(fp); fgetc(fp); fgetc(fp);

	fprintf(stderr, "size: %i x %i\n", vidw, vidh);
	fprintf(stderr, "framerate: %i FPS\n", fps);

	// Read audio data
	int aufmt = fgetc(fp);
	int auchns = fgetc(fp);
	if(aufmt != 0x00 && aufmt != 0x01 && aufmt != 0x0A)
		abort_msg("ERROR: only U8 or DFPWM1a supported");
	if(auchns != (aufmt == 0 ? 0 : 1)) abort_msg("ERROR: only mono audio supported");
	fgetc(fp); fgetc(fp);

	int aufreq = fgetc(fp);
	aufreq |= fgetc(fp)<<8;
	aufreq |= fgetc(fp)<<16;
	aufreq |= fgetc(fp)<<24;
	assert(aufreq >= 0);

	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);
	fgetc(fp); fgetc(fp); fgetc(fp); fgetc(fp);

	fprintf(stderr, "Header loaded, setting up SDL + audio\n");

	// Init SDL
	if(aufmt != 0)
	{
		SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_AUDIO);

		SDL_AtomicSet(&aring_used, 0);
		SDL_AudioSpec auspec;
		memset(&auspec, 0, sizeof(SDL_AudioSpec));
		fprintf(stderr, "output freq = %i\n", aufreq);
		fprintf(stderr, "channels = %i\n", auchns);
		auspec.freq = aufreq;
		auspec.format = AUDIO_U8;
		auspec.channels = auchns;
		auspec.samples = 2048;
		auspec.callback = authread;
		int err = SDL_OpenAudio(&auspec, NULL);
		fprintf(stderr, "open audio err: %d\n", err);
		assert(err >= 0);
		SDL_PauseAudio(0);

	} else {
		fprintf(stderr, "No audio\n");
		SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO);
	}

	fprintf(stderr, "Setting up palette\n");

	// Set up OC palette if fmt subclass is 0x01
	if(fscls == 0x01)
	{
		for(i = 0; i < 256; i++)
		{
			int r, g, b;
			pal_to_rgb(i, &r, &g, &b);

			r >>= 3;
			g >>= 2;
			b >>= 3;

			ocpal[i] = (r<<11)|(g<<5)|b;
		}
	}

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
	scrbuf = malloc(vidw*vidh*2);
	memset(scrbuf, 0, vidw*vidh*2);
#endif

	// Now draw things!
	int fgcol = 0;
	int bgcol = 0;
	for(;;)
	{
#ifndef SOFTWARE_BLIT_MODE
		// Set render target
		curtex ^= 1;
		SDL_SetRenderTarget(renderer, texlist[curtex]);
		curtex ^= 1;

		// Do straight copy
		SDL_RenderCopy(renderer, texlist[curtex], NULL, NULL);
#endif

		int mocx = 0, mocy = 0;
		if(fscls != 0x01)
		{
			// Get mocomp info
			mocx = (int)(int8_t)fgetc(fp);
			mocy = (int)(int8_t)fgetc(fp);
			do_mocomp(mocx, mocy);
		}


#ifndef SOFTWARE_BLIT_MODE
		curtex ^= 1;
#endif

		// Draw rectangles
		//fprintf(stderr, "begrect %i %i\n", mocx, mocy);
		for(;;)
		{
			int offs, rw, rh, rx, ry;
			uint16_t col;

			if(fscls == 0x01)
			{
				// OC mode
				rh = fgetc(fp);
				assert(rh > 0);
				if(rh == 0xFF) break;

				if((rh & 0x80) != 0)
				{
					// Get mocomp info
					// XXX: this doesn't use the full featureset of a copy!
					// (it assumes largest possible rect for a given mocomp)
					fgetc(fp);
					fgetc(fp);
					fgetc(fp);
					int mocx = (int)(int8_t)fgetc(fp);
					int mocy = (int)(int8_t)fgetc(fp);
					do_mocomp(mocx*4, mocy*2*4);
					continue;
				}

				// Get remainder of rect
				ry = fgetc(fp);
				rw = fgetc(fp);
				rx = fgetc(fp);

				// Get colour
				if((rh & 0x40) != 0)
				{
					if((ry & 0x40) != 0) bgcol = fgetc(fp);
					col = ocpal[bgcol];
				} else {
					if((ry & 0x40) != 0) fgcol = fgetc(fp);
					col = ocpal[fgcol];
				}

				// Clear flag bits
				ry &= ~0x40;
				rh &= ~0x40;

				// Double-height
				rh *= 2;
				ry *= 2;

				// Further upscaling
				rx *= 4;
				ry *= 4;
				rw *= 4;
				rh *= 4;

				// Get offset
				offs = rx + ry*vidw;

				assert(offs >= 0);
				assert(offs < vidw*vidh);
				assert(rx >= 0);
				assert(ry >= 0);
				assert(rx+rw <= vidw);
				assert(ry+rh <= vidh);

			} else {
				// GBA+RPi mode

				// Get dims
				if(vidh >= 248 || vidw >= 256) {
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
					assert(rh > 0);
					rw = fgetc(fp);
					if(rh == 0xFF) break;
					assert(rw > 0);
				}
				assert(rw <= vidw);
				assert(rh <= vidh);

				// Get offs
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
				rx = offs%vidw;
				ry = offs/vidw;
				//fprintf(stderr, "%i %i %i %i %i\n", rx, ry, rw, rh, offs);
				assert(rx+rw <= vidw);
				assert(ry+rh <= vidh);

				// Get colour
				col = fgetc(fp);
				col |= fgetc(fp)<<8;

				// Convert to 16bpp + R/B swap
				col = (col & 0x001F)|((col & ~0x001F)<<1);
				col = (col & 0x07E0)|(col>>11)|(col<<11);
			}

#ifdef SOFTWARE_BLIT_MODE
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
			int cr = ((col>>11)&31)<<3;
			int cg = ((col>>5)&63)<<2;
			int cb = ((col>>0)&31)<<3;

			// Draw rect
			SDL_Rect r = {.x=rx*VIDSCALE, .y=ry*VIDSCALE, .w=rw*VIDSCALE, .h=rh*VIDSCALE};
			SDL_SetRenderDrawColor(renderer, cr, cg, cb, SDL_ALPHA_OPAQUE);
			SDL_RenderFillRect(renderer, &r);
#endif
		}

		// Parse audio
		if(aufmt != 0)
		{
			int ablen = fgetc(fp);
			ablen |= fgetc(fp)<<8;
			assert(ablen >= 0);

			if(ablen != 0)
			{
				fread(input_audio, ablen, 1, fp);
				uint8_t *insrc = input_audio;

				if(aufmt == 0x0A)
				{
					// Decompress DFPWM1a
					memcpy(dfpwm_tmp_buffer, input_audio, ablen);
					assert(ablen <= 0x2000);
					au_decompress(&audecmp_fq, &audecmp_q, &audecmp_s,
						&audecmp_lt, CONST_POSTFILT,
						ablen, input_audio, dfpwm_tmp_buffer);
					ablen <<= 3;
				}

				int realablen = ablen;

				if(ablen >= AUDIO_RING_BUF - aring_end)
				{
					// Read until buffer end
					int bufdec = (AUDIO_RING_BUF - aring_end);
					memcpy(aring_data + aring_end, insrc, bufdec);
					insrc += bufdec;

					// Wrap values
					ablen -= bufdec;
					aring_end = 0;
				}

				// Read remaining data
				if(ablen > 0)
				{
					memcpy(aring_data + aring_end, insrc, ablen);
					aring_end += ablen;
				}

				// Add to audio ring buffer
				int incr = SDL_AtomicAdd(&aring_used, realablen);
				assert(incr < AUDIO_RING_BUF);

				// Align
				if((realablen & 1) != 0)
					fgetc(fp);
			}
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
		//fprintf(stderr, "%.3f\n", ticks_now/1000.0);

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

