// nabo_mic.c —— 规则见 prototype/index.html 的 analyzeHop / yin / estimateBpm / chromaStep
#include "nabo_mic.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

#define CLAMP(v, a, b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))

// ---------- 数学工具 ----------
// log2(1+i/32) 的 Q16 表，33 项，中间线性插值，误差 < 0.0005
static const uint32_t LOG2_TAB[33] = {
    0, 2909, 5732, 8473, 11136, 13727, 16248, 18704, 21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346, 38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207, 52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047, 65536};

static int clz64(uint64_t v) {
#if defined(__GNUC__)
    return v ? __builtin_clzll(v) : 64;
#else
    int n = 0; while (!(v & (1ULL << 63)) && n < 64) { v <<= 1; n++; } return n;
#endif
}

// log2(p) 的 Q16（p>0）
static int32_t log2_q16(uint64_t p) {
    int msb = 63 - clz64(p);
    uint32_t frac16;                       // 尾数小数部分 Q16
    if (msb >= 16) frac16 = (uint32_t)((p >> (msb - 16)) & 0xFFFF);
    else frac16 = (uint32_t)((p << (16 - msb)) & 0xFFFF);
    uint32_t idx = frac16 >> 11, rem = frac16 & 0x7FF;   // 32 段，每段 2048
    int32_t a = LOG2_TAB[idx], b = LOG2_TAB[idx + 1];
    return (msb << 16) + a + (int32_t)(((b - a) * (int32_t)rem) >> 11);
}

int32_t nm_db10_x100(uint64_t p) {
    if (!p) return NM_DB_NONE - 10000;
    // 1000*log10(p) = 301.03 * log2(p)
    return (int32_t)(((int64_t)log2_q16(p) * 30103) / (100LL << 16) * 1);
}

uint32_t nm_isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

// ---------- FFT：int32 原位基 2，Q15 旋转因子（来自 1024 点表） ----------
static void fft(nm_state_t *st, int n) {
    int32_t *re = st->re, *im = st->im;
    int lev = 0; while ((1 << lev) < n) lev++;
    for (int i = 0; i < n; i++) {
        int r = 0; for (int b = 0; b < lev; b++) r |= ((i >> b) & 1) << (lev - 1 - b);
        if (r > i) { int32_t t = re[i]; re[i] = re[r]; re[r] = t; t = im[i]; im[i] = im[r]; im[r] = t; }
    }
    for (int size = 2; size <= n; size <<= 1) {
        int half = size >> 1, step = 1024 / size;
        for (int i = 0; i < n; i += size) {
            for (int j = 0; j < half; j++) {
                int k = j * step, a = i + j, b = a + half;
                int32_t c = st->tw_cos[k], s = st->tw_sin[k];
                int32_t tr = (int32_t)(((int64_t)re[b] * c - (int64_t)im[b] * s) >> 15);
                int32_t ti = (int32_t)(((int64_t)re[b] * s + (int64_t)im[b] * c) >> 15);
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
            }
        }
    }
}

static inline uint64_t pw(const nm_state_t *st, int k) {
    int64_t r = st->re[k], i = st->im[k];
    return (uint64_t)(r * r) + (uint64_t)(i * i);
}

// ---------- 初始化（建表时用浮点，只跑一次） ----------
static void band_edges(int n, double lo, double hi, double df, int nb, uint8_t *alo, uint8_t *ahi) {
    for (int i = 0; i < n; i++) {
        double e0 = lo * pow(hi / lo, (double)i / n), e1 = lo * pow(hi / lo, (double)(i + 1) / n);
        int a = (int)floor(e0 / df + 0.5), b = (int)floor(e1 / df + 0.5) - 1;
        a = CLAMP(a, 1, nb - 1);
        if (b < a) b = a;
        b = CLAMP(b, a, nb - 1);
        alo[i] = (uint8_t)a; ahi[i] = (uint8_t)b;
    }
}

void nm_set_sens(nm_state_t *st, int sens_db) { st->sens_db = (int16_t)sens_db; }
void nm_set_features(nm_state_t *st, uint8_t mask) { st->features = mask; }

