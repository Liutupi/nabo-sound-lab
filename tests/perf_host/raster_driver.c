/* Unchanged 0.1.1 callback reference versus the column-span raster primitive. */
#include <stdint.h>
#include <string.h>
#include "../../firmware/main/gfx/fc_gfx.h"
#include "../../firmware/main/gfx/fc_fx.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static uint16_t fb[FC_W * FC_H], full[FC_W * FC_H];
static int mismatch, strips;
static void flush(int y, int rows, const uint16_t *pixels, void *ctx) {
  memcpy((uint16_t *)ctx + y * FC_W, pixels, (size_t)rows * FC_W * 2);
}
API const uint16_t *pf_fb(void) { return fb; }
API int pf_mismatch(void) { return mismatch; }
API int pf_overflow(void) { return fc_gfx_overflow(); }
API int pf_ops(void) { return fc_gfx_ops(); }
API void pf_begin(void) { fc_gfx_begin(); fc_gfx_bg(RGB(10,7,22), false); }
API void pf_image(const char *name, int frame, int x, int y, int affine) {
  fc_img_t im;
  if (!fc_img_open(name, &im)) return;
  if (affine) fc_img_aff(&im, frame, x + im.w/2, y + im.h/2, im.w/2, im.h/2, 0, 256, 256, 255);
  else fc_img(&im, frame, x, y, 255);
}
API int pf_end(int force) {
  strips = fc_gfx_end(flush, fb, force != 0);
  fc_gfx_end(flush, full, true);
  mismatch = 0;
  for (int i = 0; i < FC_W * FC_H; i++) mismatch += fb[i] != full[i];
  return strips;
}
typedef struct { int16_t lo[240]; uint8_t span[240]; uint16_t color[240]; int x, n; } columns_t;
static columns_t traces[4];
static int column_x, column_n = 240;
API void pf_columns(int x, int n) { column_x = x; column_n = n; }
static void old_trace(int y, uint16_t *row, int x0, int x1, const void *ctx) {
  const columns_t *t = ctx;
  int a0 = x0 > t->x ? x0 - t->x : 0, a1 = x1 < t->x + t->n ? x1 - t->x : t->n;
  for (int x = a0 + t->x; x < a1 + t->x; x++) {
    int i = x - t->x;
    if ((unsigned)(y - t->lo[i]) <= t->span[i]) row[x] = t->color[i];
  }
}
static void grid(int y, uint16_t *row, int x0, int x1, const void *ctx) {
  (void)ctx;
  if ((y - 40) % 31 == 0) for (int x = (x0+5)/6*6; x < x1; x += 6) row[x] = RGB(43,35,80);
  else if ((y - 40) % 6 == 0) for (int x = (x0+29)/30*30; x < x1; x += 30) row[x] = RGB(43,35,80);
}
API void pf_plot(int seed, int optimized, int offset_x, int offset_y,
                 int clip_x, int clip_y, int clip_w, int clip_h) {
  pf_begin();
  fc_rect(0,0,240,26,RGB(21,16,48),255);
  fc_rect(0,300,240,20,RGB(21,16,48),255);
  fc_gfx_offset(offset_x,offset_y);
  fc_gfx_clip(clip_x,clip_y,clip_w,clip_h);
  fc_cb(0,40,240,251,grid,NULL,1);
  for (int j = 3; j >= 0; j--) {
    columns_t *t = &traces[j]; t->x = column_x; t->n = column_n;
    int previous = 164;
    for (int x = 0; x < t->n; x++) {
      int y = seed % 4 == 0 ? 164 + fx_sin(x * 13 + seed*7 + j*11)*96/16384
             : seed % 4 == 1 ? 164 + ((x*31+seed*17+j*53)%239)-119
             : seed % 4 == 2 ? 164 + (x%23 == 0 ? -100 : x%23 == 1 ? 105 : 0)
             : 164 + fx_sin(x*3+seed+j)*4/16384;
      int a = y < previous ? y : previous, b = y > previous ? y : previous;
      t->lo[x] = (int16_t)(a - (j == 0)); t->span[x] = (uint8_t)(b-a+2*(j==0));
      if (x % 47 == 0) { t->lo[x] = 32767; t->span[x] = 0; }
      t->color[x] = j ? RGB(20+j*7,50-j*8,65-j*9) : RGB(53,240,220);
      previous = y;
    }
#ifndef PF_BASELINE
    if (optimized) fc_vspans(t->x,40,t->n,251,t->lo,t->span,t->color,(uint32_t)(seed*4+j));
    else
#endif
      fc_cb(t->x,40,t->n,251,old_trace,t,(uint32_t)(seed*4+j));
  }
  fc_gfx_clip(0,0,0,0); fc_gfx_offset(0,0);
}
