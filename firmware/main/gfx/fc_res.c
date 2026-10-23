#include "fc_res.h"
#include "fc_fx.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t *g_blob;
static uint32_t g_size;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void fc_res_init(const uint8_t *blob, uint32_t size) { g_blob = blob; g_size = size; fc_img_cache_clear(); }

const uint8_t *fc_res(const char *name, uint32_t *size) {
  if (!g_blob || memcmp(g_blob, "FNAS", 4)) return NULL;
  int n = rd16(g_blob + 6);
  for (int i = 0; i < n; i++) {
    const uint8_t *e = g_blob + 8 + 40 * i;
    if (strncmp((const char *)e, name, 32) == 0) {
      uint32_t off = rd32(e + 32), len = rd32(e + 36);
      if (off + len > g_size) return NULL;
      if (size) *size = len;
      return g_blob + off;
    }
  }
  return NULL;
}

/* ---------- 图像 ---------- */
bool fc_img_open(const char *name, fc_img_t *im) {
  const uint8_t *d = fc_res(name, NULL);
  if (!d) return false;
  im->data = d;
  if (!memcmp(d, "FNBO", 4)) {            /* 正方形：4s H H(size) H(n) B */
    im->w = im->h = rd16(d + 6); im->frames = rd16(d + 8); im->loop = d[10];
    im->pal = d + 11; im->offs = d + 11 + 126;
  } else if (!memcmp(d, "FNBR", 4)) {     /* 矩形：4s H H(w) H(h) H(n) B */
    im->w = rd16(d + 6); im->h = rd16(d + 8); im->frames = rd16(d + 10); im->loop = d[12];
    im->pal = d + 13; im->offs = d + 13 + 126;
  } else return false;
  return true;
}

static void decode_into(const fc_img_t *im, int k, const fc_img_surface_t *s) {
  const uint8_t *fr = im->data + rd32(im->offs + 4 * k);
  bool delta = k > 0;
  for (int y = 0; y < im->h; y++) {
    const uint8_t *p = fr + rd16(fr + 2 * y);
    uint8_t *row = (uint8_t *)fc_img_row(s, y);
    int x = 0;
    while (x < im->w) {
      uint8_t c = *p++; int L = (c & 0x7F) + 1;
      if (c & 0x80) { if (!delta) memset(row + x, 0, (size_t)L); }
      else { memcpy(row + x, p, (size_t)L); p += L; }
      x += L;
    }
  }
}

/* 解码缓存：键 = (图像, 帧)。本帧（epoch）引用的槽始终固定，后续条带仍会用它。
 * 跨帧复用同一图像的空闲槽：既能向前解差分，也能在循环回绕/倒放时从帧 0 重解，
 * 不为每次回到帧 0 再分配一张完整索引图。同帧合唱的多个不同帧仍各占自己的槽。 */
#define CACHE_N 24
#ifndef FC_IMG_BUDGET
#define FC_IMG_BUDGET (32 * 1024)   /* 合唱 120² + 2×76² = 25952 B；32 KiB 能容纳，24 KiB 不行 */
#endif
static struct { const uint8_t *key; int frame; fc_img_surface_t surface; uint32_t bytes, used, epoch; } g_cache[CACHE_N];
static uint32_t g_tick, g_epoch = 1, g_bytes;

void fc_img_epoch(void) { g_epoch++; }
static void slot_free(int i) {
  g_bytes -= g_cache[i].bytes;
  free((void *)g_cache[i].surface.part[0]); free((void *)g_cache[i].surface.part[1]);
  memset(&g_cache[i], 0, sizeof(g_cache[i]));
}
void fc_img_cache_clear(void) { for (int i = 0; i < CACHE_N; i++) if (g_cache[i].key) slot_free(i); g_bytes = 0; }
uint32_t fc_img_cache_bytes(void) { return g_bytes; }

static bool surface_alloc(fc_img_surface_t *s, const fc_img_t *im) {
  s->w = im->w; s->split = im->h;
  s->part[0] = (uint8_t *)malloc((uint32_t)im->w * im->h); s->part[1] = NULL;
  if (s->part[0]) return true;
  if (im->h < 2) return false;
  /* Fragmented C3 heap: retain original pixels with two independently allocated
   * row ranges. No reservation while radio is idle, or per-frame allocation. */
  s->split = im->h / 2;
  s->part[0] = (uint8_t *)malloc((uint32_t)im->w * s->split);
  if (!s->part[0]) return false;
  s->part[1] = (uint8_t *)malloc((uint32_t)im->w * (im->h - s->split));
  if (s->part[1]) return true;
  free((void *)s->part[0]); s->part[0] = NULL; return false;
}

static int slot_alloc(const fc_img_t *im) {
  uint32_t bytes = (uint32_t)im->w * im->h;
  if (bytes > FC_IMG_BUDGET) return -1;
  for (;;) {
    int empty = -1, lru = -1;
    for (int i = 0; i < CACHE_N; i++) {
      if (!g_cache[i].key) { if (empty < 0) empty = i; }
      else if (g_cache[i].epoch != g_epoch && (lru < 0 || g_cache[i].used < g_cache[lru].used)) lru = i;   /* 本帧显示列表里引用着的不能淘汰 */
    }
    if (empty >= 0 && g_bytes + bytes <= FC_IMG_BUDGET) {
      if (!surface_alloc(&g_cache[empty].surface, im)) {
        if (lru < 0) return -1;
        slot_free(lru); continue;
      }   /* 堆不够：再淘汰一个；分块失败已经释放临时块 */
      g_cache[empty].bytes = bytes; g_bytes += bytes; return empty;
    }
    if (lru < 0) return -1;
    slot_free(lru);
  }
}

