"""Production C rendering regression for the 17 existing entries and wireless duel.

Runs the real UI, microphone analysis, voice/audio and rasterizer with deterministic
16 kHz inputs and key events. Temporary copies add observation/allocation hooks;
no production source or published package is edited. Three independent DLLs use
normal allocation, or reject each image-cache malloc larger than 13824/7680 B.
Every rendered frame compares incremental against forced-full rendering and the
two constrained profiles against the normal RGB565 framebuffer, pixel for pixel.

Output is private device-validation evidence, not device/RF/FPS acceptance.
"""
from pathlib import Path
import ctypes as C
import hashlib
import json
import os
import struct
import subprocess
import tempfile
import time
from host_tools import find_tcc

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'firmware/main'
ASSET = ROOT / 'firmware/assets/soundlab_assets.bin'
OUT = ROOT / 'device-validation/duel-0.2.0'
SHOTS = OUT / 'legacy-ui-shots'
TCC = find_tcc()
PROFILES = (('normal', 0), ('largest-13824', 13824), ('largest-7680', 7680))
ENTRIES = ('echo', 'shout', 'candle', 'rope', 'range', 'flight', 'clap', 'sampler',
           'duel', 'spec', 'water', 'scope', 'chroma', 'db', 'tuner', 'guitar', 'beat', 'pitch', 'settings')

SHIM = r'''
#include "sl_ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
double log2(double x){return log(x)/0.69314718055994530942;}
typedef struct { int font,missing; char text[160]; } legacy_text_t;
typedef struct { const uint8_t *key; char name[64]; int w,h,frames,calls,split_calls,failures; uint32_t seen[8]; } legacy_resource_t;
static legacy_text_t texts[256];static int text_n;
static legacy_resource_t resources[256];static int resource_n;
static unsigned alloc_limit,alloc_denied,alloc_calls,alloc_max;
static int decode_failures,bad_rows;
void legacy_limit(unsigned n){alloc_limit=n;}
void *legacy_malloc(size_t n){alloc_calls++;if(n>alloc_max)alloc_max=(unsigned)n;if(alloc_limit&&n>alloc_limit){alloc_denied++;return NULL;}return malloc(n);}
unsigned legacy_alloc_denied(void){return alloc_denied;}
unsigned legacy_alloc_calls(void){return alloc_calls;}
unsigned legacy_alloc_max(void){return alloc_max;}
int legacy_decode_failures(void){return decode_failures;}
int legacy_bad_rows(void){return bad_rows;}
void legacy_reset_frame(void){text_n=0;}
int legacy_text_count(void){return text_n;}
const legacy_text_t *legacy_texts(void){return texts;}
int legacy_resource_count(void){return resource_n;}
const legacy_resource_t *legacy_resources(void){return resources;}
void legacy_real_text_n(const fc_font_t *,int,int,const char *,int,uint16_t,int);
void fc_text_n(const fc_font_t *f,int x,int y,const char *s,int nb,uint16_t color,int align){
  if(text_n<256){legacy_text_t *p=&texts[text_n++];memset(p,0,sizeof *p);int n=nb<159?nb:159;
    memcpy(p->text,s,n);p->text[n]=0;p->font=f==&F_SANS16?16:f==&F_SANS14?14:f==&F_DISP18?18:f==&F_DISP24?24:f==&F_MONO14?114:f==&F_MONO22?22:f==&F_MONO48?48:f==&F_DISP56?56:0;
    const char *q=p->text;fc_glyph_t g;while(*q){uint32_t cp=fc_utf8_next(&q);if(cp!=' '&&!fc_font_glyph(f,cp,&g))p->missing++;}
  }legacy_real_text_n(f,x,y,s,nb,color,align);
}
bool legacy_real_img_open(const char *,fc_img_t *);
bool fc_img_open(const char *name,fc_img_t *im){
  bool ok=legacy_real_img_open(name,im);if(!ok)return false;
  int i;for(i=0;i<resource_n;i++)if(resources[i].key==im->data)break;
  if(i==resource_n&&resource_n<256){legacy_resource_t *r=&resources[resource_n++];memset(r,0,sizeof *r);r->key=im->data;
    strncpy(r->name,name,63);r->w=im->w;r->h=im->h;r->frames=im->frames;}
  return true;
}
const fc_img_surface_t *legacy_real_surface(const fc_img_t *,int);
const fc_img_surface_t *fc_img_surface(const fc_img_t *im,int frame){
  const fc_img_surface_t *s=legacy_real_surface(im,frame);int i;
  for(i=0;i<resource_n;i++)if(resources[i].key==im->data)break;
  if(!s){decode_failures++;if(i<resource_n)resources[i].failures++;return NULL;}
  if(i<resource_n){legacy_resource_t *r=&resources[i];r->calls++;if(s->part[1])r->split_calls++;
    int f=frame<0?0:frame>=im->frames?im->frames-1:frame;if(f<256)r->seen[f/32]|=1u<<(f%32);}
  if(s->w!=im->w)bad_rows++;
  for(int y=0;y<im->h;y++)if(!fc_img_row(s,y))bad_rows++;
  return s;
}
int legacy_decode(const char *name,int frame,uint8_t *out){
  fc_img_t im;if(!fc_img_open(name,&im))return -1;fc_img_epoch();
  const fc_img_surface_t *s=fc_img_surface(&im,frame);if(!s)return -2;
  for(int y=0;y<im.h;y++)memcpy(out+y*im.w,fc_img_row(s,y),im.w);return im.w*im.h;
}
static uint16_t image_fb[FC_W*FC_H];
static void image_flush(int y,int rows,const uint16_t *p,void *ctx){(void)ctx;memcpy(image_fb+y*FC_W,p,rows*FC_W*2);}
const uint16_t *legacy_static_render(const char *name){
  fc_img_t im;if(!fc_img_open(name,&im)||im.frames!=1)return NULL;
  fc_gfx_begin();fc_img(&im,0,0,0,255);fc_gfx_end(image_flush,NULL,true);return image_fb;
}
'''


