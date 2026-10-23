/* 场景表 */
#include "sl_ui.h"
#include <stddef.h>
extern const sl_scene_t SC_BOOT, SC_MENU, SC_SETTINGS, SC_ECHO, SC_SHOUT, SC_CANDLE, SC_ROPE, SC_RANGE, SC_SPEC, SC_WATER, SC_SCOPE, SC_CHROMA, SC_DB,
    SC_TUNER, SC_GUITAR, SC_BEAT, SC_PITCH, SC_FLIGHT, SC_CLAP, SC_SAMPLER;
const sl_scene_t *const SL_SCENES[] = {&SC_BOOT, &SC_MENU, &SC_SETTINGS, &SC_ECHO, &SC_SHOUT, &SC_CANDLE, &SC_ROPE, &SC_RANGE, &SC_SPEC, &SC_WATER,
                                       &SC_SCOPE, &SC_CHROMA, &SC_DB, &SC_TUNER, &SC_GUITAR, &SC_BEAT, &SC_PITCH, &SC_FLIGHT, &SC_CLAP, &SC_SAMPLER, NULL};
