// REQUIRES AN SSSE3-CAPABLE X86 OR AMD64 CPU
// (might not quite be right but you SHOULD have at LEAST SSSE3)

#include "common.h"

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
uint8_t rawinbuf[3][VH][VW];
uint8_t precurbuf[3][VH][VW];
uint8_t rawcurbuf[3][VH][VW];

uint8_t rawlastbuf_mvec[3][VH][VW];
uint8_t rawinbuf_mvec[3][VH][VW];

pixeltyp rawinbuf_new_pal[VH][VW];
pixeltyp rawinbuf_pal[VH][VW];
pixeltyp precurbuf_pal[VH][VW];
pixeltyp rawcurbuf_pal[VH][VW];

gop_s gop[GOP_MAX];
int gop_count = 0;
int gop_bloat = 0;
int gop_bloat_cost = 0;

const char *fname_in = NULL;
const char *fname_out = NULL;

int main(int argc, char *argv[])
{
	int x, y, i;
	int err;
	int no_audio = 0;

	for(i = 1; i < argc; i++)
	{
		if(!strcmp("-o", argv[i]))
		{
			i++; assert(i < argc);

			assert(fname_out == NULL);
			fname_out = argv[i];

			fp = fopen(fname_out, "wb");
#ifndef PIXEL15
			assert(VH < 60);
#endif

			if(fp == NULL)
			{
				perror("fopen");
				return 1;
			}

		} else if(!strcmp("-d", argv[i])) {
			fp = stdout;

		} else if(!strcmp("-i", argv[i])) {
			i++; assert(i < argc);

			assert(fname_in == NULL);
			fname_in = argv[i];

		} else if(!strcmp("-an", argv[i])) {
			no_audio = 1;

		} else {
			fprintf(stderr, "error: unexpected argument '%s'\n", argv[i]);
			fflush(stderr);
			abort();
		}
	}

	assert(fname_in != NULL);

	// set up ffmpeg
	av_register_all();
	avcodec_register_all();

	// open input
	AVFormatContext *format_ctx = NULL;
	err = avformat_open_input(&format_ctx, fname_in, NULL, NULL);
	assert(format_ctx != NULL);
	assert(err == 0);

	format_ctx->fps_probe_size = 512*1024;
	err = avformat_find_stream_info(format_ctx, NULL);
	assert(err == 0);

	av_dump_format(format_ctx, 0, "(input)", 0);

	// open codec
	AVCodec *vcodec = NULL;
	AVCodec *acodec = NULL;
	AVStream *vstream = NULL;
	AVStream *astream = NULL;
	int vstream_idx = -1;
	int astream_idx = -1;
	fprintf(stderr, "streams: %i\n", format_ctx->nb_streams);
	for(i = 0; i < (int)(format_ctx->nb_streams); i++)
	{
		fprintf(stderr, "mtyp: %i = %i\n", i, format_ctx->streams[i]->codec->codec_type);

		if(vstream == NULL)
		{
			vstream = format_ctx->streams[i];

			if(vstream->codec->codec_type != AVMEDIA_TYPE_VIDEO)
			{
				vstream = NULL;
			} else {
				vstream_idx = i;
			}

		}

		if((!no_audio) && astream == NULL)
		{
			astream = format_ctx->streams[i];

			if(astream->codec->codec_type != AVMEDIA_TYPE_AUDIO)
			{
				astream = NULL;
			} else {
				astream_idx = i;
			}

		}
	}
	assert(vstream_idx >= 0);
	assert(vstream_idx < (int)(format_ctx->nb_streams));
	assert(astream_idx >= -1);
	assert(astream_idx < (int)(format_ctx->nb_streams));

	vcodec = avcodec_find_decoder(vstream->codec->codec_id);
	assert(vcodec != NULL);
	fprintf(stderr, "video codec: %s\n", vcodec->name);

	if(astream != NULL)
	{
		acodec = avcodec_find_decoder(astream->codec->codec_id);
		if(acodec != NULL)
		{
			fprintf(stderr, "audio codec: %s\n", acodec->name);
		}
	}

	// set some options
	AVDictionary *vopts = NULL;
	av_dict_set(&vopts, "refcounted_frames", "1", 0);
	AVDictionary *aopts = NULL;
	av_dict_set(&aopts, "refcounted_frames", "1", 0);

	// open codec context
	AVCodecContext *vcodec_ctx = vstream->codec;
	assert(vcodec_ctx != NULL);
	err = avcodec_open2(vcodec_ctx, vcodec, &vopts);
	assert(err == 0);

	AVCodecContext *acodec_ctx = NULL;
	if(astream != NULL)
	{
		acodec_ctx = astream->codec;
		assert(acodec_ctx != NULL);
		err = avcodec_open2(acodec_ctx, acodec, &aopts);
		assert(err == 0);
	}

	// allocate frame
	AVFrame *frame = av_frame_alloc();
	assert(frame != NULL);
	uint8_t *outdata[4] = {NULL, NULL, NULL, NULL};
	int outstride[4] = {0, 0, 0, 0};
	int outbufsz = 0;

	int outfreq = 0;

	// write header
	if(fp != NULL)
	{
		fprintf(fp, "ICE2");

		// format class (0x01 == uncompressed)
		fputc(0x01, fp);

		// format subclass (0x01 == OC, 0x02 == PIXEL15, 0x03 == PIXEL16(TODO?))
#ifdef PIXEL15
		fputc(0x02, fp);
#else
		fputc(0x01, fp);
#endif

		// version for given format class
		fputc(0x01, fp); fputc(0x00, fp);

		// dimensions
		fputc(VW&0xFF, fp); fputc(VW>>8, fp);
		fputc(VH&0xFF, fp); fputc(VH>>8, fp);

		// framerate
		int num = vstream->avg_frame_rate.num;
		int den = vstream->avg_frame_rate.den;
		int fps = (num + (den>>1))/den;
		fprintf(stderr, "FPS: %i (%i/%i)\n", fps, num, den);

		int tnum = vstream->r_frame_rate.num;
		int tden = vstream->r_frame_rate.den;
		int tfps = (tnum + (tden>>1))/tden;
		fprintf(stderr, "real FPS: %i (%i/%i)\n", tfps, tnum, tden);
		//if(fps >= 255)
		if(tfps >= 4 && tfps < 255)
		{
			fps = tfps;
			num = tnum;
			den = tden;
		}

		assert(fps >= 1 && fps <= 255);
		fputc(fps, fp);
		// TODO: enforce 20fps for OC mode

		// reserved
		fputc(0, fp); fputc(0, fp); fputc(0, fp);

		if(acodec_ctx != NULL)
		{
			// audio codec
			// 0x00 = no audio (thus no audio packets)
			// 0x01 = 8-bit unsigned PCM
			// 0x02 = 16-bit signed PCM (TODO)
			// 0x03 = XA-ADPCM knockoff (TODO)
			fputc(0x01, fp);

			// channels
			// TODO: handle more than one channel
			fputc(0x01, fp);

			// reserved
			fputc(0, fp); fputc(0, fp);

			// get corrected output frequency
			// (we're freq-shifting rather than giving the proper FPS)
			// the int64_t spam is because there are vids with REALLY weird FPS fractions
			outfreq = ((int64_t)acodec_ctx->sample_rate*(int64_t)fps*(int64_t)den
				+ (int64_t)num-1)/(int64_t)num;

			// audio frequency
			fprintf(stderr, "audio: %i, %i Hz\n", acodec_ctx->sample_fmt, acodec_ctx->sample_rate);
			fprintf(stderr, "corrected: %i Hz\n", outfreq);
			fputc((outfreq>>0) & 0xFF, fp);
			fputc((outfreq>>8) & 0xFF, fp);
			fputc((outfreq>>16) & 0xFF, fp);
			fputc((outfreq>>24) & 0xFF, fp);

			// reserved
			fputc(0, fp); fputc(0, fp); fputc(0, fp); fputc(0, fp);
			fputc(0, fp); fputc(0, fp); fputc(0, fp); fputc(0, fp);

		} else {
			for(i = 0; i < 16; i++)
				fputc(0, fp);

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

	// set up packet
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.size = 0;
	pkt.data = NULL;

	// set up scaler
	assert(sws_isSupportedOutput(AV_PIX_FMT_RGB24));
	struct SwsContext *scaler_ctx = NULL;

	// set up resampler
	struct SwrContext *resamp_ctx = NULL;
	if(astream != NULL)
	{
		resamp_ctx = swr_alloc_set_opts(NULL,
			AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_U8, outfreq,
			acodec_ctx->channel_layout, acodec_ctx->sample_fmt, acodec_ctx->sample_rate,
			0, NULL);
		assert(resamp_ctx != NULL);
		err = swr_init(resamp_ctx);
		assert(err == 0);
	}

	// set up audio buffers
	uint8_t *aubuf = NULL;
	ssize_t aubuf_len = 0;
	ssize_t aubuf_max = 0;

	for(;;)
	{
		// TODO: buffer frames for, say, MP4
		// just so we don't end up with out-of-sync audio
		// (it spams a few video frames, then spams a few audio frames)

		// read frame
		int got_pic = 0;
		pkt.data = NULL;
		pkt.size = 0;
		err = av_read_frame(format_ctx, &pkt);
		if(err != 0)
		{
			fprintf(stderr, "Error reading frame, assuming EOF\n");
			break;
		}

		assert(err == 0);
		AVStream *tmp_stream = format_ctx->streams[pkt.stream_index];
		if(tmp_stream != vstream)
		{
			if(tmp_stream != astream)
			{
				av_packet_unref(&pkt);
				continue;
			}

			// decode audio
			void *bdata = pkt.data;
			size_t bsize = pkt.size;
			int brem = pkt.size;

			// *really* decode audio
			// (yes, you have to call this several times with some codecs)
			while(brem > 0)
			{
				// decode
				err = avcodec_decode_audio4(acodec_ctx, frame, &got_pic, &pkt);
				assert(err >= 0);
				int xsize = err;

				if(got_pic)
				{
					// add to buffer
					int outsmps = swr_get_out_samples(resamp_ctx, frame->nb_samples);
					aubuf_len += outsmps;
					if(aubuf_len+128 > aubuf_max-64)
					{
						aubuf_max = aubuf_len + 256 + 64;
						aubuf = realloc(aubuf, aubuf_max*1*1);
						//fprintf(stderr, "out %i\n", (int)aubuf_max);
					}

					// resample + copy
					uint8_t *bmesh[4] = {(uint8_t *)(aubuf + aubuf_len - outsmps)};
					int rserr = swr_convert(resamp_ctx
						, bmesh, (outsmps)/(1*1)
						, (const uint8_t **)frame->data, frame->nb_samples
						);
					//fprintf(stderr, "%i %i %i\n", rserr, outsmps, frame->nb_samples);
					assert(rserr >= 0);
					//assert(rserr == outsmps);
					aubuf_len -= outsmps;
					aubuf_len += rserr;

					// advance
					brem -= xsize;
					pkt.data += xsize;
					pkt.size += xsize;
					//fprintf(stderr, "B%i R%i L%i\n",(int)aubuf_len,brem,err);

					// clean up
					av_frame_unref(frame);
				}
			}

			// reset packet
			pkt.data = bdata;
			pkt.size = bsize;

			// carry on
			av_packet_unref(&pkt);
			continue;
		}

		err = avcodec_decode_video2(vcodec_ctx, frame, &got_pic, &pkt);
		if(err < 0)
		{
			//fprintf(stderr, "%i %i\n", pkt.dts, pkt.pts);
			fprintf(stderr, "decode error (%i): %s\n", pkt.size, av_err2str(err));
			av_packet_unref(&pkt);
			continue;
			//fflush(stderr);
			//abort();
		}

		if(got_pic)
		{

			// prep scaler
			if(scaler_ctx == NULL)
			{
				scaler_ctx = sws_getContext(
					frame->width, frame->height, frame->format,
					VW, VH, AV_PIX_FMT_RGB24,
					SWS_LANCZOS,
					NULL, NULL,
					NULL);

				assert(scaler_ctx != NULL);

				outbufsz = av_image_alloc(outdata, outstride, VW, VH,
					AV_PIX_FMT_RGB24, 1);
				assert(outbufsz == VW*VH*3);
				assert(outdata[0] != NULL);

				outdata[1] = outdata[0]+1;
				outdata[2] = outdata[0]+2;

			}

			// scale image
			err = sws_scale(scaler_ctx,
				(const uint8_t *const*)frame->data, frame->linesize,
				0, frame->height,
				outdata, outstride);
			assert(err >= 0);
		}

		// nuke packet
		av_packet_unref(&pkt);
		//if(pkt.data != NULL) free(pkt.data);

		if(!got_pic)
			continue;

		// nuke frame
		av_frame_unref(frame);

		// loop until we have a frame
		if(scaler_ctx == NULL)
			continue;

		// fetch 
		/*
		if(fread(realrawinbuf, VW*VH*3, 1, stdin) <= 0)
		{
			//for(;;) fputc(rand()>>16, stdout);
			break;
		}
		*/

		// convert to YUV
		for(y = 0; y < VH; y++)
		for(x = 0; x < VW; x++)
		{
			//int r = realrawinbuf[y][x][0];
			//int g = realrawinbuf[y][x][1];
			//int b = realrawinbuf[y][x][2];
			int r = outdata[0][y*VW*3+3*x];
			int g = outdata[1][y*VW*3+3*x];
			int b = outdata[2][y*VW*3+3*x];

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

			// add sound packet
			if(acodec_ctx != NULL)
			{
				if(aubuf_len == 0)
				{
					// no sound in this packet
					fputc(0, fp);
					fputc(0, fp);
				} else {

					// pull packet
					//assert(aubuf_len >= 0 && aubuf_len < 65536);
					assert(aubuf_len >= 0);
					if(aubuf_len > 0xFFFE)
					{
						fputc((int)(0xFFFE&0xFF), fp);
						fputc((int)(0xFFFE>>8), fp);

						// write
						fwrite(aubuf, 0xFFFE, 1, fp);

					} else {
						fputc((int)(aubuf_len&0xFF), fp);
						fputc((int)(aubuf_len>>8), fp);

						// write
						fwrite(aubuf, aubuf_len, 1, fp);

						// pad to 16-bit
						if((aubuf_len & 1) != 0)
						{
							fputc(0, fp);
						}
					}

					// clear buffer
					if(aubuf_len > 0xFFFE)
					{
						// TODO!
						aubuf_len = 0;
					} else {
						aubuf_len = 0;
					}
				}
			}
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
	
	// finish algorithm
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

		// add sound packet
		if(acodec_ctx != NULL)
		{
			if(aubuf_len == 0)
			{
				// no sound in this packet
				fputc(0, fp);
				fputc(0, fp);
			} else {

				// pull packet
				if(aubuf_len > 0xFFFE) aubuf_len = 0xFFFE;
				assert(aubuf_len >= 0 && aubuf_len < 65536);
				fputc((int)(aubuf_len&0xFF), fp);
				fputc((int)(aubuf_len>>8), fp);

				// write
				fwrite(aubuf, aubuf_len, 1, fp);

				// pad to 16-bit
				if((aubuf_len & 1) != 0)
				{
					fputc(0, fp);
				}

				// clear buffer
				aubuf_len = 0;
			}
		}
	}

	fprintf(stderr, "***** DONE *****\n");

	if(fp != NULL)
	{
		fputc(0x00, fp);
		fputc(0x00, fp);
#ifdef PIXEL15
#if VW >= 256 || VH >= 248
		fputc(0xFF, fp);
		fputc(0xFD, fp);
#else
		fputc(0xFD, fp);
		fputc(0xFF, fp);
#endif
#else
		fputc(0xFD, fp);
#endif
		fclose(fp);
	}

	// clean up ffmpeg
	// TODO!
	//avformat_close_input(&format_ctx);

	return 0;
}