void nm_init(nm_state_t *st, int sens_db) {
    memset(st, 0, sizeof(*st));
    st->sens_db = (int16_t)sens_db;
    st->features = NM_F_ALL;
    for (int k = 0; k < 512; k++) {
        st->tw_cos[k] = (int16_t)lround(32767.0 * cos(2 * M_PI * k / 1024));
        st->tw_sin[k] = (int16_t)lround(-32767.0 * sin(2 * M_PI * k / 1024));
    }
    for (int i = 0; i < 512; i++) st->win512[i] = (int16_t)lround(32767.0 * (0.5 - 0.5 * cos(2 * M_PI * i / 512)));
    for (int i = 0; i < 1024; i++) st->win1024[i] = (int16_t)lround(32767.0 * (0.5 - 0.5 * cos(2 * M_PI * i / 1024)));
    band_edges(NM_BANDS, 60, 7000, NM_SR / 512.0, 256, st->band_lo, st->band_hi);
    band_edges(NM_WF_COLS, 60, 8000, NM_SR / 512.0, 256, st->col_lo, st->col_hi);
    const double df8 = NM_SR / 2.0 / 1024;
    for (int k = NM_CHROMA_K0; k < NM_CHROMA_K1; k++) {
        int m = (int)floor(69 + 12 * log2(k * df8 / 440) + 0.5);
        st->pc_of_bin[k] = (uint8_t)(((m % 12) + 12) % 12);
    }
    for (int L = 0; L < 52; L++) {
        double w = L ? exp(-0.5 * pow(log2(L * 0.02 / 0.5) / 0.9, 2)) : 0;
        st->bpm_w_q12[L] = (uint16_t)lround(w * 4096);
    }
    st->out.db_x100 = NM_DB_NONE; st->out.db_s_x100 = -10000;  // 和原型 A.dbS 初值一致
    st->out.chord = -1;
}

void nm_last(const nm_state_t *st, int16_t *dst, int n) {
    int p = (st->wp - n + NM_RING) & (NM_RING - 1);
    for (int i = 0; i < n; i++) dst[i] = st->ring[(p + i) & (NM_RING - 1)];
}

// ---------- YIN（1024 个采样，W=512） ----------
static void yin(nm_state_t *st, const int16_t *x, uint16_t *f_x10, uint16_t *conf) {
    // 规则同原型 yin()：CMND < 0.15 的第一个低谷，再往下走到局部最小，抛物线插值。
    // 优化（结果不变）：边算 d(τ) 边找，找到低谷并确认下一点不再下降就停，不必算满 267 个 τ。
    // 差值先除 2（平方 ≤ 2^30），4 个一组在 32 位里累加，再并进 64 位：内层循环省掉大部分 64 位加法。
    const int tmin = NM_SR / 1100, tmax = NM_YIN_TMAX;
    uint32_t *d = st->yd_q16;
    uint64_t run = 0;
    int found = -1;
    d[0] = 65536;
    for (int tau = 1; tau <= tmax; tau++) {
        uint64_t s = 0;
        const int16_t *a = x, *b = x + tau;
        for (int i = 0; i < NM_YIN_W; i += 4) {
            int32_t v0 = (a[i] - b[i]) >> 1, v1 = (a[i + 1] - b[i + 1]) >> 1, v2 = (a[i + 2] - b[i + 2]) >> 1, v3 = (a[i + 3] - b[i + 3]) >> 1;
            uint32_t q = (uint32_t)(v0 * v0) + (uint32_t)(v1 * v1) + (uint32_t)(v2 * v2) + (uint32_t)(v3 * v3);   // 每项 ≤ 2^30
            s += q;
        }
        run += s;
        if (!run) d[tau] = 65536;
        else {
            uint64_t num = s * (uint64_t)tau, den = run;
            while (num >= (1ULL << 47)) { num >>= 1; den >>= 1; }
            uint64_t q = den ? (num << 16) / den : 0xFFFFFFFFu;
            d[tau] = q > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)q;
        }
        if (found < 0) { if (tau >= tmin && tau < tmax && d[tau] < 9830) found = tau; }
        else if (tau < tmax && d[tau] < d[found]) found = tau;   // 原型：while (d[τ+1] < d[τ]) τ++
        else break;                                             // 已经有 d[found+1]，够做插值
    }
    int best = found;
    if (best < 0) { *f_x10 = 0; *conf = 0; return; }
    // A noisy first-period valley can narrowly miss 0.15 while a multiple
    // crosses it. Do not report a confident subharmonic when a shorter local
    // minimum is almost as good (within 0.03 CMND) at an integer submultiple.
    // Reject this ambiguous frame; choosing the shorter lag could instead
    // octave-shift a real harmonic-rich low note. No extra difference sums.
    for (int tau = tmin; tau + 1 < best; tau++) {
        if (d[tau] <= d[tau - 1] && d[tau] < d[tau + 1] &&
            d[tau] <= d[best] + 1966u) {
            int multiple = (best + tau / 2) / tau;
            int delta = best - multiple * tau;
            if (multiple >= 2 && delta >= -multiple && delta <= multiple) {
                *f_x10 = 0; *conf = 0; return;
            }
        }
    }
    int64_t a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c;
    int64_t sh = den ? ((a - c) * 128) / den : 0;   // Q8
    sh = CLAMP(sh, -256, 256);
    *f_x10 = (uint16_t)((uint64_t)NM_SR * 10 * 256 / (uint64_t)(best * 256 + sh));
    int32_t cf = 1000 - (int32_t)(((int64_t)b * 1000) >> 16);
    *conf = (uint16_t)CLAMP(cf, 0, 1000);
}

