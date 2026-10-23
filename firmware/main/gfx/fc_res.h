/* 素材总包（FNAS）、调色板图像（FNBO/FNBR）、点阵字体（FNFT）、文本表（FNTX）的只读访问。
 * 设备上总包来自 esp_partition_mmap 的 assets 分区，宿主机上来自整个文件读入内存；数据都原地读取。 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void fc_res_init(const uint8_t *blob, uint32_t size);
const uint8_t *fc_res(const char *name, uint32_t *size);

/* ---- 图像 ---- */
typedef struct {
  const uint8_t *data;      /* 文件起点 */
  const uint8_t *pal;       /* 63 × RGB565（小端） */
  const uint8_t *offs;      /* 帧偏移表 */
  uint16_t w, h, frames;
  uint8_t loop;
} fc_img_t;
bool fc_img_open(const char *name, fc_img_t *img);
/* 索引帧可能在整行边界分为两块；逐行访问不要求堆上有 w×h 连续空间。
 * 连续帧 part[1]=NULL、split=h；fc_img_row 要求 0<=y<h。
 * 同一 epoch 内 surface/行指针不会被后续请求修改/淘汰；下一 epoch 才允许复用。 */
typedef struct { const uint8_t *part[2]; uint16_t w, split; } fc_img_surface_t;
const fc_img_surface_t *fc_img_surface(const fc_img_t *img, int frame);
static inline const uint8_t *fc_img_row(const fc_img_surface_t *s, int y) {
  return y < s->split ? s->part[0] + y * s->w : s->part[1] + (y - s->split) * s->w;
}
/* 返回连续 w×h 索引图（0 = 透明）；缓存分块或分配失败时返回 NULL。
 * 顺序播放解差分，回绕复用槽从头解码；指针生命周期与 surface 相同。 */
const uint8_t *fc_img_frame(const fc_img_t *img, int frame);
static inline uint16_t fc_img_color(const fc_img_t *img, uint8_t idx) {
  const uint8_t *p = img->pal + 2 * (idx - 1); return (uint16_t)(p[0] | (p[1] << 8));
}
void fc_img_cache_clear(void);
void fc_img_epoch(void);                /* 每帧合成开始时调用 */
uint32_t fc_img_cache_bytes(void);

/* ---- 字体 ---- */
typedef struct { const uint8_t *data; uint16_t n; uint8_t line_h, asc, space; } fc_font_t;
typedef struct { const uint8_t *bits; uint8_t w, h, adv; int8_t xo, yo; } fc_glyph_t;
bool fc_font_open(const char *name, fc_font_t *f);
bool fc_font_glyph(const fc_font_t *f, uint32_t cp, fc_glyph_t *g);
int fc_text_width(const fc_font_t *f, const char *utf8);
uint32_t fc_utf8_next(const char **s);

/* ---- 文本表 ---- */
const char *fc_txt(const char *table, int i);
