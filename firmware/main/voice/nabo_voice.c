// nabo_voice.c —— 规则见 prototype/index.html 的“复读链路”
#include "nabo_voice.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

#define CLAMP(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))

static const int16_t STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IDXT[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

// sin 表：1025 项 Q15（最后一项 = 第一项，方便插值）
static int16_t SIN[1025];
static int16_t TANH[513];   // tanh(u)，u = 0..4，步长 1/128，Q15
static uint8_t s_tables;
static void tables(void) {   // 只在第一次录音或播放时建表，用浮点无所谓
    if (s_tables) return;
    for (int k = 0; k <= 1024; k++) SIN[k] = (int16_t)lround(32767.0 * sin(2 * M_PI * k / 1024));
    for (int k = 0; k <= 512; k++) TANH[k] = (int16_t)lround(32767.0 * tanh(k / 128.0));
    s_tables = 1;
}

// phase：周期内的位置 Q32；返回 sin Q15
static inline int32_t sin_q32(uint32_t ph) {
    uint32_t k = ph >> 22, f = (ph >> 6) & 0xFFFF;
    int32_t a = SIN[k], b = SIN[k + 1];
    return a + (((b - a) * (int32_t)f) >> 16);
}
// sin(2π·hz·i/SR)，hz 以 0.1 Hz 为单位
static inline int32_t sin_at(uint32_t i, uint32_t hz10) {
    uint64_t num = ((uint64_t)i * hz10) % ((uint64_t)NV_SR * 10);
    return sin_q32((uint32_t)((num << 32) / ((uint64_t)NV_SR * 10)));
}
static inline int32_t tanh_q15(int32_t x_q15) {   // 输入 Q15（实数 = x/32768）
    int32_t a = x_q15 < 0 ? -x_q15 : x_q15;
    int32_t u = a >> 8;                // 1/128 一格（Q15 → 32768/128 = 256）
    int32_t r = u >= 512 ? TANH[512] : TANH[u] + (((TANH[u + 1] - TANH[u]) * (a & 255)) >> 8);
    return x_q15 < 0 ? -r : r;
}

/* ---------------- 录音 ---------------- */

void nv_rec_start(nv_rec_t *r) {
    tables();
    r->n8 = 0; r->pred = 0; r->idx = 0; r->has_pend = 0; r->peak = 0;
}

static void encode(nv_rec_t *r, int32_t smp) {
    int32_t i = r->n8;
    nv_block_t *b = &r->blk[i / NV_BLOCK];
    if (i % NV_BLOCK == 0) { b->pred = (int16_t)r->pred; b->idx = (uint8_t)r->idx; b->pad = 0; memset(b->data, 0, sizeof b->data); }
    int32_t step = STEP[r->idx], diff = smp - r->pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    int32_t vp = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; vp += step; } step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; vp += step; } step >>= 1;
    if (diff >= step) { code |= 1; vp += step; }
    r->pred = CLAMP(r->pred + ((code & 8) ? -vp : vp), -32768, 32767);
    r->idx = CLAMP(r->idx + IDXT[code & 7], 0, 88);
    int32_t ap = r->pred < 0 ? -r->pred : r->pred;
    if (ap > r->peak) r->peak = ap;
    int j = i % NV_BLOCK;
    b->data[j >> 1] |= (uint8_t)((code & 15) << ((j & 1) * 4));
    r->n8++;
}

int nv_rec_push(nv_rec_t *r, const int16_t *x16, int n) {
    int k = 0;
    for (; k < n; k++) {
        if (!r->has_pend) { r->pend = x16[k]; r->has_pend = 1; continue; }
        if (r->n8 >= NV_MAX_8K) break;
        encode(r, ((int32_t)r->pend + x16[k] + 1) >> 1);   // 两两平均，四舍五入（同原型）
        r->has_pend = 0;
    }
    return k;
}

int32_t nv_gain_q8(const nv_rec_t *r) {
    if (!r->peak) return 256;
    int32_t g = (int32_t)((26214LL * 256) / r->peak);   // 峰值拉到 0.8
    return g > 1536 ? 1536 : g;                        // 增益不超过 6
}

