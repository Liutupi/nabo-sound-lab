#include "sl_ui.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

fc_font_t F_DISP24, F_DISP18, F_SANS16, F_SANS14, F_MONO14, F_MONO22, F_MONO48, F_DISP56;
uint32_t VT;
nm_result_t MIC;
bool MIC_LIVE;
uint8_t WF_BUF[WF_ROWS][NM_WF_COLS];
volatile uint16_t WF_HEAD, PITCH_HEAD, DB_HEAD;
volatile bool WF_FROZEN;
int16_t PITCH_HIST[PH_N], DB_HIST[DBH_N];
uint32_t ONSET_T[16];
int ONSET_CNT;
typedef struct { uint32_t sample; int16_t db_x100; } onset_event_t;
static onset_event_t g_on_q[16];
static uint32_t g_on_w, g_on_r, g_on_dropped;
static int16_t g_on_dispatch_db = NM_DB_NONE;
static volatile uint32_t g_live_hop;
sl_settings_t SET = {.a4 = 440, .quiet = 60, .cal = 110, .sens = 0, .calm = 0, .volume = 80};
sl_records_t REC;
void (*g_onset_hook)(uint32_t t_sample);

extern const sl_scene_t SC_BOOT, SC_MENU;
static const sl_scene_t *g_scene;
static uint32_t g_t0, g_up_t, g_toast_t, g_rng = 20261023;
static bool g_held[3], g_up_pending;
static char g_toast[48];

__attribute__((weak)) void sl_settings_save(void) {}
__attribute__((weak)) void sl_volume_apply(uint8_t volume) { (void)volume; }
__attribute__((weak)) void sl_mic_last(int16_t *dst, int n) { memset(dst, 0, sizeof(int16_t) * (size_t)n); }

const uint16_t BARC[32] = {   /* 原型 grad([cyan, blue, purple, mag, gold], 32)，由脚本生成 */
  RGB(0x35,0xF0,0xDC),RGB(0x36,0xE2,0xE0),RGB(0x37,0xD5,0xE5),RGB(0x38,0xC8,0xE9),RGB(0x39,0xBB,0xEE),RGB(0x3A,0xAE,0xF2),RGB(0x3B,0xA1,0xF7),RGB(0x3C,0x94,0xFB),RGB(0x3F,0x89,0xFF),RGB(0x49,0x83,0xFF),RGB(0x53,0x7D,0xFF),RGB(0x5D,0x77,0xFF),RGB(0x67,0x71,0xFF),RGB(0x71,0x6B,0xFF),RGB(0x7B,0x65,0xFF),RGB(0x85,0x5F,0xFF),RGB(0x91,0x5A,0xF9),RGB(0xA0,0x56,0xED),RGB(0xAF,0x52,0xE2),RGB(0xBE,0x4E,0xD6),RGB(0xCD,0x4A,0xCB),RGB(0xDD,0x46,0xBF),RGB(0xEC,0x42,0xB4),RGB(0xFB,0x3E,0xA8),RGB(0xFF,0x4A,0x9D),RGB(0xFF,0x5C,0x91),RGB(0xFF,0x6E,0x85),RGB(0xFF,0x80,0x79),RGB(0xFF,0x92,0x6D),RGB(0xFF,0xA4,0x61),RGB(0xFF,0xB6,0x55),RGB(0xFF,0xC9,0x4A)};
const uint16_t PC12[12] = {RGB(0xFF,0x4D,0x5E),RGB(0xFF,0x9A,0x3D),RGB(0xFF,0xC9,0x4A),RGB(0xC8,0xFF,0x4A),RGB(0x5C,0xFF,0x8A),RGB(0x35,0xF0,0xDC),
  RGB(0x3D,0xC8,0xFF),RGB(0x3D,0x8B,0xFF),RGB(0x8A,0x5C,0xFF),RGB(0xC3,0x5C,0xFF),RGB(0xFF,0x3D,0xA6),RGB(0xFF,0x5C,0x8A)};
const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

uint32_t rng_u32(void) {
  g_rng += 0x6D2B79F5u; uint32_t t = g_rng;
  t = (t ^ (t >> 15)) * (1u | t); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
  return t ^ (t >> 14);
}

