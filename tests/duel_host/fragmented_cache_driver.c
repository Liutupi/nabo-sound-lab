/* Host-only observation and fault injection around the production decoder.
 * The tracked allocator is private to fc_res.c; no firmware code is replaced. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

#define TRACKED 128
#define GUARD 16
static struct { uint8_t *raw, *p; size_t bytes; } allocations[TRACKED];
static unsigned attempts, successes, frees, live, peak, failures, bad_free;
static unsigned guard_errors, live_blocks, largest_success, max_block, fail_at;
static unsigned requests[4096], request_n;

static void check_guards(int i) {
  for (int j = 0; j < GUARD; j++)
    if (allocations[i].raw[j] != 0xA7 || allocations[i].p[allocations[i].bytes + j] != 0xA7)
      guard_errors++;
}
static void *tracked_malloc(size_t bytes) {
  attempts++;
  if (request_n < sizeof requests / sizeof requests[0]) requests[request_n++] = (unsigned)bytes;
  if ((max_block && bytes > max_block) || (fail_at && attempts == fail_at)) { failures++; return NULL; }
  int slot = -1;
  for (int i = 0; i < TRACKED; i++) if (!allocations[i].p) { slot = i; break; }
  if (slot < 0) { failures++; return NULL; }
  uint8_t *raw = (uint8_t *)malloc(bytes + 2 * GUARD);
  if (!raw) { failures++; return NULL; }
  allocations[slot].raw = raw; allocations[slot].p = raw + GUARD; allocations[slot].bytes = bytes;
  memset(raw, 0xA7, bytes + 2 * GUARD); memset(raw + GUARD, 0xCD, bytes);
  successes++; live_blocks++; live += (unsigned)bytes;
  if (live > peak) peak = live;
  if (bytes > largest_success) largest_success = (unsigned)bytes;
  return raw + GUARD;
}
static void tracked_free(void *p) {
  if (!p) return;
  for (int i = 0; i < TRACKED; i++) if (allocations[i].p == p) {
    check_guards(i); live -= (unsigned)allocations[i].bytes; live_blocks--; frees++;
    memset(allocations[i].p, 0xDD, allocations[i].bytes);
    free(allocations[i].raw); memset(&allocations[i], 0, sizeof allocations[i]); return;
  }
  bad_free++;
}
#define malloc tracked_malloc
#define free tracked_free
#include "../../firmware/main/gfx/fc_res.c"
#undef malloc
#undef free
#include "../../firmware/main/gfx/fc_gfx.h"

API void frag_init(const uint8_t *data, unsigned size) { fc_res_init(data, size); }
API void frag_clear(void) { fc_img_cache_clear(); }
/* Policy changes require an empty cache, so leak accounting cannot be reset away. */
API int frag_reset(unsigned limit, unsigned fail_call) {
  if (live || live_blocks || bad_free || guard_errors) return 0;
  attempts = successes = frees = peak = failures = largest_success = request_n = 0;
  max_block = limit; fail_at = fail_call; return 1;
}
API void frag_epoch(void) { fc_img_epoch(); }
API unsigned frag_stat(int which) {
  for (int i = 0; i < TRACKED; i++) if (allocations[i].p) check_guards(i);
  switch (which) {
    case 0: return attempts; case 1: return successes; case 2: return frees;
    case 3: return live; case 4: return peak; case 5: return failures;
    case 6: return bad_free; case 7: return guard_errors; case 8: return live_blocks;
    case 9: return largest_success; default: return fc_img_cache_bytes();
  }
}
API unsigned frag_request(unsigned i) { return i < request_n ? requests[i] : 0; }
API unsigned frag_request_count(void) { return request_n; }
API const fc_img_surface_t *frag_surface(const char *name, int frame) {
  fc_img_t im; return fc_img_open(name, &im) ? fc_img_surface(&im, frame) : NULL;
}
API const uint8_t *frag_linear(const char *name, int frame) {
  fc_img_t im; return fc_img_open(name, &im) ? fc_img_frame(&im, frame) : NULL;
}
API const uint8_t *frag_row(const fc_img_surface_t *surface, int y) { return fc_img_row(surface, y); }

static uint16_t fb[FC_W * FC_H], full[FC_W * FC_H];
static int mismatches;
static void flush(int y, int rows, const uint16_t *pixels, void *ctx) {
  memcpy((uint16_t *)ctx + y * FC_W, pixels, (size_t)rows * FC_W * 2);
}
API void frag_begin(int dx, int dy, int x, int y, int w, int h, int scan) {
  fc_gfx_begin(); fc_gfx_bg(RGB(10, 7, 22), false); fc_gfx_scanlines(scan != 0);
  fc_rect(0, 0, FC_W, 27, RGB(35, 26, 70), 255);
  fc_gfx_offset(dx, dy); fc_gfx_clip(x, y, w, h);
}
API int frag_add(const char *name, int frame, int x, int y, int kind,
                 int angle, int kx, int ky, int alpha) {
  fc_img_t im; if (!fc_img_open(name, &im)) return 0;
  if (kind == 1) fc_img_aff(&im, frame, x + im.w / 2, y + im.h / 2,
                           im.w / 2, im.h / 2, angle, kx, ky, (uint8_t)alpha);
  else if (kind == 2) fc_img_part(&im, frame, im.w / 5, im.h / 3, im.w * 3 / 5,
                                 im.h / 2, x, y);
  else fc_img(&im, frame, x, y, (uint8_t)alpha);
  return 1;
}
API int frag_end(int force) {
  int strips = fc_gfx_end(flush, fb, force != 0);
  fc_gfx_end(flush, full, true); mismatches = 0;
  for (int i = 0; i < FC_W * FC_H; i++) mismatches += fb[i] != full[i];
  return strips;
}
API const uint16_t *frag_fb(void) { return fb; }
API int frag_mismatches(void) { return mismatches; }
API int frag_overflow(void) { return fc_gfx_overflow(); }
