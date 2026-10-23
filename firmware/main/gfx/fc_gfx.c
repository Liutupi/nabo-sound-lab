#include "fc_gfx.h"
#include "fc_fx.h"
#include <string.h>

#define MAX_OPS 400
#define ARENA 8192
#define NSTRIP (FC_H / FC_STRIP)

enum { OP_RECT, OP_LINE, OP_ELL, OP_RING, OP_TRI, OP_TAIJI, OP_IMG, OP_AFF, OP_TEXT, OP_POLY, OP_CB, OP_VSPANS };

typedef struct {
  uint8_t type, alpha;
  uint16_t c1, c2;
  int16_t x0, y0, x1, y1;            /* 包围盒（已裁剪，右下不含） */
  int16_t k0, k1, k2, k3;            /* 裁剪框 */
  int32_t p[8];
  union {                  /* Reuse image-op storage; no larger display list. */
    fc_img_t im;
    struct { const int16_t *lo; const uint8_t *span; const uint16_t *colors; } columns;
  };
  int frame;
  const fc_font_t *font;
  fc_row_fn fn;
  const void *ctx;
  uint16_t s_off, s_len;
  uint32_t hash;
} op_t;

static op_t g_ops[MAX_OPS];
static int g_n, g_dx, g_dy, g_clip[4] = {0, 0, FC_W, FC_H};
static bool g_scan = false, g_grid = false, g_over;
static char g_arena[ARENA];
static int g_arena_n;
static uint32_t g_prev[NSTRIP];
static uint16_t g_strip[FC_W * FC_STRIP];

static uint16_t C_BG = RGB(0x0A, 0x07, 0x16);
#define C_GRID RGB(0x23, 0x1A, 0x46)

void fc_gfx_begin(void) { fc_img_epoch(); g_n = 0; g_dx = g_dy = 0; g_arena_n = 0; g_over = false; fc_gfx_clip(0, 0, 0, 0); }
void fc_gfx_offset(int dx, int dy) { g_dx = dx; g_dy = dy; }
void fc_gfx_scanlines(bool on) { g_scan = on; }
void fc_gfx_bg(uint16_t c, bool grid) { C_BG = c; g_grid = grid; }
void fc_gfx_clip(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) { g_clip[0] = 0; g_clip[1] = 0; g_clip[2] = FC_W; g_clip[3] = FC_H; return; }
  g_clip[0] = fx_clamp(x, 0, FC_W); g_clip[1] = fx_clamp(y, 0, FC_H);
  g_clip[2] = fx_clamp(x + w, 0, FC_W); g_clip[3] = fx_clamp(y + h, 0, FC_H);
}
int fc_gfx_ops(void) { return g_n; }
bool fc_gfx_overflow(void) { return g_over; }

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  while (n--) { h ^= *b++; h *= 16777619u; }
  return h;
}

static op_t *add(int type, int x0, int y0, int x1, int y1) {
  x0 += g_dx; x1 += g_dx; y0 += g_dy; y1 += g_dy;
  if (x0 < g_clip[0]) x0 = g_clip[0];
  if (y0 < g_clip[1]) y0 = g_clip[1];
  if (x1 > g_clip[2]) x1 = g_clip[2];
  if (y1 > g_clip[3]) y1 = g_clip[3];
  if (x1 <= x0 || y1 <= y0) return NULL;
  if (g_n >= MAX_OPS) { g_over = true; return NULL; }
  op_t *o = &g_ops[g_n++];
  memset(o, 0, sizeof(*o));
  o->type = (uint8_t)type; o->alpha = 255;
  o->x0 = (int16_t)x0; o->y0 = (int16_t)y0; o->x1 = (int16_t)x1; o->y1 = (int16_t)y1;
  o->k0 = (int16_t)g_clip[0]; o->k1 = (int16_t)g_clip[1]; o->k2 = (int16_t)g_clip[2]; o->k3 = (int16_t)g_clip[3];
  return o;
}
static void seal(op_t *o) {
  uint32_t h = 2166136261u;
  h = fnv(h, &o->type, 2); h = fnv(h, &o->c1, 4); h = fnv(h, &o->x0, 16); h = fnv(h, o->p, sizeof(o->p));
  h = fnv(h, &o->im.data, sizeof(o->im.data)); h = fnv(h, &o->frame, sizeof(o->frame)); h = fnv(h, &o->font, sizeof(o->font));
  if (o->s_len) h = fnv(h, g_arena + o->s_off, o->s_len);
  o->hash = h;
}