bool sl_ui_init(const uint8_t *blob, uint32_t size, uint32_t seed) {
  fc_res_init(blob, size);
  fc_gfx_bg(C_BG, false); fc_gfx_scanlines(false);
  bool ok = fc_font_open("font/disp24", &F_DISP24) & fc_font_open("font/disp18", &F_DISP18) & fc_font_open("font/sans16", &F_SANS16) &
            fc_font_open("font/sans14", &F_SANS14) & fc_font_open("font/mono14", &F_MONO14) & fc_font_open("font/mono22", &F_MONO22) &
            fc_font_open("font/mono48", &F_MONO48) & fc_font_open("font/disp56", &F_DISP56);
  g_rng = seed ? seed : 1; VT = 0; g_scene = NULL;
  g_on_w = g_on_r = g_on_dropped = 0; g_on_dispatch_db = NM_DB_NONE;
  sl_audio_init();
  return ok;
}

const sl_scene_t *sl_ui_scene(void) { return g_scene; }
uint32_t sl_ui_t0(void) { return g_t0; }
uint8_t sl_ui_mic_features(void) { return g_scene ? g_scene->mic : 0; }

void sl_ui_go(const sl_scene_t *s) {
  if (g_scene && g_scene->leave) g_scene->leave();
  g_onset_hook = NULL;
  g_scene = s; g_t0 = VT; g_up_pending = false; particles_clear();
  fc_img_cache_clear();   /* 换场景把解码缓存还给堆，避免碎片越积越多 */
  if (s->enter) s->enter();
}
extern const sl_scene_t *const SL_SCENES[];
void sl_ui_go_id(const char *id) { for (int i = 0; SL_SCENES[i]; i++) if (!strcmp(SL_SCENES[i]->id, id)) { sl_ui_go(SL_SCENES[i]); return; } }
void sl_ui_back(void) {
  if (g_scene == &SC_MENU || g_scene == &SC_BOOT) return;
  if (g_scene->back && g_scene->back()) return;
  sl_ui_go(&SC_MENU);
}

/* 按键：▲ 单击延迟 300 ms 判断是否双击（双击返回）；主菜单的 ▲ 立刻响应；OK 支持按住 */
void sl_ui_key(int k, bool down) {
  if (k < 0 || k > 2 || g_held[k] == down) return;
  g_held[k] = down;
  const sl_scene_t *s = g_scene;
  if (!down) { if (k == K_OK && s->ok_up) s->ok_up(); return; }
  if (k == K_UP) {
    if (s->up_now) { if (s->up) s->up(); return; }
    if (g_up_pending && VT - g_up_t < 300) { g_up_pending = false; sl_ui_back(); return; }
    g_up_pending = true; g_up_t = VT; return;
  }
  if (k == K_DOWN) { if (s->down) s->down(); return; }
  if (s->ok_down) s->ok_down(); else if (s->ok) s->ok();
}

void sl_mic_hop(const nm_result_t *r, uint32_t t) {
  MIC = *r;
  if (!WF_FROZEN) { uint16_t h = (uint16_t)((WF_HEAD + 1) % WF_ROWS); memcpy(WF_BUF[h], r->wf_row, NM_WF_COLS); WF_HEAD = h; }
  PITCH_HIST[PITCH_HEAD] = (int16_t)(r->f0_x10 && r->f0_conf_x1000 > 800 ? hz10_to_midi100(r->f0_x10) : 0); PITCH_HEAD = (uint16_t)((PITCH_HEAD + 1) % PH_N);
  DB_HIST[DB_HEAD] = r->db_x100; DB_HEAD = (uint16_t)((DB_HEAD + 1) % DBH_N);
  if (r->db_x100 > -7000) g_live_hop = r->hop;
  MIC_LIVE = r->hop - g_live_hop < 100;
  if (r->onset) {
    /* Audio producer and UI consumer share the existing short audio mutex.
     * Publish the time and level together; never overwrite an unread slot. */
    sl_audio_lock();
    if (g_on_w - g_on_r < 16) {
      g_on_q[g_on_w % 16] = (onset_event_t){t, r->db_x100};
      g_on_w++;
    } else g_on_dropped++;
    sl_audio_unlock();
  }
}
void sl_mic_skip(void) { MIC.onset = 0; }
int16_t sl_onset_db_x100(void) { return g_on_dispatch_db; }
uint32_t sl_onset_dropped(void) { sl_audio_lock(); uint32_t n = g_on_dropped; sl_audio_unlock(); return n; }