// ---------- BPM（6 秒包络自相关） ----------
static void estimate_bpm(nm_state_t *st) {
    static int32_t e[NM_ENV];
    int64_t m = 0;
    for (int i = 0; i < NM_ENV; i++) { e[i] = (int32_t)st->env[(st->ep + i) % NM_ENV]; m += e[i]; }
    m /= NM_ENV;
    int64_t r0 = 0;
    for (int i = 0; i < NM_ENV; i++) { e[i] -= (int32_t)m; r0 += (int64_t)e[i] * e[i]; }
    if (r0 <= 0) return;
    const int L0 = 15, L1 = 50;
    int64_t r[53] = {0}, best = 0; int bl = 0;
    for (int L = L0; L <= L1 + 1; L++) {
        int64_t s = 0;
        for (int i = L; i < NM_ENV; i++) s += (int64_t)e[i] * e[i - L];
        r[L] = s;
        int64_t sw = s * st->bpm_w_q12[L];
        if (L <= L1 && sw > best) { best = sw; bl = L; }
    }
    if (!bl) return;
    int64_t a = r[bl - 1], b = r[bl], c = r[bl + 1], den = a - 2 * b + c, sh = 0;
    if (den) { sh = ((a - c) * 128) / den; sh = CLAMP(sh, -128, 128); }
    uint32_t bpm = (uint32_t)(3000LL * 10 * 256 / (bl * 256 + sh));
    uint32_t conf = (uint32_t)(b * 1000 / r0);
    st->out.bpm_conf_x1000 = (uint16_t)(conf > 1000 ? 1000 : conf);
    if (conf > 80) {
        uint32_t old = st->out.bpm_x10;
        int32_t diff = (int32_t)bpm - (int32_t)old;
        st->out.bpm_x10 = (uint16_t)((old && diff < 60 && diff > -60) ? (old * 7 + bpm * 3) / 10 : bpm);
    }
}

// ---------- 音级（2048 个采样 → 8 kHz → 1024 点） ----------
static void chroma_step(nm_state_t *st) {
    int p = (st->wp - 2048 + NM_RING) & (NM_RING - 1);
    for (int i = 0; i < 1024; i++) {
        // 两个采样直接相加（不先除 2），加窗后只右移 10 位：比 >>15 多留 5 位精度，弱音级不被舍入噪声淹没。
        // 上限 65534×32767 < 2^31，1024 点累加后仍在 int32 内（音级会按最大值归一化，绝对比例无所谓）
        int32_t v = (int32_t)st->ring[(p + 2 * i) & (NM_RING - 1)] + st->ring[(p + 2 * i + 1) & (NM_RING - 1)];
        st->re[i] = (int32_t)(((int64_t)v * st->win1024[i]) >> 10); st->im[i] = 0;
    }
    fft(st, 1024);
    uint32_t ch[12] = {0}, mx = 0;
    for (int k = NM_CHROMA_K0; k < NM_CHROMA_K1; k++) ch[st->pc_of_bin[k]] += nm_isqrt64(pw(st, k));
    for (int i = 0; i < 12; i++) if (ch[i] > mx) mx = ch[i];
    int gate = st->out.db_x100 > -5000 && mx > 0;
    uint32_t nrm2 = 0;
    for (int i = 0; i < 12; i++) {
        uint32_t v = gate ? (uint32_t)(((uint64_t)ch[i] * 4095) / mx) : 0;
        st->chroma_q12[i] = (uint16_t)((st->chroma_q12[i] * 6 + v * 4) / 10);
        nrm2 += (uint32_t)st->chroma_q12[i] * st->chroma_q12[i];
        st->out.chroma[i] = (uint8_t)(st->chroma_q12[i] >> 4);
    }
    uint32_t nrm = nm_isqrt64(nrm2), best = 0; int name = -1;
    if (nrm > 1228) {
        for (int r = 0; r < 12; r++) for (int q = 0; q < 2; q++) {
            int iv = q ? 3 : 4;
            uint32_t s = (uint32_t)st->chroma_q12[r] + st->chroma_q12[(r + iv) % 12] + st->chroma_q12[(r + 7) % 12];
            uint32_t sc = (s * 37837u) / nrm;          // 65536/√3 = 37837
            if (sc > best) { best = sc; name = r * 2 + q; }
        }
    }
    st->out.chord_conf_x1000 = (uint16_t)((best * 1000) >> 16);
    st->out.chord = best > 48497 ? (int8_t)name : -1;  // 0.74
}