void fc_rect(int x, int y, int w, int h, uint16_t c, uint8_t a) {
  op_t *o = add(OP_RECT, x, y, x + w, y + h); if (!o) return;
  o->c1 = c; o->alpha = a; seal(o);
}
void fc_frame(int x, int y, int w, int h, int th, uint16_t c) {
  fc_rect(x, y, w, th, c, 255); fc_rect(x, y + h - th, w, th, c, 255);
  fc_rect(x, y + th, th, h - 2 * th, c, 255); fc_rect(x + w - th, y + th, th, h - 2 * th, c, 255);
}
void fc_line(int x0, int y0, int x1, int y1, int w, uint16_t c) {
  int r = w / 2 + 1;
  op_t *o = add(OP_LINE, (x0 < x1 ? x0 : x1) - r, (y0 < y1 ? y0 : y1) - r, (x0 > x1 ? x0 : x1) + r + 1, (y0 > y1 ? y0 : y1) + r + 1);
  if (!o) return;
  o->c1 = c; o->p[0] = x0 + g_dx; o->p[1] = y0 + g_dy; o->p[2] = x1 + g_dx; o->p[3] = y1 + g_dy; o->p[4] = w; seal(o);
}
void fc_ellipse(int cx, int cy, int rx, int ry, uint16_t c, uint8_t a) {
  op_t *o = add(OP_ELL, cx - rx, cy - ry, cx + rx + 1, cy + ry + 1); if (!o) return;
  o->c1 = c; o->alpha = a; o->p[0] = cx + g_dx; o->p[1] = cy + g_dy; o->p[2] = rx; o->p[3] = ry; seal(o);
}
void fc_disc(int cx, int cy, int r, uint16_t c, uint8_t a) { fc_ellipse(cx, cy, r, r, c, a); }
void fc_ring(int cx, int cy, int r, int th, uint16_t c) {
  int e = r + th / 2 + 1;
  op_t *o = add(OP_RING, cx - e, cy - e, cx + e + 1, cy + e + 1); if (!o) return;
  o->c1 = c; o->p[0] = cx + g_dx; o->p[1] = cy + g_dy; o->p[2] = r; o->p[3] = th; seal(o);
}
void fc_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  int mnx = x0 < x1 ? x0 : x1, mny = y0 < y1 ? y0 : y1, mxx = x0 > x1 ? x0 : x1, mxy = y0 > y1 ? y0 : y1;
  if (x2 < mnx) { mnx = x2; }
  if (y2 < mny) { mny = y2; }
  if (x2 > mxx) { mxx = x2; }
  if (y2 > mxy) { mxy = y2; }
  op_t *o = add(OP_TRI, mnx, mny, mxx + 1, mxy + 1); if (!o) return;
  o->c1 = c; o->p[0] = x0 + g_dx; o->p[1] = y0 + g_dy; o->p[2] = x1 + g_dx; o->p[3] = y1 + g_dy; o->p[4] = x2 + g_dx; o->p[5] = y2 + g_dy; seal(o);
}
void fc_taiji(int cx, int cy, int r, int ang, uint16_t a, uint16_t b) {
  op_t *o = add(OP_TAIJI, cx - r, cy - r, cx + r + 1, cy + r + 1); if (!o) return;
  o->c1 = a; o->c2 = b; o->p[0] = cx + g_dx; o->p[1] = cy + g_dy; o->p[2] = r; o->p[3] = ang & (ANG_TURN - 1); seal(o);
}
void fc_img_part(const fc_img_t *im, int frame, int sx, int sy, int sw, int sh, int x, int y) {
  if (!im || !im->data) return;
  if (sw <= 0) { sx = 0; sy = 0; sw = im->w; sh = im->h; }
  op_t *o = add(OP_IMG, x, y, x + sw, y + sh); if (!o) return;
  o->im = *im; o->frame = frame;
  o->p[0] = x + g_dx - sx; o->p[1] = y + g_dy - sy; o->p[2] = im->w; o->p[3] = im->h; o->p[4] = frame; seal(o);
}
void fc_img(const fc_img_t *im, int frame, int x, int y, uint8_t a) {
  if (!im || !im->data) return;
  int n = g_n; fc_img_part(im, frame, 0, 0, 0, 0, x, y);
  if (g_n > n) { g_ops[n].alpha = a; seal(&g_ops[n]); }
}
void fc_img_aff(const fc_img_t *im, int frame, int cx, int cy, int ax, int ay, int ang, int kx, int ky, uint8_t a) {
  if (!im || !im->data || kx <= 0 || ky <= 0) return;
  int s = fx_sin(ang), c = fx_cos(ang), mx = 0, my = 0;
  int cxs[4] = {-ax, im->w - ax, im->w - ax, -ax}, cys[4] = {-ay, -ay, im->h - ay, im->h - ay};
  for (int i = 0; i < 4; i++) {
    int64_t u = (int64_t)cxs[i] * kx / 256, v = (int64_t)cys[i] * ky / 256;
    int X = (int)((u * c - v * s) >> 14), Y = (int)((u * s + v * c) >> 14);
    if (X < 0) { X = -X; }
    if (Y < 0) { Y = -Y; }
    if (X > mx) { mx = X; }
    if (Y > my) { my = Y; }
  }
  op_t *o = add(OP_AFF, cx - mx - 1, cy - my - 1, cx + mx + 2, cy + my + 2); if (!o) return;
  o->im = *im; o->frame = frame;
  o->alpha = a;
  o->p[0] = cx + g_dx; o->p[1] = cy + g_dy; o->p[2] = ax; o->p[3] = ay; o->p[4] = ang & (ANG_TURN - 1); o->p[5] = kx; o->p[6] = ky;
  o->p[7] = (im->w << 16) | im->h; seal(o);
}
void fc_text_n(const fc_font_t *f, int x, int y, const char *s, int nb, uint16_t c, int align) {
  if (!f || !f->data || nb <= 0) return;
  if (g_arena_n + nb + 1 > ARENA) { g_over = true; return; }
  char *dst = g_arena + g_arena_n; memcpy(dst, s, (size_t)nb); dst[nb] = 0;
  int w = fc_text_width(f, dst);
  int left = align == AL_C ? x - w / 2 : align == AL_R ? x - w : x;
  int top = y - f->line_h / 2;
  op_t *o = add(OP_TEXT, left - 2, top - 2, left + w + 3, top + f->line_h + 3); if (!o) return;
  o->c1 = c; o->font = f; o->s_off = (uint16_t)g_arena_n; o->s_len = (uint16_t)nb;
  o->p[0] = left + g_dx; o->p[1] = top + g_dy;
  g_arena_n += nb + 1; seal(o);
}
void fc_text(const fc_font_t *f, int x, int y, const char *s, uint16_t c, int align) { fc_text_n(f, x, y, s, (int)strlen(s), c, align); }

