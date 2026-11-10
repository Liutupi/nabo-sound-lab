#pragma once
#include <stddef.h>
#include "sl_ui.h"
extern const sl_scene_t SC_CIRCUS;
extern uint16_t CIRCUS_BEST;                       /* 最高合计分 0..400；平台存 NVS 键 circus */
void sl_circus_status(char *out, size_t n);        /* 串口 status 用 */
