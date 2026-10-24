#include "nabo_voice.h"
static nv_rec_t rec;
static nv_player_t pl;
int nv_record(const int16_t *x16, int n) { nv_rec_start(&rec); nv_rec_push(&rec, x16, n); return rec.n8; }
int nv_decode_all(int16_t *out) { nv_dec_t d; nv_dec_init(&d, &rec); for (int i = 0; i < rec.n8; i++) out[i] = nv_dec_get(&d, i); return nv_gain_q8(&rec); }
int nv_render(int voice, int16_t *out, int maxn) {
    nv_play_start(&pl, &rec, voice);
    int n = 0, w;
    while (n < maxn && (w = nv_play_fill(&pl, out + n, maxn - n > 320 ? 320 : maxn - n)) > 0) n += w;   // 按 20 ms 一帧取，和固件一样
    return n;
}
int nv_sizes(int *rec_size) { *rec_size = (int)sizeof(nv_rec_t); return (int)sizeof(nv_player_t); }
int nv_record_chunked(const int16_t *x, int n, int chunk) {
    nv_rec_start(&rec);
    for (int at = 0; at < n;) {
        int part = n - at < chunk ? n - at : chunk;
        int used = nv_rec_push(&rec, x + at, part);
        at += used;
        if (used < part) break;
    }
    return rec.n8;
}
int nv_render_chunked(int voice, int16_t *out, int maxn, int chunk) {
    nv_play_start(&pl, &rec, voice);
    int n = 0, w;
    while (n < maxn && (w = nv_play_fill(&pl, out + n, maxn - n < chunk ? maxn - n : chunk)) > 0) n += w;
    return n;
}