void fc_cb(int x, int y, int w, int h, fc_row_fn fn, const void *ctx, uint32_t key) {
  if (!fn) return;
  op_t *o = add(OP_CB, x, y, x + w, y + h); if (!o) return;
  o->fn = fn; o->ctx = ctx; o->p[0] = (int32_t)key; o->p[1] = g_dx; o->p[2] = g_dy; seal(o);
}

void fc_vspans(int x, int top, int n, int height, const int16_t *lo,
               const uint8_t *span, const uint16_t *colors, uint32_t key) {
  if (!lo || !span || !colors || n <= 0 || height <= 0) return;
  op_t *o = add(OP_VSPANS, x, top, x + n, top + height); if (!o) return;
  o->columns.lo = lo; o->columns.span = span; o->columns.colors = colors;
  o->p[0] = x + g_dx; o->p[1] = g_dy; o->p[2] = (int32_t)key; seal(o);
}

/* 多边形：顶点为 1/16 像素（Q4），奇偶规则填充，最多 16 个顶点 */
void fc_poly(const int16_t *xy, int n, uint16_t c, uint8_t a) {
  if (n < 3 || n > 16) return;
  int mnx = 1 << 20, mny = 1 << 20, mxx = -(1 << 20), mxy = -(1 << 20);
  for (int i = 0; i < n; i++) {
    if (xy[2 * i] < mnx) { mnx = xy[2 * i]; }
    if (xy[2 * i] > mxx) { mxx = xy[2 * i]; }
    if (xy[2 * i + 1] < mny) { mny = xy[2 * i + 1]; }
    if (xy[2 * i + 1] > mxy) { mxy = xy[2 * i + 1]; }
  }
  int nb = 4 * n, al = (g_arena_n + 1) & ~1;
  if (al + nb > ARENA) { g_over = true; return; }
  op_t *o = add(OP_POLY, (mnx >> 4) - 1, (mny >> 4) - 1, (mxx >> 4) + 2, (mxy >> 4) + 2); if (!o) return;
  int16_t *dst = (int16_t *)(void *)(g_arena + al);
  for (int i = 0; i < n; i++) { dst[2 * i] = (int16_t)(xy[2 * i] + g_dx * 16); dst[2 * i + 1] = (int16_t)(xy[2 * i + 1] + g_dy * 16); }
  o->c1 = c; o->alpha = a; o->s_off = (uint16_t)al; o->s_len = (uint16_t)nb; o->p[0] = n;
  g_arena_n = al + nb; seal(o);
}

