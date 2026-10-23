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