/* ---------- 粒子（彩带）：位置 Q8 像素，速度 Q8 像素/秒 ---------- */
#define NP 80
static struct { int32_t x, y, vx, vy; uint16_t c; } P[NP];
static int g_np;
void particles_clear(void) { g_np = 0; }
void confetti(int n) {
  for (int i = 0; i < n && g_np < NP; i++) {
    P[g_np].x = RI(240) * 256; P[g_np].y = -RI(120) * 256;
    P[g_np].vx = (RI(80) - 40) * 256; P[g_np].vy = (40 + RI(60)) * 256; P[g_np].c = PC12[i % 12]; g_np++;
  }
}
static void step_p(int dt) {
  for (int i = g_np - 1; i >= 0; i--) {
    P[i].vy += 60 * 256 * dt / 1000; P[i].x += P[i].vx * dt / 1000; P[i].y += P[i].vy * dt / 1000;
    if (P[i].y > (320 << 8)) P[i] = P[--g_np];
  }
}

void sl_ui_step(int dt) {
  VT += (uint32_t)dt;
  for (;;) {                            /* 起音：在界面线程里按顺序交给场景 */
    onset_event_t event;
    sl_audio_lock();
    if (g_on_r == g_on_w) { sl_audio_unlock(); break; }
    event = g_on_q[g_on_r % 16]; g_on_r++;
    sl_audio_unlock();                    /* 回调可能播放声音，不能持锁派发 */
    uint32_t t = event.sample;
    g_on_dispatch_db = event.db_x100;
    ONSET_T[ONSET_CNT % 16] = t; ONSET_CNT++;
    if (g_onset_hook) g_onset_hook(t);
  }
  if (g_up_pending && VT - g_up_t >= 300) { g_up_pending = false; if (g_scene->up) g_scene->up(); }
  if (g_scene->update) g_scene->update(dt);
  step_p(dt);
}

void sl_ui_render(fc_flush_fn flush, void *ctx, bool force) {
  fc_gfx_begin();
  g_scene->draw();
  for (int i = 0; i < g_np; i++) if (P[i].y >= 0) fc_rect(P[i].x >> 8, P[i].y >> 8, 3, 5, P[i].c, 255);
  if (g_toast[0] && VT - g_toast_t < 1200) {
    /* Temporarily replace the complete footer, including any scene-specific
     * line at 276, so no fragments remain beside a short notification. */
    fc_rect(0, 260, FC_W, 60, C_PANEL, 255);
    T(&F_SANS14, g_toast, 120, 276, C_WHITE, AL_C);
    if (g_scene != &SC_MENU && g_scene != &SC_BOOT)
      T(&F_SANS16, "▲▲ 返回", 120, 306, C_GOLD, AL_C);
  }
  fc_gfx_end(flush, ctx, force);
}

void toast(const char *s) { snprintf(g_toast, sizeof g_toast, "%s", s); g_toast_t = VT; }

/* ---------- 画面工具 ---------- */
uint16_t mix565(uint16_t a, uint16_t b, int t) {
  int r = ((a >> 11) * (1000 - t) + (b >> 11) * t) / 1000, g = (((a >> 5) & 63) * (1000 - t) + ((b >> 5) & 63) * t) / 1000, bl = ((a & 31) * (1000 - t) + (b & 31) * t) / 1000;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
void T(const fc_font_t *f, const char *s, int x, int y, uint16_t c, int al) { fc_text(f, x, y, s, c, al); }
void TF(const fc_font_t *f, int x, int y, uint16_t c, int al, const char *fmt, ...) {
  char b[96]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); fc_text(f, x, y, b, c, al);
}
void rr(int x, int y, int w, int h, int r, uint16_t c) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  fc_rect(x + r, y, w - 2 * r, h, c, 255);
  fc_rect(x, y + r, r, h - 2 * r, c, 255); fc_rect(x + w - r, y + r, r, h - 2 * r, c, 255);
  fc_disc(x + r, y + r, r, c, 255); fc_disc(x + w - r - 1, y + r, r, c, 255);
  fc_disc(x + r, y + h - r - 1, r, c, 255); fc_disc(x + w - r - 1, y + h - r - 1, r, c, 255);
}
void rr_frame(int x, int y, int w, int h, int r, int th, uint16_t c, uint16_t inner) {   /* 外圆角填 c，再在里面填 inner */
  rr(x, y, w, h, r, c); rr(x + th, y + th, w - 2 * th, h - 2 * th, r > th ? r - th : 1, inner);
}