/* ---------- 合成 ---------- */
static inline uint16_t mix(uint16_t d, uint16_t s, int a) {      /* a: 0..256 */
  int r = ((s >> 11) * a + (d >> 11) * (256 - a)) >> 8;
  int g = (((s >> 5) & 63) * a + ((d >> 5) & 63) * (256 - a)) >> 8;
  int b = ((s & 31) * a + (d & 31) * (256 - a)) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
#define PUT(px, col, a) (*(px) = ((a) >= 256 ? (col) : mix(*(px), (col), (a))))

static void draw_op(const op_t *o, int sy0, int sy1) {
  int y0 = o->y0 > sy0 ? o->y0 : sy0, y1 = o->y1 < sy1 ? o->y1 : sy1;
  if (y0 >= y1) return;
  if (o->type == OP_VSPANS) {
    /* Trace callbacks used to test every column for every row in their 251px
     * bounding box. The same precomputed inclusive spans need only a clipped
     * column traversal and the actual painted pixels. No framebuffer or heap. */
    for (int x = o->x0; x < o->x1; x++) {
      int i = x - o->p[0], a = o->columns.lo[i] + o->p[1];
      int b = a + o->columns.span[i] + 1;
      if (a < y0) a = y0;
      if (b > y1) b = y1;
      for (int y = a; y < b; y++) g_strip[(y - sy0) * FC_W + x] = o->columns.colors[i];
    }
    return;
  }
  int A = o->alpha + (o->alpha > 127);  /* 0..256 */
  const fc_img_surface_t *idx = NULL;
  /* 单帧静态图（卡面、蛋糕、图标）直接从 RLE 逐行画，不占解码缓存（卡面一张就 64 KB） */
  bool direct = o->type == OP_IMG && o->im.frames == 1;
  if ((o->type == OP_IMG && !direct) || o->type == OP_AFF) { idx = fc_img_surface(&o->im, o->frame); if (!idx) return; }
  for (int y = y0; y < y1; y++) {
    uint16_t *row = g_strip + (y - sy0) * FC_W;
    switch (o->type) {
      case OP_RECT: for (int x = o->x0; x < o->x1; x++) PUT(row + x, o->c1, A); break;
      case OP_LINE: {
        int64_t ax = o->p[0] * 2, ay = o->p[1] * 2, bx = o->p[2] * 2, by = o->p[3] * 2, w = o->p[4];
        int64_t vx = bx - ax, vy = by - ay, L2 = vx * vx + vy * vy;
        for (int x = o->x0; x < o->x1; x++) {
          int64_t px = 2 * x + 1 - ax, py = 2 * y + 1 - ay, t = L2 ? (px * vx + py * vy) : 0;
          int64_t dx, dy;
          if (t <= 0 || !L2) { dx = px; dy = py; }
          else if (t >= L2) { dx = 2 * x + 1 - bx; dy = 2 * y + 1 - by; }
          else { dx = px - vx * t / L2; dy = py - vy * t / L2; }
          if (dx * dx + dy * dy <= w * w) row[x] = o->c1;
        }
        break; }
      case OP_ELL: {
        int64_t rx = o->p[2] * 2 + 1, ry = o->p[3] * 2 + 1, R = rx * rx * ry * ry;
        for (int x = o->x0; x < o->x1; x++) {
          int64_t dx = 2 * (x - o->p[0]), dy = 2 * (y - o->p[1]);
          if (dx * dx * ry * ry + dy * dy * rx * rx <= R) PUT(row + x, o->c1, A);
        }
        break; }
      case OP_RING: {
        int in = 2 * o->p[2] - o->p[3], out = 2 * o->p[2] + o->p[3];
        int in2 = in < 0 ? 0 : in * in, out2 = out * out;
        for (int x = o->x0; x < o->x1; x++) {
          int dx = 2 * (x - o->p[0]) + 1, dy = 2 * (y - o->p[1]) + 1, d = dx * dx + dy * dy;
          if (d >= in2 && d <= out2) row[x] = o->c1;
        }
        break; }
      case OP_TRI: {
        int x0 = o->p[0], y0_ = o->p[1], x1 = o->p[2], y1_ = o->p[3], x2 = o->p[4], y2 = o->p[5];
        int area = (x1 - x0) * (y2 - y0_) - (y1_ - y0_) * (x2 - x0);
        for (int x = o->x0; x < o->x1; x++) {
          int e0 = (x1 - x0) * (y - y0_) - (y1_ - y0_) * (x - x0), e1 = (x2 - x1) * (y - y1_) - (y2 - y1_) * (x - x1), e2 = (x0 - x2) * (y - y2) - (y0_ - y2) * (x - x2);
          if (area >= 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0)) row[x] = o->c1;
        }
        break; }
      case OP_TAIJI: {
        int r = o->p[2], s = fx_sin(-o->p[3]), c = fx_cos(-o->p[3]);
        for (int x = o->x0; x < o->x1; x++) {
          int dx = x - o->p[0], dy = y - o->p[1];
          if (dx * dx + dy * dy > r * r) continue;
          int u = (dx * c - dy * s) >> 14, v = (dx * s + dy * c) >> 14, h = r / 2;
          int up = u * u + (v + h) * (v + h), dn = u * u + (v - h) * (v - h), dot = (r / 7) * (r / 7);
          bool A_ = u > 0;
          if (up <= h * h) A_ = true;
          if (dn <= h * h) A_ = false;
          if (up <= dot) A_ = false;
          if (dn <= dot) A_ = true;
          row[x] = A_ ? o->c1 : o->c2;
        }
        break; }
      case OP_IMG: {
        int iw = o->p[2], ih = o->p[3], sy = y - o->p[1];
        if (sy < 0 || sy >= ih) break;
        if (direct) {
          const uint8_t *fr = o->im.data + (o->im.offs[0] | o->im.offs[1] << 8 | o->im.offs[2] << 16 | (uint32_t)o->im.offs[3] << 24);
          const uint8_t *p = fr + (fr[2 * sy] | fr[2 * sy + 1] << 8);
          int x = o->p[0], end = x + iw;
          while (x < end && x < o->x1) {
            uint8_t c = *p++; int L = (c & 0x7F) + 1;
            if (c & 0x80) { x += L; continue; }
            for (int k = 0; k < L; k++, x++) {
              if (x < o->x0 || x >= o->x1) continue;
              const uint8_t *pp = o->im.pal + 2 * (p[k] - 1); PUT(row + x, (uint16_t)(pp[0] | (pp[1] << 8)), A);
            }
            p += L;
          }
          break;
        }
        const uint8_t *src = fc_img_row(idx, sy);
        for (int x = o->x0; x < o->x1; x++) {
          int sx = x - o->p[0];
          if (sx < 0 || sx >= iw) continue;
          uint8_t i = src[sx];
          if (i) { const uint8_t *pp = o->im.pal + 2 * (i - 1); PUT(row + x, (uint16_t)(pp[0] | (pp[1] << 8)), A); }
        }
        break; }
      case OP_AFF: {
        int iw = o->p[7] >> 16, ih = o->p[7] & 0xFFFF, s = fx_sin(o->p[4]), c = fx_cos(o->p[4]);
        int kx = o->p[5], ky = o->p[6];
        int dx = 2 * (o->x0 - o->p[0]) + 1, dy = 2 * (y - o->p[1]) + 1;
        int nx = dx * c + dy * s, ny = -dx * s + dy * c;
        for (int x = o->x0; x < o->x1; x++) {
          /* Screen coordinates fit in 32 bits. Avoid software 64-bit division per pixel on C3. */
          int uq = (nx >> 14) * 256 / kx + 2 * o->p[2], vq = (ny >> 14) * 256 / ky + 2 * o->p[3];
          nx += 2 * c; ny -= 2 * s;
          if (uq < 0 || vq < 0) continue;
          int sx = uq >> 1, sy = vq >> 1;
          if (sx < 0 || sy < 0 || sx >= iw || sy >= ih) continue;
          uint8_t i = fc_img_row(idx, sy)[sx];
          if (i) { const uint8_t *pp = o->im.pal + 2 * (i - 1); PUT(row + x, (uint16_t)(pp[0] | (pp[1] << 8)), A); }
        }
        break; }
      case OP_CB: o->fn(y - o->p[2], row + o->p[1], o->x0 - o->p[1], o->x1 - o->p[1], o->ctx); break;
      case OP_POLY: {
        const int16_t *v = (const int16_t *)(const void *)(g_arena + o->s_off);
        int n = o->p[0], xs[16], k = 0, Y = y * 16 + 8;
        for (int i = 0; i < n; i++) {
          int x0 = v[2 * i], y0 = v[2 * i + 1], x1 = v[2 * ((i + 1) % n)], y1 = v[2 * ((i + 1) % n) + 1];
          if ((y0 <= Y && y1 > Y) || (y1 <= Y && y0 > Y)) xs[k++] = x0 + (int)((int64_t)(Y - y0) * (x1 - x0) / (y1 - y0));
        }
        for (int i = 1; i < k; i++) { int t = xs[i], j = i; while (j > 0 && xs[j - 1] > t) { xs[j] = xs[j - 1]; j--; } xs[j] = t; }
        for (int i = 0; i + 1 < k; i += 2) {
          int a0 = (xs[i] + 8) >> 4, a1 = (xs[i + 1] + 8) >> 4;
          if (a0 < o->x0) { a0 = o->x0; }
          if (a1 > o->x1) { a1 = o->x1; }
          for (int x = a0; x < a1; x++) PUT(row + x, o->c1, A);
        }
        break; }
      default: break;
    }
  }
}