// ---------- 每帧 ----------
const nm_result_t *nm_process(nm_state_t *st, const int16_t *hop) {
    nm_result_t *o = &st->out;
    o->hop++;
    o->onset = 0;
    for (int i = 0; i < NM_HOP; i++) { st->ring[st->wp] = hop[i]; st->wp = (st->wp + 1) & (NM_RING - 1); }
    // 音量
    uint64_t ss = 0;
    for (int i = 0; i < NM_HOP; i++) ss += (uint64_t)((int32_t)hop[i] * hop[i]);
    int32_t db = ss ? nm_db10_x100(ss / NM_HOP) - 9031 : NM_DB_NONE;
    if (db < NM_DB_NONE) db = NM_DB_NONE;
    o->db_x100 = (int16_t)db;
    o->db_s_x100 = (int16_t)(db > o->db_s_x100 ? db : (o->db_s_x100 * 9 + db) / 10);
    // 512 点 FFT
    int p = (st->wp - 512 + NM_RING) & (NM_RING - 1);
    for (int i = 0; i < 512; i++) { st->re[i] = ((int32_t)st->ring[(p + i) & (NM_RING - 1)] * st->win512[i]) >> 15; st->im[i] = 0; }
    fft(st, 512);
    // 32 个频段
    const int32_t sens100 = st->sens_db * 100;
    uint32_t flux = 0;
    for (int b = 0; b < NM_BANDS; b++) {
        uint64_t m = 0;
        for (int k = st->band_lo[b]; k <= st->band_hi[b]; k++) { uint64_t q = pw(st, k); if (q > m) m = q; }
        int32_t dbb = nm_db10_x100(m) - 13245 + 7200 + sens100;   // (dB+72)
        int32_t v = CLAMP(dbb * 4095 / 6200, 0, 4095);
        if (v > st->raw_q12[b]) flux += (uint32_t)(v - st->raw_q12[b]);
        st->raw_q12[b] = (uint16_t)v;
        int32_t bq = st->band_q12[b];
        bq = v > bq ? v : (bq - 143 > v ? bq - 143 : v);
        st->band_q12[b] = (uint16_t)bq;
        int32_t pq = st->peak_q12[b];
        pq = bq > pq ? bq : (pq - 41 > bq ? pq - 41 : bq);
        st->peak_q12[b] = (uint16_t)pq;
        o->bands[b] = (uint8_t)(bq >> 4); o->peaks[b] = (uint8_t)(pq >> 4);
    }
    // 起音：通量 > 均值×1.7 + 0.45
    uint64_t mean = 0;
    for (int i = 0; i < 30; i++) mean += st->flux_hist[i];
    st->flux_hist[st->fh] = flux; st->fh = (st->fh + 1) % 30;
    if ((uint64_t)flux * 30 * 10 > mean * 17 + 18428ULL * 30 && db > -5500 &&
        (!st->has_onset || o->hop - st->last_onset_hop >= 7)) {
        o->onset = 1; st->has_onset = 1; st->last_onset_hop = o->hop;
    }
    st->env[st->ep] = flux; st->ep = (st->ep + 1) % NM_ENV;
    if ((st->features & NM_F_BPM) && o->hop % 25 == 3) estimate_bpm(st);   // 第 3 帧起每 0.5 s，避开音级帧
    // 声纹瀑布一行
    if (st->features & NM_F_WF) for (int j = 0; j < NM_WF_COLS; j++) {
        uint64_t m = 0;
        for (int k = st->col_lo[j]; k <= st->col_hi[j]; k++) { uint64_t q = pw(st, k); if (q > m) m = q; }
        int32_t x = nm_db10_x100(m) - 13245 + 8200 + sens100;
        int32_t lv = x <= 0 ? 0 : (x * 31 + 3300) / 6600;
        o->wf_row[j] = (uint8_t)CLAMP(lv, 0, 31);
    }
    // 音高：每 40 ms 一次
    if (o->hop % 2 == 0) {
        if ((st->features & NM_F_PITCH) && db > -5200) {
            static int16_t buf[1024];
            nm_last(st, buf, 1024);
            yin(st, buf, &o->f0_x10, &o->f0_conf_x1000);
        } else { o->f0_x10 = 0; o->f0_conf_x1000 = 0; }
    }
    // 音级：每 100 ms 一次
    if ((st->features & NM_F_CHROMA) && o->hop % 5 == 0) chroma_step(st);
    return o;
}
