#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <assert.h>
//define assert(...)

#include <pthread.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

// SSSE3 intrinsics
#include <tmmintrin.h>

//define I_AM_A_POLISH_WEEB
//define NO_THREADS

#if 1
#define VW 640
#define VH 448
#define GPU_BUDGET_HARD_MAX (2000)
#define GPU_BUDGET_MAX (2000-6)
#define COST_COPY 0
#define COST_FG 0
#define COST_BG 0
#define COST_FILL (6+(VW*VH>65536?2:0)+(VW>=256||VH>=248?2:0))
#define COST_SET 4
typedef uint16_t pixeltyp;
#define PIXEL15
#define NOYUV
//define NOCOMPACT
//define NOMOCOMP
#define LAZY_MOCOMP
#define FAST_LARGE_BOXES

#define WTL 1
#define WTCB 1
#define WTCR 1

#else
#define VW 160
#define VH 50
// the compactor accidentally bloats things sometimes
#define GPU_BUDGET_HARD_MAX (255)
#define GPU_BUDGET_MAX (255-4)
#define COST_FILL 2
#define COST_COPY 4
#define COST_SET 1
#define COST_FG 2
#define COST_BG 2
typedef uint8_t pixeltyp;

#define WTL 1
#define WTCB 2
#define WTCR 2
#endif

#define PAL_WTL 1
#define PAL_WTC_GREY 64
#define PAL_WTC 1
#define GREY_RAMP 64


typedef enum gop_op
{
	GOP_COPY,
	GOP_FILL_FG,
	GOP_FILL_BG,

} gop_op_e;

typedef struct gop
{
	uint8_t l, cb, cr;
	uint8_t cost;
	int16_t bx, by, bw, bh;
	int16_t rsx, rsy;
	uint8_t op;
	pixeltyp pal;
} gop_s;

struct tdat_calc_motion_comp
{
	int move_x;
	int move_y;
};

struct tdat_algo_1
{
	struct tdat_calc_motion_comp Tm;
};
#define GOP_MAX 10240
extern gop_s gop[GOP_MAX];
extern int gop_count;
extern int gop_bloat;
extern int gop_bloat_cost;

extern FILE *fp;

extern int gpu_usage_saving2;
extern int gpu_usage_saving;
extern int gpu_usage;
extern int gpu_pal_bg_fbeg;
extern int gpu_pal_fg_fbeg;
extern int gpu_pal_bg;
extern int gpu_pal_fg;
extern int gpu_pal_next_is_bg;
extern int gpu_pal_next_is_bg_fbeg;
extern int gpu_compact_pal_changes;

extern uint8_t rawinbuf[3][VH][VW];
extern uint8_t precurbuf[3][VH][VW];
extern uint8_t rawcurbuf[3][VH][VW];

extern uint8_t rawlastbuf_mvec[3][VH][VW];
extern uint8_t rawinbuf_mvec[3][VH][VW];

extern pixeltyp rawinbuf_new_pal[VH][VW];
extern pixeltyp rawinbuf_pal[VH][VW];
extern pixeltyp precurbuf_pal[VH][VW];
extern pixeltyp rawcurbuf_pal[VH][VW];

// algo.c
void *algo_1(void *tdat);

// compact.c
void gpu_compact(void);

// gpu.c
void gpu_start(void);
void gpu_emit(void);
int gpu_compar_palidx(const void *av, const void *bv);
void gpu_fill(int bx, int by, int bw, int bh, int l, int cb, int cr, int pal, int add_op);
void gpu_copy(int bx, int by, int bw, int bh, int dx, int dy, int add_op);

// mvec.c
void *calc_motion_comp(void *tdat);

// palcmp.c
void pal_to_rgb(int pal, int *restrict r, int *restrict g, int *restrict b);
void init_pal_list(void);
int rgb_to_pal_exact_refimp(int *r, int *g, int *b);
int rgb_to_pal_exact(int *r, int *g, int *b);
int rgb_to_pal_approx(int *r, int *g, int *b);
int rgb_to_pal_noyuv(int *r, int *g, int *b);
int rgb_to_pal_walk(int *r, int *g, int *b);

#ifdef PIXEL15
//define rgb_to_pal_pre rgb_to_pal_walk
#define rgb_to_pal_pre rgb_to_pal_noyuv
#define rgb_to_pal rgb_to_pal_noyuv
#else
#ifdef I_AM_A_POLISH_WEEB
#define rgb_to_pal rgb_to_pal_exact_refimp
#else
#define rgb_to_pal rgb_to_pal_exact
#endif
#define rgb_to_pal_pre rgb_to_pal
#endif

// rect.c
int get_average_rect_in(int layer, int bx, int by, int bw, int bh);
int compare_rect_copy_layer(int layer, int sx, int sy, int rw, int rh, int dx, int dy);
int compare_rect_fill_layer_small(int layer, int dx, int dy, int rw, int rh, int ref);
int compare_rect_fill_layer(int layer, int dx, int dy, int rw, int rh, int ref);
int compare_rect_still_small(int layer, int dx, int dy, int rw, int rh);
int compare_full_screen(int32_t *rows);
int compare_rect_still_rows(int dx, int dy, int rw, int rh, int32_t *rows);
int compare_rect_still(int layer, int dx, int dy, int rw, int rh);
int compare_rect_still_pal(int dx, int dy, int rw, int rh);

// ycbcr.c
void to_ycbcr(int r, int g, int b, int *restrict l, int *restrict cb, int *restrict cr);
void from_ycbcr(int l, int cb, int cr, int *restrict r, int *restrict g, int *restrict b);

// inline stuff
static inline __m128i get_end_mask(int rw)
{
	const __m128i ref_endmask = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
	return _mm_cmpgt_epi8(_mm_set1_epi8(rw&15), ref_endmask);
}

