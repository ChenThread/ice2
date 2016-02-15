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

		} else if(!strcmp("-i", argv[i])) {
			i++; assert(i < argc);

			assert(fname_in == NULL);
			fname_in = argv[i];

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
	for(i = 0; i < format_ctx->nb_streams; i++)
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

		if(astream == NULL)
		{
			astream = format_ctx->streams[i];

			if(astream->codec->codec_type != AVMEDIA_TYPE_VIDEO)
			{
				astream = NULL;
			} else {
				astream_idx = i;
			}

		}
	}
	assert(vstream_idx >= 0);
	assert(vstream_idx < format_ctx->nb_streams);

	vcodec = avcodec_find_decoder(vstream->codec->codec_id);
	assert(vcodec != NULL);
	fprintf(stderr, "video codec: %s\n", vcodec->name);

	// set some options
	AVDictionary *vopts = NULL;
	av_dict_set(&vopts, "refcounted_frames", "1", 0);

	// open codec context
	//AVCodecContext *vcodec_ctx = avcodec_alloc_context3(vcodec);
	AVCodecContext *vcodec_ctx = vstream->codec;
	assert(vcodec_ctx != NULL);
	err = avcodec_open2(vcodec_ctx, vcodec, &vopts);
	assert(err == 0);

	// allocate frame
	AVFrame *frame = av_frame_alloc();
	assert(frame != NULL);
	uint8_t *outdata[4] = {NULL, NULL, NULL, NULL};
	int outstride[4] = {0, 0, 0, 0};
	int outbufsz = 0;

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
		if(fps >= 255) fps = tfps;
		assert(fps >= 1 && fps <= 255);
		fputc(fps, fp);
		// TODO: enforce 20fps for OC mode

		// reserved
		fputc(0, fp); fputc(0, fp); fputc(0, fp);

		// audio codec (0x00 = no audio, 0x01 = PCM, 0x02 = IMA ADPCM, 0x03 = DFPWM?)
		fputc(0, fp);
		
		// channels
		fputc(0, fp);

		// bytes per block (0 if irrelevant)
		fputc(0, fp);
		fputc(0, fp);

		// audio frequency
		fputc(0, fp);
		fputc(0, fp);
		fputc(0, fp);
		fputc(0, fp);

		// reserved
		fputc(0, fp); fputc(0, fp); fputc(0, fp); fputc(0, fp);
		fputc(0, fp); fputc(0, fp); fputc(0, fp); fputc(0, fp);

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

	for(;;)
	{
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
		AVStream *tmp_vstream = format_ctx->streams[pkt.stream_index];
		if(tmp_vstream != vstream) continue;

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
	avformat_close_input(&format_ctx);

	return 0;
}