void header(const char *title, const char *mode) {
  fc_rect(0, 0, FC_W, 26, C_PANEL, 255);
  T(&F_DISP18, title, 8, 13, C_WHITE, AL_L);
  if (mode && *mode) T(&F_SANS14, mode, 232, 13, C_CYAN, AL_R);
}

void hintK(const hint_t *h, int n, bool back) {
  hint_t it[6]; int m = 0;
  for (int i = 0; i < n && m < 5; i++) if (h[i].key) it[m++] = h[i];
  if (back) { it[m].key = "▲▲"; it[m].label = "返回"; m++; }
  int ws[6], tot = 0, gap = 8, key_gap = 3;
  for (int pass = 0; pass < 2; pass++) {
    tot = 0;
    for (int i = 0; i < m; i++) { ws[i] = fc_text_width(&F_SANS16, it[i].key) + key_gap + fc_text_width(&F_SANS16, it[i].label); tot += ws[i] + (i ? gap : 0); }
    if (tot <= 224) break;
    gap = 4;
  }
  /* 固定保留返回语义；拥挤时缩短词语，字号与亮度不退回去。 */
  if (tot > 224) {
    for (int i = 0; i < m; i++) {
      if (!strcmp(it[i].label, "灵敏度")) it[i].label = "灵敏";
      else if (!strcmp(it[i].label, "下一个")) it[i].label = "下个";
      else if (!strcmp(it[i].label, "和弦进行")) it[i].label = "进行";
      else if (!strcmp(it[i].label, "尤克里里")) it[i].label = "尤克";
      else if (!strcmp(it[i].label, "重新点")) it[i].label = "重置";
      else if (!strcmp(it[i].label, "选指法")) it[i].label = "指法";
      else if (!strcmp(it[i].label, "答对了！下一题")) it[i].label = "下一题";
      else if (!strcmp(it[i].label, "时间轴")) it[i].label = "时间";
    }
    tot = 0; for (int i = 0; i < m; i++) { ws[i] = fc_text_width(&F_SANS16, it[i].key) + key_gap + fc_text_width(&F_SANS16, it[i].label); tot += ws[i] + (i ? gap : 0); }
  }
  /* Four compact groups fit with positive spacing and the same 16px font.
   * Longer future hints wrap whole groups instead of overlapping glyphs. */
  if (tot > 232) {
    key_gap = 2; gap = 3; tot = 0;
    for (int i = 0; i < m; i++) { ws[i] = fc_text_width(&F_SANS16, it[i].key) + key_gap + fc_text_width(&F_SANS16, it[i].label); tot += ws[i] + (i ? gap : 0); }
  }
  int row_first[6], row_end[6], row_w[6], rows = 0;
  for (int i = 0; i < m;) {
    int first = i, w = ws[i++];
    while (i < m && w + gap + ws[i] <= 232) { w += gap + ws[i]; i++; }
    row_first[rows] = first; row_end[rows] = i; row_w[rows++] = w;
  }
  if (!rows) rows = 1;
  int top = 320 - rows * 20;
  fc_rect(0, top, FC_W, rows * 20, C_PANEL, 255);
  for (int row = 0; row < rows && m; row++) {
    int x = 120 - row_w[row] / 2, y = top + row * 20 + 10;
    for (int i = row_first[row]; i < row_end[row]; i++) {
      const char *k = it[i].key;
      uint16_t kc = !strcmp(k, "▼") ? C_GREEN : !strcmp(k, "OK") ? C_WHITE : !strcmp(k, "▲▲") ? C_GOLD : C_CYAN;
      T(&F_SANS16, k, x, y, kc, AL_L);
      T(&F_SANS16, it[i].label, x + fc_text_width(&F_SANS16, k) + key_gap, y, C_WHITE, AL_L);
      x += ws[i] + gap;
    }
  }
}