static void draw_text(const op_t *o, int sy0, int sy1) {
  const fc_font_t *f = o->font; const char *s = g_arena + o->s_off;
  int x = o->p[0], top = o->p[1]; fc_glyph_t g;
  while (*s) {
    uint32_t cp = fc_utf8_next(&s);
    if (cp == ' ' || !fc_font_glyph(f, cp, &g)) { x += f->space; continue; }
    int gy0 = top + g.yo, gx0 = x + g.xo, rb = (g.w + 1) / 2;
    for (int yy = 0; yy < g.h; yy++) {
      int Y = gy0 + yy;
      if (Y < sy0 || Y >= sy1 || Y < o->k1 || Y >= o->k3) continue;
      uint16_t *row = g_strip + (Y - sy0) * FC_W;
      const uint8_t *b = g.bits + yy * rb;
      for (int xx = 0; xx < g.w; xx++) {
        int X = gx0 + xx;
        if (X < o->k0 || X >= o->k2) continue;
        int v = (xx & 1) ? (b[xx >> 1] & 15) : (b[xx >> 1] >> 4);
        if (v) PUT(row + X, o->c1, v == 15 ? 256 : v * 17);
      }
    }
    x += g.adv;
  }
}

int fc_gfx_end(fc_flush_fn flush, void *ctx, bool force) {
  int drawn = 0;
  for (int s = 0; s < NSTRIP; s++) {
    int sy0 = s * FC_STRIP, sy1 = sy0 + FC_STRIP;
    uint32_t h = (2166136261u ^ (uint32_t)g_scan ^ ((uint32_t)g_grid << 1)) * 16777619u ^ C_BG;
    for (int i = 0; i < g_n; i++) if (g_ops[i].y0 < sy1 && g_ops[i].y1 > sy0) h = fnv(h, &g_ops[i].hash, 4);
    if (!force && h == g_prev[s]) continue;
    g_prev[s] = h; drawn++;
    for (int y = sy0; y < sy1; y++) {
      uint16_t *row = g_strip + (y - sy0) * FC_W;
      for (int x = 0; x < FC_W; x++) row[x] = C_BG;
      if (g_grid && y >= 30 && (y - 30) % 16 == 0) for (int x = 8; x < FC_W; x += 16) row[x] = C_GRID;
    }
    for (int i = 0; i < g_n; i++) {
      const op_t *o = &g_ops[i];
      if (o->y0 >= sy1 || o->y1 <= sy0) continue;
      if (o->type == OP_TEXT) draw_text(o, sy0, sy1); else draw_op(o, sy0, sy1);
    }
    if (g_scan)
      for (int y = sy0; y < sy1; y += 2) {
        uint16_t *row = g_strip + (y - sy0) * FC_W;
        for (int x = 0; x < FC_W; x++) row[x] = mix(row[x], 0, 46);
      }
    flush(sy0, FC_STRIP, g_strip, ctx);
  }
  return drawn;
}