/* ---------------- 解码 ---------------- */

void nv_dec_init(nv_dec_t *d, const nv_rec_t *r) { d->rec = r; d->blk[0] = d->blk[1] = -1; d->lru = 0; }

static void decode_block(const nv_rec_t *r, int32_t k, int16_t *out) {
    const nv_block_t *b = &r->blk[k];
    int32_t pred = b->pred, idx = b->idx, n = r->n8 - k * NV_BLOCK;
    if (n > NV_BLOCK) n = NV_BLOCK;
    for (int j = 0; j < n; j++) {
        int code = (b->data[j >> 1] >> ((j & 1) * 4)) & 15;
        int32_t step = STEP[idx], vp = step >> 3;
        if (code & 4) vp += step;
        if (code & 2) vp += step >> 1;
        if (code & 1) vp += step >> 2;
        pred = CLAMP(pred + ((code & 8) ? -vp : vp), -32768, 32767);
        idx = CLAMP(idx + IDXT[code & 7], 0, 88);
        out[j] = (int16_t)pred;
    }
}

int16_t nv_dec_get(nv_dec_t *d, int32_t i) {
    if (i < 0 || i >= d->rec->n8) return 0;
    int32_t k = i / NV_BLOCK;
    for (int c = 0; c < 2; c++) if (d->blk[c] == k) { d->lru = (uint8_t)(c ^ 1); return d->buf[c][i % NV_BLOCK]; }
    int c = d->lru;
    decode_block(d->rec, k, d->buf[c]);
    d->blk[c] = k; d->lru = (uint8_t)(c ^ 1);
    return d->buf[c][i % NV_BLOCK];
}

/* ---------------- 播放 ---------------- */

// 8 kHz 位置 t（Q16）插值读取并乘增益，返回 Q15
static inline int32_t src_q16(nv_player_t *p, nv_dec_t *d, uint64_t t) {
    int32_t i = (int32_t)(t >> 16), f = (int32_t)(t & 0xFFFF);
    int32_t a = nv_dec_get(d, i), b = nv_dec_get(d, i + 1);
    int32_t v = a + (int32_t)(((int64_t)(b - a) * f) >> 16);
    return (int32_t)(((int64_t)v * p->g_q8) >> 8);
}
// 第 i 个输出点读 i·num/den（8 kHz 单位）
static inline int32_t src_rate(nv_player_t *p, nv_dec_t *d, uint32_t i, uint32_t num, uint32_t den) {
    return src_q16(p, d, (((uint64_t)i * num) << 16) / den);
}
// 调制延迟：从历史 x 里读 i - (D+2) - D·sin(2π·hz·i/SR)
static inline int32_t vib_read(const nv_player_t *p, uint32_t i, int32_t D, uint32_t hz10) {
    int64_t pos = ((int64_t)i << 16) - ((int64_t)(D + 2) << 16) - (int64_t)D * sin_at(i, hz10) * 2;   // D·sin(Q15)·2 = Q16
    if (pos < 0) return 0;
    int32_t k = (int32_t)(pos >> 16), f = (int32_t)(pos & 0xFFFF);
    int32_t a = p->hist[k % NV_HIST], b = p->hist[(k + 1) % NV_HIST];
    return a + (int32_t)(((int64_t)(b - a) * f) >> 16);
}

static uint32_t mulberry32(uint32_t *a) {
    *a += 0x6D2B79F5u;
    uint32_t t = *a;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return t ^ (t >> 14);
}

