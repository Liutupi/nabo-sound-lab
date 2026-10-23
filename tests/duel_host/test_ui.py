"""Production C UI rendered on host: pixels, glyphs, bounds and action mapping.

Temporary source copies add observation hooks only; repository sources stay intact.
These RGB565 screenshots are C renderer output, not optical device photographs.
"""
from pathlib import Path
import ctypes as C
import hashlib
import json
import os
import subprocess
import tempfile
from host_tools import find_tcc

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'firmware/main'
OUT = ROOT / 'docs/images'
TCC = find_tcc()

SHIM = r'''
#include "sl_ui.h"
#include <string.h>
#include <math.h>
/* Old Windows msvcrt used by portable TCC has log, without C99 log2. */
double log2(double x){return log(x)/0.69314718055994530942;}
typedef struct { int x,y,w,h,font,missing; char text[128]; } dui_text_t;
typedef struct { int size,x,y,t; char id[32]; } dui_sprite_t;
static dui_text_t texts[100];static int text_n;
static dui_sprite_t sprites[20];static int sprite_n;
void dui_reset(void){text_n=sprite_n=0;}
int dui_text_count(void){return text_n;}
const dui_text_t *dui_texts(void){return texts;}
int dui_sprite_count(void){return sprite_n;}
const dui_sprite_t *dui_sprites(void){return sprites;}
void dui_real_text_n(const fc_font_t *,int,int,const char *,int,uint16_t,int);
void fc_text_n(const fc_font_t *f,int x,int y,const char *s,int nb,uint16_t color,int align){
  if(text_n<100){dui_text_t *p=&texts[text_n++];memset(p,0,sizeof *p);
    int n=nb<127?nb:127;memcpy(p->text,s,n);p->text[n]=0;
    p->w=fc_text_width(f,p->text);p->h=f->line_h;p->x=align==AL_C?x-p->w/2:align==AL_R?x-p->w:x;p->y=y-f->line_h/2;
    p->font=f==&F_SANS16?16:f==&F_SANS14?14:f==&F_DISP18?18:f==&F_DISP24?24:f==&F_MONO22?22:f==&F_MONO48?48:0;
    /* Validate actual glyph ink; FNFT line boxes include blank descender rows. */
    int left=p->x,top=p->y,px=left,x0=999,y0=999,x1=-999,y1=-999;
    const char *q=p->text;fc_glyph_t g;while(*q){uint32_t cp=fc_utf8_next(&q);
      if(cp==' '){px+=f->space;continue;}if(!fc_font_glyph(f,cp,&g)){p->missing++;px+=f->space;continue;}
      if(g.w&&g.h){int a=px+g.xo,b=top+g.yo;if(a<x0)x0=a;if(b<y0)y0=b;if(a+g.w>x1)x1=a+g.w;if(b+g.h>y1)y1=b+g.h;}px+=g.adv;
    }if(x1>=x0){p->x=x0;p->y=y0;p->w=x1-x0;p->h=y1-y0;}
  }dui_real_text_n(f,x,y,s,nb,color,align);
}
void dui_real_nabo(const char *,int,int,int,int);
void nabo(const char *id,int size,int x,int y,int t){
  if(sprite_n<20){dui_sprite_t *p=&sprites[sprite_n++];p->size=size;p->x=x;p->y=y;p->t=t;strncpy(p->id,id,31);p->id[31]=0;}
  dui_real_nabo(id,size,x,y,t);
}
void dui_real_nabo_once(const char *,int,int,int,int);
void nabo_once(const char *id,int size,int x,int y,int t){
  if(sprite_n<20){dui_sprite_t *p=&sprites[sprite_n++];p->size=size;p->x=x;p->y=y;p->t=t;strncpy(p->id,id,31);p->id[31]=0;}
  dui_real_nabo_once(id,size,x,y,t);
}
'''

