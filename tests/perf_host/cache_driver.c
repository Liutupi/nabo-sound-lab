/* Host-only allocation accounting around the real decoder. No ESP-IDF hooks. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static struct { void *p; size_t bytes; } allocations[128];
static unsigned live_bytes, peak_bytes, alloc_calls, free_calls;
static void *tracked_malloc(size_t bytes) {
  void *p = malloc(bytes);
  if (p) {
    for (int i = 0; i < 128; i++) if (!allocations[i].p) {
      allocations[i].p = p; allocations[i].bytes = bytes; break;
    }
    live_bytes += (unsigned)bytes; alloc_calls++;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
  }
  return p;
}
static void tracked_free(void *p) {
  if (p) for (int i = 0; i < 128; i++) if (allocations[i].p == p) {
    live_bytes -= (unsigned)allocations[i].bytes;
    allocations[i].p = NULL; allocations[i].bytes = 0; free_calls++; break;
  }
  free(p);
}
#define malloc tracked_malloc
#define free tracked_free
#ifdef PF_BASELINE
#include "baseline/fc_res_0_1_1.c"
#else
#include "../../firmware/main/gfx/fc_res.c"
#endif
#undef malloc
#undef free
API void pf_init(const uint8_t *data, unsigned n) { fc_res_init(data, n); }
API void pf_reset(void) { fc_img_cache_clear(); live_bytes = peak_bytes = alloc_calls = free_calls = 0; }
API void pf_epoch(void) { fc_img_epoch(); }
API const uint8_t *pf_frame(const char *name, int frame) {
  fc_img_t im; return fc_img_open(name, &im) ? fc_img_frame(&im, frame) : NULL;
}
API unsigned pf_cache_bytes(void) { return fc_img_cache_bytes(); }
API unsigned pf_live_bytes(void) { return live_bytes; }
API unsigned pf_peak_bytes(void) { return peak_bytes; }
API unsigned pf_alloc_calls(void) { return alloc_calls; }
API unsigned pf_free_calls(void) { return free_calls; }