const fc_img_surface_t *fc_img_surface(const fc_img_t *im, int frame) {
  if (!im || !im->data || !im->w || !im->h || !im->frames) return NULL;
  if (frame < 0) frame = 0;
  if (frame >= im->frames) frame = im->frames - 1;
  int hit = -1, base = -1, reusable = -1;
  for (int i = 0; i < CACHE_N; i++) {
    if (g_cache[i].key != im->data) continue;
    if (g_cache[i].frame == frame) { hit = i; break; }
    if (g_cache[i].frame < frame && (base < 0 || g_cache[i].frame > g_cache[base].frame)) base = i;
    if (g_cache[i].epoch != g_epoch && (reusable < 0 || g_cache[i].used < g_cache[reusable].used)) reusable = i;
  }
  g_tick++;
  if (hit < 0) {
    int cur;
    if (base >= 0 && g_cache[base].epoch != g_epoch) { hit = base; cur = g_cache[base].frame; }   /* 原地推进 */
    else {
      /* A stale frame of this image is equally reusable when it is later than
       * the requested frame. Restart its decoder, not its allocation. A base
       * already pinned by this epoch may be copied but never modified/evicted. */
      hit = reusable >= 0 ? reusable : slot_alloc(im);
      if (hit < 0) return NULL;
      g_cache[hit].key = im->data;
      if (base >= 0 && g_cache[base].key == im->data) {
        for (int y = 0; y < im->h; y++) memcpy((uint8_t *)fc_img_row(&g_cache[hit].surface, y), fc_img_row(&g_cache[base].surface, y), im->w);
        cur = g_cache[base].frame;
      } else { decode_into(im, 0, &g_cache[hit].surface); cur = 0; }
    }
    while (cur < frame) decode_into(im, ++cur, &g_cache[hit].surface);
    g_cache[hit].frame = frame;
  }
  g_cache[hit].used = g_tick; g_cache[hit].epoch = g_epoch;
  return &g_cache[hit].surface;
}

const uint8_t *fc_img_frame(const fc_img_t *im, int frame) {
  const fc_img_surface_t *s = fc_img_surface(im, frame);
  return s && !s->part[1] ? s->part[0] : NULL;
}

/* ---------- 字体 ---------- */
bool fc_font_open(const char *name, fc_font_t *f) {
  const uint8_t *d = fc_res(name, NULL);
  if (!d || memcmp(d, "FNFT", 4)) return false;
  f->data = d; f->n = rd16(d + 6); f->line_h = d[8]; f->asc = d[9]; f->space = d[10];
  return true;
}

bool fc_font_glyph(const fc_font_t *f, uint32_t cp, fc_glyph_t *g) {
  int lo = 0, hi = f->n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const uint8_t *e = f->data + 12 + 13 * mid;
    uint32_t c = rd32(e);
    if (c == cp) {
      g->bits = f->data + rd32(e + 4); g->w = e[8]; g->h = e[9];
      g->xo = (int8_t)e[10]; g->yo = (int8_t)e[11]; g->adv = e[12];
      return true;
    }
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}

uint32_t fc_utf8_next(const char **s) {
  const uint8_t *p = (const uint8_t *)*s; uint32_t c = *p;
  int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
  if (n == 2) c = ((c & 0x1F) << 6) | (p[1] & 0x3F);
  else if (n == 3) c = ((c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
  else if (n == 4) c = ((c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
  *s += n; return c;
}

int fc_text_width(const fc_font_t *f, const char *s) {
  int w = 0; fc_glyph_t g;
  while (*s) { uint32_t c = fc_utf8_next(&s); if (c == ' ') w += f->space; else if (fc_font_glyph(f, c, &g)) w += g.adv; else w += f->space; }
  return w;
}

/* ---------- 文本表 ---------- */
const char *fc_txt(const char *table, int i) {
  char name[32]; size_t n = strlen(table);
  if (n > 26) return "";
  memcpy(name, "txt/", 4); memcpy(name + 4, table, n + 1);
  uint32_t sz; const uint8_t *d = fc_res(name, &sz);
  if (!d || memcmp(d, "FNTX", 4)) return "";
  int cnt = rd16(d + 4);
  if (i < 0 || i >= cnt) return "";
  return (const char *)d + rd32(d + 6 + 4 * i);
}

/* ---------- 正弦表 ---------- */
static const int16_t SIN_Q14[257] = {
#include "fc_sin_table.inc"
};
int fx_sin(int a) {
  a &= ANG_TURN - 1;
  int q = a >> 8, i = a & 255;
  switch (q) {
    case 0: return SIN_Q14[i];
    case 1: return SIN_Q14[256 - i];
    case 2: return -SIN_Q14[i];
    default: return -SIN_Q14[256 - i];
  }
}