void need_mic(void) {
  if (MIC_LIVE) return;
  rr(20, 136, 200, 50, 8, RGB(0, 0, 0));
  T(&F_DISP18, "麦克风没有声音", 120, 152, C_GOLD, AL_C);
  T(&F_SANS14, "对着工牌说句话试试", 120, 172, C_DIM, AL_C);
}

/* ---------- NABO 精灵：缺动作退回 S-IDLE，缺尺寸用最接近的尺寸缩放 ---------- */
typedef struct { char name[24]; fc_img_t im; bool ok; } slot_t;
static slot_t g_spr[160];   /* 素材包约 93 项 + 找不到的名字（也要记住，免得每帧重查） */
static int g_nspr;
static const fc_img_t *open_named(const char *name) {
  for (int i = 0; i < g_nspr; i++) if (!strcmp(g_spr[i].name, name)) return g_spr[i].ok ? &g_spr[i].im : NULL;
  if (strlen(name) >= sizeof g_spr[0].name || g_nspr >= (int)(sizeof g_spr / sizeof g_spr[0])) return NULL;
  slot_t *s = &g_spr[g_nspr++];
  snprintf(s->name, sizeof s->name, "%s", name);
  s->ok = fc_img_open(name, &s->im);
  return s->ok ? &s->im : NULL;
}
const fc_img_t *prop(const char *name) { char b[32]; snprintf(b, sizeof b, "prop/%s", name); return open_named(b); }
static const fc_img_t *spr_find(const char *id, int size, int *got) {
  static const int SZ[] = {120, 76, 56};
  char b[32];
  for (int pass = 0; pass < 2; pass++) {
    const char *nm = pass ? "S-IDLE" : id;
    snprintf(b, sizeof b, "spr/%s/%d", nm, size);
    const fc_img_t *im = open_named(b);
    if (im) { *got = size; return im; }
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++) for (int j = i + 1; j < 3; j++)
      if (abs(SZ[order[j]] - size) < abs(SZ[order[i]] - size)) { int v = order[i]; order[i] = order[j]; order[j] = v; }
    for (int i = 0; i < 3; i++) {   /* 最接近的尺寸 */
      snprintf(b, sizeof b, "spr/%s/%d", nm, SZ[order[i]]);
      if ((im = open_named(b))) { *got = SZ[order[i]]; return im; }
    }
  }
  return NULL;
}
static int frame_at(const fc_img_t *im, int t, bool once) {
  int n = im->frames, f = (int)((int64_t)(t < 0 ? 0 : t) * 12 / 1000);
  return (once || !im->loop) ? fx_clamp(f, 0, n - 1) : f % n;
}
int nabo_ms(const char *id) { int got; const fc_img_t *im = spr_find(id, 76, &got); return im ? im->frames * 1000 / 12 : 1000; }
static bool standing_sprite(const char *id) {
  return !strncmp(id, "S-ROPE-", 7) || !strcmp(id, "S-RUN-R") || !strcmp(id, "S-JUMP-R") || !strcmp(id, "S-FALL-R") || !strcmp(id, "S-UMBRELLA");
}
static void sprite_draw(const fc_img_t *im, const char *id, int f, int got, int size, int cx, int cy, int kx, int ky, int ang) {
  /* anchors.json: full-body foot=(512,928)/1024. FLOAT keeps its own centre. */
  bool foot = standing_sprite(id), floating = !strcmp(id, "S-FLOAT");
  int ay = foot ? (got * 29 + 16) / 32 : floating ? got / 2 : got;
  if (got == size && kx == 1000 && ky == 1000 && !ang) { fc_img(im, f, cx - size / 2, foot ? cy + size / 2 - ay : cy - size / 2, 255); return; }
  int sy = floating ? cy : cy + size / 2;
  fc_img_aff(im, f, cx, sy, got / 2, ay, ang, (int)((int64_t)kx * size * 256 / got / 1000), (int)((int64_t)ky * size * 256 / got / 1000), 255);
}
void nabo_sq(const char *id, int size, int cx, int cy, int t, int kx, int ky, int ang) {
  int got; const fc_img_t *im = spr_find(id, size, &got);
  if (!im) return;
  int f = frame_at(im, t, false);
  sprite_draw(im, id, f, got, size, cx, cy, kx, ky, ang);
}
void nabo(const char *id, int size, int cx, int cy, int t) { nabo_sq(id, size, cx, cy, t, 1000, 1000, 0); }
void nabo_once(const char *id, int size, int cx, int cy, int t) {
  int got; const fc_img_t *im = spr_find(id, size, &got);
  if (!im) return;
  int f = frame_at(im, t, true);
  sprite_draw(im, id, f, got, size, cx, cy, 1000, 1000, 0);
}