class Text(C.Structure):
    _fields_ = [('font', C.c_int), ('missing', C.c_int), ('text', C.c_char * 160)]


class Resource(C.Structure):
    _fields_ = [('key', C.c_void_p), ('name', C.c_char * 64)] + [(k, C.c_int) for k in
               ('w', 'h', 'frames', 'calls', 'split_calls', 'failures')] + [('seen', C.c_uint32 * 8)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(directory):
    """Rename observation boundaries only; all real C draw/decode code is retained."""
    sources = sorted((SRC / 'ui').glob('*.c')) + [SRC / 'gfx/fc_gfx.c', SRC / 'gfx/fc_res.c',
              SRC / 'mic/nabo_mic.c', SRC / 'voice/nabo_voice.c', ROOT / 'tests/ui_host/sl_host.c']
    copies = {}
    for relative, replacements in {
        'gfx/fc_gfx.c': [('void fc_text_n(', 'void legacy_real_text_n(')],
        'gfx/fc_res.c': [('bool fc_img_open(', 'bool legacy_real_img_open('),
                         ('const fc_img_surface_t *fc_img_surface(', 'const fc_img_surface_t *legacy_real_surface(')]
    }.items():
        source = SRC / relative
        contents = source.read_text(encoding='utf-8')
        for old, new in replacements:
            assert contents.count(old) == 1, (relative, old, 'surface cache implementation required')
            contents = contents.replace(old, new)
        if relative.endswith('fc_res.c'):
            marker = '#include <string.h>'
            assert marker in contents
            contents = contents.replace(marker, marker + '\nvoid *legacy_malloc(size_t);\n#define malloc legacy_malloc')
        copy = directory / source.name
        copy.write_text(contents, encoding='utf-8')
        copies[source] = copy
    shim = directory / 'legacy_probe.c'
    shim.write_text(SHIM, encoding='utf-8')
    command = [str(TCC), '-shared', '-rdynamic']
    command += ['-I' + str(SRC / p) for p in ('gfx', 'ui', 'mic', 'voice')]
    command += [str(copies.get(p, p)) for p in sources] + [str(shim)]
    libraries = []
    for name, _ in PROFILES:
        target = directory / (name + '.dll')
        result = subprocess.run(command + ['-o', str(target)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        libraries.append(target)
    dependencies = sources + sorted(p for folder in ('ui', 'gfx', 'mic', 'voice')
                                    for p in (SRC / folder).glob('*.h'))
    dependencies += sorted((SRC / 'gfx').glob('*.inc'))
    return libraries, {p.relative_to(ROOT).as_posix(): sha(p) for p in dependencies}


def configure(path, limit):
    lib = C.CDLL(str(path))
    lib.host_scene.restype = C.c_char_p
    lib.host_fb.restype = C.POINTER(C.c_uint16)
    lib.legacy_texts.restype = C.POINTER(Text)
    lib.legacy_resources.restype = C.POINTER(Resource)
    lib.legacy_decode.argtypes = [C.c_char_p, C.c_int, C.c_void_p]
    lib.legacy_static_render.argtypes = [C.c_char_p]
    lib.legacy_static_render.restype = C.POINTER(C.c_uint16)
    lib.host_feed.argtypes = [C.c_void_p, C.c_int, C.c_void_p]
    assert lib.host_init(str(ASSET).encode(), 7) == 0
    lib.legacy_limit(limit)
    return lib


def signals():
    result = {}
    for name in ('voice', 'beats', 'chords', 'tones', 'noise', 'quiet', 'breathy'):
        x = np.load(ROOT / 'tests/mic_host/signals' / (name + '.npy')).astype(np.float64)
        result[name] = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)
    result['silence'] = np.zeros(16000 * 10, np.int16)
    result['blow'] = (np.random.default_rng(1).standard_normal(16000 * 10) * 9000).astype(np.int16)
    return result


def rgb565(fb):
    fb = fb.astype(np.uint32)
    return Image.fromarray(np.stack([((fb >> 11) & 31) * 255 // 31,
                                    ((fb >> 5) & 63) * 255 // 63, (fb & 31) * 255 // 31], -1)
                           .reshape(320, 240, 3).astype(np.uint8))


class Run:
    def __init__(self, libs):
        self.libs = libs
        self.sigs = signals()
        self.pos = {}
        self.label = 'boot'
        self.records = {}
        self.missing = []
        self.frames = 0
        self.images = []
        self.snapshot_records = []
        self.font_pairs = set()
        self.menu_texts = set()

    def call(self, name, *args):
        return [getattr(lib, name)(*args) for lib in self.libs]

    def go(self, scene, label=None):
        self.label = label or scene
        self.call('host_go', scene.encode())
        assert self.scenes() == [scene] * len(self.libs)

    def scenes(self):
        return [lib.host_scene().decode() for lib in self.libs]

    def feed(self, signal, ms):
        x = self.sigs[signal]
        p = self.pos.get(signal, 0)
        n = 16 * ms
        chunk = np.take(x, (np.arange(n) + p) % len(x)).astype(np.int16)
        self.pos[signal] = (p + n) % len(x)
        self.call('host_feed', chunk.ctypes.data_as(C.c_void_p), n, None)

    def run(self, signal, ms, every=1):
        for index in range(ms // 33):
            self.feed(signal, 33)
            self.call('host_step', 33)
            if index % every == 0:
                self.frame()

    def key(self, key, hold=60):
        self.call('host_key', key, 1)
        self.call('host_step', hold)
        self.call('host_key', key, 0)

    def up(self):
        self.key(0)
        self.call('host_step', 320)

    def frame(self):
        images = []
        record = self.records.setdefault(self.label, {'frames': 0, 'max_display_ops': 0,
            'incremental_full_pixel_mismatches': 0, 'fragmented_normal_pixel_mismatches': 0,
            'display_list_overflows': 0, 'missing_glyphs': 0})
        for index, lib in enumerate(self.libs):
            lib.legacy_reset_frame()
            mm, ops = C.c_int(), C.c_int()
            lib.host_frame(0, C.byref(mm), C.byref(ops))
            assert mm.value == 0, (self.label, self.frames, PROFILES[index], 'incremental/full', mm.value)
            assert not ops.value & (1 << 30), (self.label, self.frames, 'display-list overflow')
            record['max_display_ops'] = max(record['max_display_ops'], ops.value)
            texts = list(lib.legacy_texts()[:lib.legacy_text_count()])
            for item in texts:
                text = item.text.decode('utf-8')
                self.font_pairs.add((text, item.font))
                if self.scenes()[index] == 'menu':
                    self.menu_texts.add((text, item.font))
                if item.missing:
                    self.missing.append({'case': self.label, 'profile': PROFILES[index][0],
                                         'text': text, 'font': item.font, 'missing': item.missing})
                    record['missing_glyphs'] += item.missing
            images.append(np.ctypeslib.as_array(lib.host_fb(), shape=(240 * 320,)).copy())
            assert lib.legacy_decode_failures() == 0, (self.label, PROFILES[index], 'sprite decode failed')
            assert lib.legacy_bad_rows() == 0, (self.label, PROFILES[index], 'sprite row missing')
        for index in range(1, len(images)):
            bad = int(np.count_nonzero(images[0] != images[index]))
            if bad:
                for n, fb in enumerate(images):
                    rgb565(fb).save(SHOTS / f'FAIL-{self.label}-{self.frames}-{PROFILES[n][0]}.png')
            assert bad == 0, (self.label, self.frames, PROFILES[index], 'normal/fragmented', bad)
        self.images = images
        record['frames'] += 1
        self.frames += 1

    def shot(self, label=None):
        label = label or self.label
        self.frame()
        path = SHOTS / (label + '.png')
        rgb565(self.images[0]).save(path)
        self.snapshot_records.append({'case': label, 'file': str(path.relative_to(ROOT)),
            'rgb565_sha256': hashlib.sha256(self.images[0].tobytes()).hexdigest()})

    def status(self):
        out = []
        for lib in self.libs:
            text = C.create_string_buffer(256)
            lib.sl_newgames_status(text, len(text))
            out.append(text.value.decode())
        assert len(set(out)) == 1, out
        return out[0]


def exercise(r):
    r.go('boot'); r.run('silence', 600); r.shot('boot-0.6s')
    r.run('silence', 2200); r.shot('boot-2.8s')
    r.key(2); assert r.scenes() == ['menu'] * 3
    # Reach every old entry and the new duel through the real scrolling menu.
    for entry in ENTRIES:
        r.label = 'menu-' + entry; r.run('voice', 66); r.shot()
        r.key(2); assert r.scenes() == [entry] * 3, (entry, r.scenes())
        r.label = 'menu-entry-' + entry; r.run('silence', 66)
        r.go('menu'); r.key(1)

    r.go('settings')
    # The existing volume row is initially selected; exercise its actual key handler.
    volume = 80
    for target in (0, 40, 80, 100):
        while volume != target:
            r.key(2); volume = (volume + 10) % 110
        r.label = f'settings-volume-{target}'; r.run('silence', 99); r.shot()
        assert f'{target}%' in [t.text.decode() for t in r.libs[0].legacy_texts()[:r.libs[0].legacy_text_count()]]
    while volume != 80:
        r.key(2); volume = (volume + 10) % 110
    for row in range(1, 6):
        r.key(1); r.label = f'settings-row-{row}'; r.run('silence', 66); r.shot()

    r.go('spec'); r.run('chords', 1500); r.shot('spec-led')
    r.key(1); r.run('chords', 400); r.shot('spec-mirror')
    r.key(1); r.run('chords', 400); r.shot('spec-ring')
    r.go('water'); r.run('voice', 3000); r.shot('waterfall')
    r.go('scope'); r.run('voice', 800); r.shot('scope-wave')
    r.key(1); r.run('voice', 400); r.shot('scope-circle')
    r.key(1); r.run('voice', 400); r.shot('scope-lissa')
    r.go('chroma'); r.run('chords', 1500); r.shot()
    r.go('db'); r.run('noise', 2000); r.shot()
    r.key(1); r.key(1); r.run('blow', 600); r.shot('db-alarm')
    r.go('tuner'); r.run('voice', 1200); r.shot()
    r.key(1); r.run('tones', 1500); r.shot('tuner-guitar')
    r.go('beat'); r.run('beats', 7300); r.shot()
    r.go('pitch'); r.run('voice', 3500); r.shot()

    r.go('echo'); r.run('silence', 300); r.shot('echo-idle')
    # All 12 voices, including the 120+76+76 chorus that pins several frames.
    for voice in range(12):
        r.label = f'echo-voice-{voice}'
        r.call('host_key', 2, 1); r.run('voice', 1200); r.shot(f'echo-record-{voice}')
        r.call('host_key', 2, 0); r.run('silence', 300); r.shot(f'echo-play-{voice}')
        r.run('silence', 2200)
        if voice < 11: r.up(); r.run('silence', 1300, every=4)
    r.go('shout'); r.run('silence', 300); r.shot('shout-ready')
    r.key(2); r.run('voice', 1500); r.shot('shout-run')
    r.go('candle'); r.run('silence', 300); r.shot()
    r.run('blow', 2500); r.shot('candle-done')
    r.go('rope'); r.run('silence', 300); r.key(2); r.run('voice', 2000); r.shot('rope-walk')
    r.go('range'); r.run('tones', 1500); r.shot('range-low')
    r.key(2); r.run('tones', 2500); r.key(2); r.run('silence', 300); r.shot('range-card')
    r.go('guitar'); r.run('silence', 300); r.shot('guitar-menu')
    selected = 0
    for page, signal in ((1, 'silence'), (2, 'chords'), (3, 'beats'), (4, 'silence'), (5, 'silence'), (6, 'silence')):
        r.go('guitar', f'guitar-page-{page}')
        for _ in range((page - selected) % 7): r.key(1)
        selected = page; r.key(2); r.run(signal, 1200)
        if page == 3: r.key(2); r.run('beats', 2500)
        if page == 1: r.key(2); r.run('silence', 200)
        r.shot()

    r.go('flight'); r.run('tones', 900); r.shot('flight-ready')
    r.key(2); r.run('tones', 900); r.shot('flight-run')
    assert 'state=1' in r.status(), r.status()
    r.key(2); r.run('tones', 99); r.shot('flight-pause')
    assert 'state=2' in r.status(), r.status()
    r.key(2); r.run('voice', 4000); r.shot('flight-later')
    r.go('clap'); r.run('silence', 300); r.shot('clap-ready')
    r.key(2); r.run('beats', 1000); r.shot('clap-countdown')
    r.run('beats', 2600); r.shot('clap-run')
    r.run('beats', 10000, every=2); r.shot('clap-result')
    assert 'state=3' in r.status(), r.status()
    r.go('sampler'); r.run('voice', 300); r.shot('sampler-ready')
    r.call('host_key', 2, 1); r.run('voice', 450); r.shot('sampler-recording')
    r.run('voice', 650); r.call('host_key', 2, 0); r.run('silence', 1300); r.shot('sampler-recorded')
    assert 'samples8k=4000' in r.status(), r.status()
    for key in (0, 1, 2):
        r.key(key); r.run('silence', 99); r.shot(f'sampler-note-{key}'); r.run('silence', 700)

    # Real host create/search handlers and back lifecycle. RF is stubbed by the existing host hooks.
    r.go('duel'); r.run('silence', 300); r.shot('duel-choose')
    r.key(2); r.run('silence', 300); r.shot('duel-host-real-handler')
    r.go('echo'); r.run('silence', 900); r.shot('echo-after-duel-exit')
    r.go('duel'); r.key(1); r.key(2); r.run('silence', 300); r.shot('duel-search-real-handler')
    # Existing production fixtures only initialize model states, retaining the actual draw functions.
    for name, stage, outcome in (('lobby',5,1),('armed',6,1),('play',7,1),('round',8,1),
                                ('win',9,1),('tie',9,0),('loss',9,-1),('lost',10,1)):
        r.go('duel', 'duel-' + name); r.call('sl_duel_fixture', stage, outcome)
        r.run('silence', 396); r.shot()


def decode_all(libs):
    """Use the same static-RLE / animated-surface paths as production OP_IMG."""
    data = ASSET.read_bytes()
    count = struct.unpack_from('<H', data, 6)[0]
    records = []
    output = [np.empty(240 * 320, np.uint8) for _ in libs]
    for index in range(count):
        name, offset, size = struct.unpack_from('<32sII', data, 8 + index * 40)
        name = name.split(b'\0', 1)[0]
        resource = data[offset:offset + size]
        if resource[:4] == b'FNBO':
            w, frames = struct.unpack_from('<HH', resource, 6); h = w
        elif resource[:4] == b'FNBR':
            w, h, frames = struct.unpack_from('<HHH', resource, 6)
        else:
            continue
        assert w * h <= output[0].size
        for lib in libs: lib.fc_img_cache_clear()
        digest = hashlib.sha256()
        if frames == 1:
            # Static props intentionally bypass the bounded animation cache. Some
            # exceed its entire budget (cards), so a surface request is not valid.
            images = []
            assert w <= 240 and h <= 320, (name, w, h)
            for lib in libs:
                pointer = lib.legacy_static_render(name)
                assert pointer, name
                images.append(np.ctypeslib.as_array(pointer, shape=(240*320,)).copy())
            assert all(np.array_equal(images[0], im) for im in images[1:]), (name, 'static RLE differs')
            records.append({'name': name.decode(), 'width': w, 'height': h, 'frames': frames,
                            'path': 'production static OP_IMG direct RLE; no surface allocation',
                            'encoded_sha256': hashlib.sha256(resource).hexdigest(),
                            'rgb565_sha256': hashlib.sha256(images[0].tobytes()).hexdigest()})
            continue
        for frame in range(frames):
            for p, lib in enumerate(libs):
                n = lib.legacy_decode(name, frame, output[p].ctypes.data_as(C.c_void_p))
                assert n == w * h, (name, frame, PROFILES[p], n)
                assert output[p][:n].max() <= 63, (name, frame, 'palette index out of range')
            assert np.array_equal(output[0][:n], output[1][:n]), (name, frame, '13824 decode mismatch')
            assert np.array_equal(output[0][:n], output[2][:n]), (name, frame, '7680 decode mismatch')
            digest.update(output[0][:n].tobytes())
        # Explicit backwards/random requests also traverse production delta restart paths.
        for frame in dict.fromkeys((frames - 1, 0, frames // 2, 0)):
            for p, lib in enumerate(libs):
                assert lib.legacy_decode(name, frame, output[p].ctypes.data_as(C.c_void_p)) == w * h
            assert all(np.array_equal(output[0][:w*h], o[:w*h]) for o in output[1:])
        records.append({'name': name.decode(), 'width': w, 'height': h, 'frames': frames,
                        'path': 'production animated surface / delta decoder',
                        'encoded_sha256': hashlib.sha256(resource).hexdigest(), 'decoded_frames_sha256': digest.hexdigest()})
    for lib in libs: lib.fc_img_cache_clear()
    return records


def main():
    started = time.monotonic()
    SHOTS.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nabo-legacy-ui-', ignore_cleanup_errors=True) as temp:
        paths, bound = build(Path(temp))
        libs = [configure(path, profile[1]) for path, profile in zip(paths, PROFILES)]
        r = Run(libs); exercise(r)
        # Save render-observed resources before the complete asset sweep expands coverage.
        observed = []
        for lib in libs:
            observed.append([{'name': item.name.decode(), 'width': item.w, 'height': item.h,
                'frames': item.frames, 'surface_calls': item.calls, 'split_surface_calls': item.split_calls,
                'decode_failures': item.failures, 'distinct_rendered_frames': sum(int(v).bit_count() for v in item.seen)}
                for item in lib.legacy_resources()[:lib.legacy_resource_count()]])
        decoded = decode_all(libs)
        assert not r.missing, r.missing[:12]
        assert len([e for e in ENTRIES if e not in ('duel', 'settings')]) == 17
        stats = [{'profile': name, 'max_image_malloc_bytes': limit or None,
                  'image_malloc_calls': lib.legacy_alloc_calls(), 'rejected_image_mallocs': lib.legacy_alloc_denied(),
                  'largest_requested_image_malloc': lib.legacy_alloc_max(), 'decode_failures': lib.legacy_decode_failures(),
                  'bad_surface_rows': lib.legacy_bad_rows(), 'observed_render_resources': images}
                 for (name, limit), lib, images in zip(PROFILES, libs, observed)]
        assert stats[1]['rejected_image_mallocs'] > 0 and stats[2]['rejected_image_mallocs'] > 0
        assert any(i['split_surface_calls'] for i in stats[2]['observed_render_resources'])
        report = {'scope': 'production C host RGB565, real audio/microphone/voice and key paths; not physical display, RF, timing or heap acceptance',
                  'device_tested': False, 'rf_tested': False, 'asset_sha256': sha(ASSET), 'source_sha256': bound,
                  'test_sha256': sha(Path(__file__)), 'frames_per_profile': r.frames, 'profiles': stats,
                  'existing_entry_count': 17, 'menu_routes': list(ENTRIES), 'settings_volume_percent': [0,40,80,100],
                  'incremental_full_pixel_mismatches': 0, 'normal_fragmented_pixel_mismatches': 0,
                  'display_list_overflows': 0, 'max_display_ops': max(v['max_display_ops'] for v in r.records.values()),
                  'missing_glyphs': 0, 'observed_text_font_pairs': [{'text': t, 'font': f} for t,f in sorted(r.font_pairs)],
                  'menu_text_font_pairs': [{'text': t, 'font': f} for t,f in sorted(r.menu_texts)],
                  'all_native_image_resources_decoded': decoded, 'cases': r.records, 'snapshots': r.snapshot_records,
                  'elapsed_seconds': round(time.monotonic()-started, 2)}
        (OUT / 'legacy-ui-regression.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        # Private visual contact sheet, explicitly labeled as host rendering.
        shots = r.snapshot_records; cols = 8; cw = 250; ch = 346
        sheet = Image.new('RGB', (cols*cw, ((len(shots)+cols-1)//cols)*ch+32), '#0A0716')
        draw = ImageDraw.Draw(sheet); font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 13)
        draw.text((8,8), 'PRODUCTION C HOST RGB565 - NOT A DEVICE PHOTO', font=font, fill='#35f0dc')
        for i, item in enumerate(shots):
            x = i%cols*cw+5; y = i//cols*ch+32
            with Image.open(ROOT / item['file']) as im: sheet.paste(im, (x,y))
            draw.text((x,y+322), item['case'], font=font, fill='#ece8ff')
        sheet.save(SHOTS / 'overview.png')
        print(json.dumps({k: report[k] for k in ('frames_per_profile','existing_entry_count','incremental_full_pixel_mismatches',
            'normal_fragmented_pixel_mismatches','display_list_overflows','max_display_ops','missing_glyphs','elapsed_seconds')}, indent=2))
        print('PASS all native image resources:', len(decoded), 'frames:', sum(x['frames'] for x in decoded))
        print('Private report:', OUT / 'legacy-ui-regression.json')


if __name__ == '__main__':
    main()