void nv_play_start(nv_player_t *p, const nv_rec_t *r, int voice) {
    tables();
    memset(p, 0, sizeof *p);
    p->rec = r; p->voice = voice; p->g_q8 = nv_gain_q8(r); p->L2 = (uint32_t)r->n8 * 2;
    nv_dec_init(&p->da, r); nv_dec_init(&p->db, r);
    uint32_t n8 = (uint32_t)r->n8;
    switch (voice) {
    case NV_CHIPMUNK: p->n_out = n8 * 4 / 3; break;
    case NV_OLDMAN:   p->n_out = n8 * 200 / 72; break;
    case NV_ALIEN:    p->n_out = n8 * 200 / 135; break;
    case NV_ECHO:     p->n_out = p->L2 + NV_SR; break;
    case NV_WALKIE:   p->n_out = p->L2 + NV_SR * 3 / 10; p->rng = 1234; break;
    case NV_LOWBAT:   p->n_out = 0; break;
    case NV_STUTTER:  p->n_out = 0; p->lcg = 1; p->seg0 = 0; p->seg_u = 0; p->seg_rep = 0; break;
    default:          p->n_out = p->L2; break;
    }
    if (!r->n8) p->done = 1;
}

uint32_t nv_play_length(const nv_player_t *p) { return p->n_out ? p->n_out : (p->done ? p->i : 0); }

// 基础流：原速 x(i) = src(i/2)，同时记进历史
static inline int32_t base_x(nv_player_t *p, uint32_t i) {
    int32_t x = i < p->L2 ? src_rate(p, &p->da, i, 1, 2) : 0;
    p->hist[i % NV_HIST] = x;
    return x;
}

static inline int32_t ramp(int32_t value, uint32_t left, uint32_t full) {
    return left >= full ? value : (int32_t)((int64_t)value * left / full);
}

/* Same phase recurrence as LOWBAT playback. The lookahead runs once, only when
 * <=80 source samples remain (<=201 output steps at its minimum0.4 speed). */
static inline uint64_t lowbat_step(uint64_t q) {
    uint64_t q2 = (q * q) >> 32;
    return (1ULL << 32) - ((q2 * 2576980378ULL) >> 32);
}
static uint32_t lowbat_remaining(uint64_t position, uint64_t length) {
    uint64_t end = (length - 1) << 32;
    uint32_t count = 0;
    while (position < end) { position += lowbat_step(position / length); count++; }
    return count;
}

static int32_t next_sample(nv_player_t *p) {
    uint32_t i = p->i;
    switch (p->voice) {
    case NV_CHIPMUNK: return src_rate(p, &p->da, i, 3, 4);
    case NV_OLDMAN:   return src_rate(p, &p->da, i, 9, 25);
    case NV_ORIGINAL: return src_rate(p, &p->da, i, 1, 2);
    case NV_REVERSE:  return src_q16(p, &p->da, ((uint64_t)(p->L2 - 1 - i)) << 15);
    case NV_ROBOT: {
        int32_t x = src_rate(p, &p->da, i, 1, 2);
        int64_t y = ((int64_t)x * sin_at(i, 700) >> 15) * 39322 >> 15;   // ×sin×1.2（39322 = 1.2·32768）
        return (int32_t)(((y + 1024) >> 11) * 2048);                          // 16 级量化：round(v·16)/16
    }
    case NV_ECHO: {
        int32_t x = i < p->L2 ? src_rate(p, &p->da, i, 1, 2) : 0;
        int32_t *slot = &p->echo[i % NV_ECHO_DL];
        int32_t y = x + (int32_t)(((int64_t)*slot * 29491) >> 16);    // y[i-dl]×0.45
        *slot = y;
        return (int32_t)(((int64_t)y * 39322) >> 16);                 // ×0.6
    }
    case NV_VIBRATO: { base_x(p, i); return vib_read(p, i, 72, 60); }
    case NV_ALIEN: {
        p->hist[i % NV_HIST] = src_rate(p, &p->da, i, 27, 40);        // 1.35 倍速的流
        return vib_read(p, i, 32, 110);
    }
    case NV_CHORUS: {
        int32_t x = base_x(p, i);
        int32_t a = vib_read(p, i, 192, 8), b = vib_read(p, i, 288, 13), hi = src_rate(p, &p->db, i, 63, 100);
        return (int32_t)(((int64_t)x * 26214 + (int64_t)a * 20972 + (int64_t)b * 20972 + (int64_t)hi * 13107) >> 16);
    }
    case NV_WALKIE: {
        if (i < p->L2) {
            int32_t x = src_rate(p, &p->da, i, 1, 2);
            p->hp = (int32_t)(((int64_t)(p->hp + x - p->px) * 58982) >> 16);   // 0.9
            p->px = x;
            p->lp += (int32_t)(((int64_t)(p->hp - p->lp) * 29491) >> 16);      // 0.45
            int32_t y = (int32_t)(((int64_t)tanh_q15(p->lp * 4) * 39322) >> 16); // tanh(4·lp)×0.6
            return ramp(y, p->L2 - 1 - i, NV_EDGE_RAMP);
        }
        uint32_t tail = p->n_out - p->L2, k = i - p->L2;
        int32_t nz = (int32_t)(mulberry32(&p->rng) >> 16) - 32768;          // (2R−1)·32768
        int32_t y = (int32_t)(((int64_t)nz * (tail - k) / tail) >> 2); // ×0.25×(1−k/tail)
        return ramp(y, k, NV_EDGE_RAMP); /* speech and hiss meet at zero */
    }
    default: return 0;
    }
}

