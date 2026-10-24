/* Host-only observations and platform hooks; production handlers stay intact. */
#include "sl_ui.h"
#include <stdio.h>
#include <string.h>
#include "sc_core.c"
#include "sc_echo.c"

_Static_assert(sizeof(sl_settings_t) == 10, "settings ABI must remain 10 bytes");

static int saves, applies, applied, missing, outside;
static char texts[4096];
void sl_settings_save(void) { saves++; }
void sl_volume_apply(uint8_t volume) { applied = volume; applies++; }
int ix_saves(void) { return saves; }
int ix_applies(void) { return applies; }
int ix_applied(void) { return applied; }
int ix_editing(void) { return s_volume_edit; }
int ix_echo_state(void) { return e_state; }
int ix_echo_voice(void) { return e_v; }
int ix_echo_mode(void) { return e_m; }
int ix_missing(void) { return missing; }
int ix_outside(void) { return outside; }
const char *ix_texts(void) { return texts; }
void ix_frame_reset(void) { texts[0] = 0; missing = outside = 0; }
void ix_real_text_n(const fc_font_t *, int, int, const char *, int, uint16_t, int);
void fc_text_n(const fc_font_t *font, int x, int y, const char *s, int nb,
               uint16_t color, int align) {
    char text[160];
    int n = nb < (int)sizeof(text) - 1 ? nb : (int)sizeof(text) - 1;
    memcpy(text, s, n); text[n] = 0;
    size_t used = strlen(texts);
    snprintf(texts + used, sizeof(texts) - used, "%d:%s\n", y, text);
    int width = fc_text_width(font, text);
    int px = align == AL_C ? x - width / 2 : align == AL_R ? x - width : x;
    int top = y - font->line_h / 2;
    const char *p = text;
    while (*p) {
        uint32_t cp = fc_utf8_next(&p);
        fc_glyph_t glyph;
        if (cp == ' ') { px += font->space; continue; }
        if (!fc_font_glyph(font, cp, &glyph)) { missing++; continue; }
        if (glyph.w && glyph.h && (px + glyph.xo < 0 || top + glyph.yo < 0 ||
            px + glyph.xo + glyph.w > FC_W || top + glyph.yo + glyph.h > FC_H)) outside++;
        px += glyph.adv;
    }
    ix_real_text_n(font, x, y, s, nb, color, align);
}