VARIANTS = r'''
/* Additional host-only state fixtures; production draw/update functions intact. */
void dui_variant(int v){
  if(v==1){D.stage=SD_SEARCH;D.room_n=0;}
  if(v==2){D.stage=SD_LOBBY;D.host=0;D.sync_n=0;}
  if(v==3){D.stage=SD_LOBBY;D.ready=D.peer_ready=1;}
  if(v==4){D.stage=SD_RESULT;D.got_score=D.peer_got_score=0;}
  if(v==5){D.stage=SD_LOST;start_failed=true;}
  if(v>=6&&v<=8){D.stage=SD_PLAY;feedback=v==6?100:v==7?60:0;feedback_at=sd_radio_now();}
  if(v==9){D.stage=SD_PLAY;muted=true;}
  if(v==10){D.stage=SD_CHOOSE;choice=1;}
}
'''


class Text(C.Structure):
    _fields_ = [(k, C.c_int) for k in ('x', 'y', 'w', 'h', 'font', 'missing')] + [('text', C.c_char * 128)]


class Sprite(C.Structure):
    _fields_ = [(k, C.c_int) for k in ('size', 'x', 'y', 't')] + [('id', C.c_char * 32)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(directory):
    modified = {}
    for name, old, new in [('gfx/fc_gfx.c', 'void fc_text_n(', 'void dui_real_text_n('),
                           ('ui/sl_ui.c', 'void nabo(', 'void dui_real_nabo(')]:
        source = SRC / name
        copy = directory / source.name
        contents = source.read_text(encoding='utf-8')
        assert contents.count(old) == 1
        contents = contents.replace(old, new)
        if name == 'ui/sl_ui.c':
            contents = contents.replace('void nabo_once(', 'void dui_real_nabo_once(')
        copy.write_text(contents, encoding='utf-8')
        modified[source] = copy
    source = SRC / 'ui/sc_duel.c'
    copy = directory / source.name
    copy.write_text(source.read_text(encoding='utf-8') + VARIANTS, encoding='utf-8')
    modified[source] = copy
    shim = directory / 'probe.c'
    shim.write_text(SHIM, encoding='utf-8')
    sources = sorted((SRC / 'ui').glob('*.c')) + [SRC / 'gfx/fc_gfx.c', SRC / 'gfx/fc_res.c',
                SRC / 'mic/nabo_mic.c', SRC / 'voice/nabo_voice.c', ROOT / 'tests/ui_host/sl_host.c']
    command = [str(TCC), '-shared', '-rdynamic', '-I' + str(SRC / 'gfx'), '-I' + str(SRC / 'ui'),
               '-I' + str(SRC / 'mic'), '-I' + str(SRC / 'voice')]
    command += [str(modified.get(s, s)) for s in sources] + [str(shim), '-o', str(directory / 'ui.dll')]
    completed = subprocess.run(command, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    return directory / 'ui.dll', {str(p.relative_to(ROOT)): sha(p) for p in sources}


def main():
    OUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nabo-duel-ui-', ignore_cleanup_errors=True) as temp:
        dll, bound = build(Path(temp))
        lib = C.CDLL(str(dll))
        lib.host_fb.restype = C.POINTER(C.c_uint16)
        lib.dui_texts.restype = C.POINTER(Text)
        lib.dui_sprites.restype = C.POINTER(Sprite)
        lib.fc_res.restype = C.c_void_p
        lib.fc_res.argtypes = [C.c_char_p, C.POINTER(C.c_uint32)]
        blob = ROOT / 'firmware/assets/soundlab_assets.bin'
        assert lib.host_init(str(blob).encode(), 7) == 0
        vt = C.c_uint32.in_dll(lib, 'VT')
        cases = [
            ('choose', 0, 1, 0, 'S-HEADPHONE'), ('search-four-rooms', 1, 1, 0, None),
            ('host-room', 2, 1, 0, 'S-HEADPHONE'), ('join-request', 3, 1, 0, 'S-HEADPHONE'),
            ('join-wait', 4, 1, 0, 'S-HEADPHONE'), ('lobby', 5, 1, 0, 'S-HEADPHONE'),
            ('armed', 6, 1, 0, 'S-HEADPHONE'), ('play', 7, 1, 0, 'S-DRUM'),
            ('round-result', 8, 1, 0, 'S-CHEER'), ('final-win', 9, 1, 0, 'S-CHEER'),
            ('final-tie', 9, 0, 0, 'S-NOD'), ('final-loss', 9, -1, 0, 'S-OOPS'),
            ('disconnected', 10, 1, 0, 'S-OOPS'), ('search-empty', 1, 1, 1, 'S-HEADPHONE'),
            ('syncing', 5, 1, 2, 'S-HEADPHONE'), ('both-ready', 5, 1, 3, 'S-HEADPHONE'),
            ('result-pending', 8, 1, 4, 'S-HEADPHONE'), ('radio-error', 10, 1, 5, 'S-OOPS'),
            ('perfect', 7, 1, 6, 'S-DRUM'), ('hit', 7, 1, 7, 'S-DRUM'),
            ('extra', 7, 1, 8, 'S-DRUM'), ('muted', 7, 1, 9, 'S-DRUM'),
            ('choose-search', 0, 1, 10, 'S-HEADPHONE'),
            ('final-host-ack-lost', 9, 98, 0, 'S-CHEER')]
        resource_bindings = {}
        for actor in sorted({case[4] for case in cases if case[4]}):
            name = f'spr/{actor}/76'; size = C.c_uint32(); pointer = lib.fc_res(name.encode(), C.byref(size))
            assert pointer and size.value > 137, f'expected native sprite missing; IDLE fallback forbidden: {name}'
            data = C.string_at(pointer, size.value)
            assert data[:4] == b'FNBO' and int.from_bytes(data[6:8], 'little') == 76
            resource_bindings[name] = {'bytes': size.value, 'frames': int.from_bytes(data[8:10], 'little'), 'sha256': hashlib.sha256(data).hexdigest()}
        records = []; frames = 0; max_ops = 0; violations = []; shots = []; overlap_count = 0
        for index, (name, stage, outcome, variant, expected) in enumerate(cases):
            lib.host_go(b'duel'); vt.value = 20000 + index * 4000
            lib.sl_duel_fixture(stage, outcome); lib.dui_variant(variant)
            texts = []; sprites = []
            for frame in range(12):
                lib.dui_reset()
                mismatch, ops = C.c_int(), C.c_int()
                lib.host_frame(0, C.byref(mismatch), C.byref(ops)); frames += 1
                assert mismatch.value == 0, (name, frame, mismatch.value)
                assert not ops.value & (1 << 30), (name, frame, 'display-list overflow')
                max_ops = max(max_ops, ops.value)
                texts = [{k: getattr(t, k) for k in ('x', 'y', 'w', 'h', 'font', 'missing')} |
                         {'text': t.text.decode('utf-8')} for t in lib.dui_texts()[:lib.dui_text_count()]]
                sprites = [{'id': s.id.decode(), 'size': s.size, 'x': s.x, 'y': s.y, 't': s.t}
                           for s in lib.dui_sprites()[:lib.dui_sprite_count()]]
                for t in texts:
                    if t['missing'] or t['x'] < 0 or t['x'] + t['w'] > 240 or t['y'] < 0 or t['y'] + t['h'] > 320:
                        violations.append({'case': name, 'frame': frame, **t})
                    if any('\u4e00' <= ch <= '\u9fff' for ch in t['text']) and t['font'] < 16:
                        violations.append({'case': name, 'frame': frame, 'reason': 'Chinese smaller than 16', **t})
                    if t['text'].isdigit() or (t['text'][:1].isdigit() and ':' in t['text']):
                        if t['font'] != 22:
                            violations.append({'case': name, 'frame': frame, 'reason': 'score/room number must use 22px', **t})
                for i, a in enumerate(texts):
                    for b in texts[i + 1:]:
                        if min(a['x'] + a['w'], b['x'] + b['w']) > max(a['x'], b['x']) and min(a['y'] + a['h'], b['y'] + b['h']) > max(a['y'], b['y']):
                            overlap_count += 1
                            violations.append({'case': name, 'frame': frame, 'reason': 'text ink overlap', 'a': a, 'b': b})
                assert [s['id'] for s in sprites] == ([] if expected is None else [expected]), (name, sprites)
                if frame == 0:
                    fb = np.ctypeslib.as_array(lib.host_fb(), shape=(240 * 320,)).copy().astype(np.uint32)
                    rgb = np.stack([((fb >> 11) & 31) * 255 // 31, ((fb >> 5) & 63) * 255 // 63, (fb & 31) * 255 // 31], -1)
                    im = Image.fromarray(rgb.reshape(320, 240, 3).astype(np.uint8))
                    shot = OUT / f'duel-firmware-{name}.png'; im.save(shot); shots.append((name, im))
                vt.value += 33
            records.append({'case': name, 'stage': stage, 'outcome': outcome, 'variant': variant,
                            'text': texts, 'sprite': sprites, 'native_png': f'docs/images/duel-firmware-{name}.png'})
        # Real production countdown branch, using the same fixture then advancing its host clock.
        lib.host_go(b'duel'); vt.value = 160000; lib.sl_duel_fixture(7, 1); vt.value -= 5 * (60000 // 105)
        for i in range(4):
            lib.dui_reset(); mismatch, ops = C.c_int(), C.c_int(); lib.host_frame(0, C.byref(mismatch), C.byref(ops)); frames += 1
            assert mismatch.value == 0 and not ops.value & (1 << 30)
            countdown = [t.text.decode('utf-8') for t in lib.dui_texts()[:lib.dui_text_count()] if t.font == 48]
            assert countdown == [str(4 - i)], countdown
            fb = np.ctypeslib.as_array(lib.host_fb(), shape=(240 * 320,)).copy().astype(np.uint32)
            rgb = np.stack([((fb >> 11) & 31) * 255 // 31, ((fb >> 5) & 63) * 255 // 63, (fb & 31) * 255 // 31], -1)
            im = Image.fromarray(rgb.reshape(320, 240, 3).astype(np.uint8)); name = f'countdown-{4-i}'
            im.save(OUT / f'duel-firmware-{name}.png'); shots.append((name, im))
            vt.value += 60000 // 105
        cols = 6; cell_w = 260; cell_h = 358; rows = (len(shots) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * cell_w, rows * cell_h + 40), '#0A0716'); draw = ImageDraw.Draw(sheet)
        font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 16)
        draw.text((12, 10), 'PRODUCTION C RGB565 RENDER / NOT A DEVICE PHOTO', font=font, fill='#35f0dc')
        for i, (name, im) in enumerate(shots):
            x = i % cols * cell_w + 10; y = i // cols * cell_h + 40
            sheet.paste(im, (x, y)); draw.text((x, y + 325), name, font=font, fill='#ece8ff')
        sheet.save(OUT / 'duel-firmware-overview.png')
        report = {'scope': 'production C UI host RGB565 rendering, not optical photographs or wireless device verification',
                  'frames': frames, 'states_and_variants': len(cases), 'incremental_full_pixel_mismatches': 0,
                  'display_list_overflows': 0, 'max_display_ops': max_ops, 'action_mapping_checked': True,
                  'text_ink_overlaps': overlap_count, 'chinese_minimum_nominal_px': 16, 'scores_nominal_px': 22,
                  'native_resolution': [240, 320], 'missing_glyphs': sum(t['missing'] for r in records for t in r['text']),
                  'countdown_verified': [4, 3, 2, 1],
                  'native_sprite_resources': resource_bindings,
                  'violations': violations, 'source_sha256': bound, 'assets_sha256': sha(blob), 'cases': records}
        (HERE / 'ui-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        summary = {k: v for k, v in report.items() if k not in ('cases', 'source_sha256', 'violations', 'native_sprite_resources')}
        summary['violation_count'] = len(violations); summary['first_violations'] = violations[:5]
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        assert not violations, f'{len(violations)} UI glyph/size/bounds violations; see ui-validation.json'


if __name__ == '__main__':
    main()