/* ---------- 音名 ---------- */
void note_name(int midi, char *o) { int pc = ((midi % 12) + 12) % 12; sprintf(o, "%s%d", NN[pc], (midi - pc) / 12 - 1); }

static const uint32_t LOG2_TAB[33] = {0, 2909, 5732, 8473, 11136, 13727, 16248, 18704, 21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346,
                                      38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207, 52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047, 65536};
static int32_t log2_q16(uint64_t p) {
  int msb = 63; while (msb > 0 && !(p >> msb)) msb--;
  uint32_t fr = msb >= 16 ? (uint32_t)((p >> (msb - 16)) & 0xFFFF) : (uint32_t)((p << (16 - msb)) & 0xFFFF);
  uint32_t i = fr >> 11, r = fr & 0x7FF;
  return (msb << 16) + (int32_t)LOG2_TAB[i] + (int32_t)((((int32_t)LOG2_TAB[i + 1] - (int32_t)LOG2_TAB[i]) * (int32_t)r) >> 11);
}
int hz10_to_midi100(uint32_t hz10) {
  if (!hz10) return 0;
  int32_t d = log2_q16((uint64_t)hz10 << 8) - log2_q16((uint64_t)SET.a4 * 10 << 8);   /* log2(f/A4) Q16 */
  return 6900 + (int)(((int64_t)d * 1200) >> 16);
}

uint32_t fnv32(const void *p, int n) { const uint8_t *b = (const uint8_t *)p; uint32_t h = 2166136261u; while (n--) { h ^= *b++; h *= 16777619u; } return h; }
void trace_draw(trace_t *t, int top, int h) {
  for (int i = 0; i < t->n; i++) {   /* 每列先算好竖线段 [lo, lo+sp]，合成时每像素只比一次 */
    int a = t->y[i];
    if (a < 0) { t->lo[i] = 32767; t->sp[i] = 0; continue; }
    int b = i > 0 && t->y[i - 1] >= 0 ? t->y[i - 1] : a, lo = (a < b ? a : b) - t->th, hi = (a > b ? a : b) + t->th;
    if (hi - lo > 255) hi = lo + 255;
    t->lo[i] = (int16_t)lo; t->sp[i] = (uint8_t)(hi - lo);
  }
  fc_vspans(t->x0, top, t->n, h, t->lo, t->sp, t->c, fnv32(t->y, t->n * 2) ^ fnv32(t->c, t->n * 2) ^ (uint32_t)t->th * 2654435761u);
}

void icon(const sl_scene_t *s, const char *g, int cx, int cy, uint16_t c) {
  if (s) { char b[24]; snprintf(b, sizeof b, "icon-%s", s->id); const fc_img_t *im = prop(b); if (im) { fc_img(im, 0, cx - im->w / 2, cy - im->h / 2, 255); return; } }
  const char *p = g; fc_glyph_t gl;
  if (g && *g && fc_font_glyph(&F_SANS14, fc_utf8_next(&p), &gl)) T(&F_SANS14, g, cx, cy, c, AL_C); else fc_disc(cx, cy, 3, c, 255);
}
