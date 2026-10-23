// 宿主测试用：整段信号逐帧跑 nabo_mic，把每帧结果写进平铺数组
#include "nabo_mic.h"
static nm_state_t st;
int nm_run(const int16_t *x, int hops, int sens, int16_t *db, int16_t *dbs, uint8_t *bands, uint8_t *wf,
           uint8_t *onset, uint16_t *bpm, uint16_t *f0, uint16_t *f0c, uint8_t *chroma, int8_t *chord) {
    nm_init(&st, sens);
    for (int h = 0; h < hops; h++) {
        const nm_result_t *r = nm_process(&st, x + h * NM_HOP);
        db[h] = r->db_x100; dbs[h] = r->db_s_x100; onset[h] = r->onset; bpm[h] = r->bpm_x10;
        f0[h] = r->f0_x10; f0c[h] = r->f0_conf_x1000; chord[h] = r->chord;
        for (int i = 0; i < NM_BANDS; i++) bands[h * NM_BANDS + i] = r->bands[i];
        for (int i = 0; i < NM_WF_COLS; i++) wf[h * NM_WF_COLS + i] = r->wf_row[i];
        for (int i = 0; i < 12; i++) chroma[h * 12 + i] = r->chroma[i];
    }
    return (int)sizeof(nm_state_t);
}