int nv_play_fill(nv_player_t *p, int16_t *out, int n) {
    int w = 0;
    while (w < n && !p->done) {
        int32_t v;
        uint32_t end_left = UINT32_MAX;
        if (p->voice == NV_LOWBAT) {
            uint64_t L = p->L2;
            if (p->p_q32 >= ((L - 1) << 32)) { p->done = 1; break; }
            if (!p->fade_end && ((L - 1) << 32) - p->p_q32 <= ((uint64_t)NV_EDGE_RAMP << 32))
                p->fade_end = p->i + lowbat_remaining(p->p_q32, L);
            uint64_t q = p->p_q32 / L;                                   // Q32
            v = src_q16(p, &p->da, p->p_q32 >> 17);                       // p/2 → Q16
            if (q > 3650722201ULL) v = (int32_t)((int64_t)v * (int64_t)(((1ULL << 32) - q) >> 16) * 20 / 3 >> 16);   // q>0.85：×(1−q)/0.15
            p->p_q32 += lowbat_step(q);                                  // 速度 1 − 0.6q²
            end_left = p->fade_end ? p->fade_end - 1 - p->i : UINT32_MAX;
        } else if (p->voice == NV_STUTTER) {
            if (p->seg_u == 0 && p->seg_rep == 0) {                       // 新的一段
                if (p->seg0 >= p->L2) { p->done = 1; break; }
                p->lcg = (uint32_t)(((uint64_t)p->lcg * 16807) % 2147483647);
                p->seg_rep = p->lcg % 4 == 0 ? 3 : 1;
                p->seg_len = p->L2 - p->seg0 < 1760 ? p->L2 - p->seg0 : 1760;
            }
            v = src_rate(p, &p->da, p->seg0 + p->seg_u, 1, 2);
            uint32_t left = p->seg_len - 1 - p->seg_u;
            if (p->lcg % 4 == 0) {
                uint32_t edge = NV_SPLICE_RAMP;
                if (p->seg_rep < 3 && p->seg_u < edge) edge = p->seg_u;
                if (p->seg_rep > 1 && left < edge) edge = left;
                v = ramp(v, edge, NV_SPLICE_RAMP);
            }
            if (p->seg0 + p->seg_len == p->L2 && p->seg_rep == 1) end_left = left;
            if (++p->seg_u >= p->seg_len) { p->seg_u = 0; if (--p->seg_rep == 0) p->seg0 += p->seg_len; }
        } else {
            if (p->i >= p->n_out) { p->done = 1; break; }
            v = next_sample(p);
            end_left = p->n_out - 1 - p->i;
        }
        uint32_t edge = p->i < NV_EDGE_RAMP ? p->i : NV_EDGE_RAMP;
        if (end_left < edge) edge = end_left;
        v = ramp(v, edge, NV_EDGE_RAMP);
        out[w++] = (int16_t)CLAMP(v, -32768, 32767);
        p->i++;
    }
    return w;
}
