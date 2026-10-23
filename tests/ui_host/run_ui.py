"""把每个场景在宿主机上跑一段（送入测试声音、按键），存 PNG 并拼成总览；检查增量重画 == 整屏重画、显示列表不溢出。
用法：python run_ui.py [输出目录]"""
import ctypes as C, os, pathlib, subprocess, sys, numpy as np
from PIL import Image
HERE = pathlib.Path(__file__).parent
ROOT = HERE.parents[1]
SIG = ROOT / 'tests' / 'mic_host' / 'signals'
OUT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / 'shots'
OUT.mkdir(exist_ok=True)

def load(name):
    x = np.load(SIG / f'{name}.npy').astype(np.float64)
    return np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)

SIGS = {k: load(k) for k in ('voice', 'beats', 'chords', 'tones', 'noise', 'quiet', 'breathy')}
SIGS['silence'] = np.zeros(16000 * 10, np.int16)
SIGS['blow'] = (np.random.default_rng(1).standard_normal(16000 * 10) * 9000).astype(np.int16)   # 吹气：响亮的宽带噪声

lib = C.CDLL(str(HERE / 'libslhost.so'))
lib.host_scene.restype = C.c_char_p
lib.host_fb.restype = C.POINTER(C.c_uint16)
assert lib.host_init(str(ROOT / 'firmware' / 'assets' / 'soundlab_assets.bin').encode(), 7) == 0

class Run:
    def __init__(s): s.pos = {}; s.problems = []
    def feed(s, sig, ms):
        x = SIGS[sig]; p = s.pos.get(sig, 0); n = 16 * ms
        chunk = np.resize(np.roll(x, -p), n).astype(np.int16)
        s.pos[sig] = (p + n) % len(x)
        lib.host_feed(chunk.ctypes.data_as(C.c_void_p), n, None)
    def run(s, sig, ms, frames=True):
        for _ in range(ms // 33):
            s.feed(sig, 33); lib.host_step(33)
            if frames: s.frame()
    def key(s, k, hold=60):
        lib.host_key(k, 1); lib.host_step(hold); lib.host_key(k, 0)
    def up1(s): s.key(0); lib.host_step(320)           # ▲ 单击要等 300 ms 才确认
    def frame(s):
        mm, ops = C.c_int(), C.c_int()
        lib.host_frame(0, C.byref(mm), C.byref(ops))
        if mm.value: s.problems.append(f'{lib.host_scene().decode()}: 增量与整屏差 {mm.value} 像素')
        if ops.value & (1 << 30): s.problems.append(f'{lib.host_scene().decode()}: 显示列表溢出')
        return ops.value & 0xFFFF
    def shot(s, name):
        s.frame(); fb = np.ctypeslib.as_array(lib.host_fb(), shape=(320 * 240,)).copy()
        r = ((fb >> 11) & 31) * 255 // 31; g = ((fb >> 5) & 63) * 255 // 63; b = (fb & 31) * 255 // 31
        Image.fromarray(np.stack([r, g, b], -1).reshape(320, 240, 3).astype(np.uint8)).save(OUT / f'{name}.png'); return name

R = Run(); names = []
def go(i): lib.host_go(i.encode())
go('boot'); R.run('silence', 600); names.append(R.shot('boot-0.6s')); R.run('silence', 2200); names.append(R.shot('boot-2.8s'))
R.key(2); R.run('voice', 600); names.append(R.shot('menu'))
for k in range(9): R.key(1)
R.run('voice', 300); names.append(R.shot('menu-scroll'))
go('settings'); R.run('silence', 300); names.append(R.shot('settings'))
go('spec'); R.run('chords', 1500); names.append(R.shot('spec-led')); R.key(1); R.run('chords', 400); names.append(R.shot('spec-mirror')); R.key(1); R.run('chords', 400); names.append(R.shot('spec-ring'))
go('water'); R.run('voice', 3000); names.append(R.shot('waterfall'))
go('scope'); R.run('voice', 800); names.append(R.shot('scope-wave')); R.key(1); R.run('voice', 400); names.append(R.shot('scope-circle')); R.key(1); R.run('voice', 400); names.append(R.shot('scope-lissa'))
go('chroma'); R.run('chords', 1500); names.append(R.shot('chroma'))
go('db'); R.run('noise', 2000); names.append(R.shot('db')); R.key(1); R.key(1); R.run('blow', 600); names.append(R.shot('db-alarm'))
go('tuner'); R.run('voice', 1200); names.append(R.shot('tuner')); R.key(1); R.run('tones', 1500); names.append(R.shot('tuner-guitar'))
go('beat'); R.run('beats', 7000, frames=False); R.run('beats', 300); names.append(R.shot('beat'))
go('pitch'); R.run('voice', 3500); names.append(R.shot('pitch'))
go('echo'); R.run('silence', 300); names.append(R.shot('echo-idle'))
lib.host_key(2, 1); R.run('voice', 1200); names.append(R.shot('echo-rec')); lib.host_key(2, 0); R.run('silence', 600); names.append(R.shot('echo-play'))
go('shout'); R.run('silence', 300); names.append(R.shot('shout-ready')); R.key(2); R.run('voice', 1500); names.append(R.shot('shout-run'))
go('candle'); R.run('silence', 300); names.append(R.shot('candle')); R.run('blow', 2500); names.append(R.shot('candle-done'))
go('rope'); R.run('silence', 300); R.key(2); R.run('voice', 2000); names.append(R.shot('rope-walk'))
go('range'); R.run('tones', 1500); names.append(R.shot('range-low')); R.key(2); R.run('tones', 2500); R.key(2); R.run('silence', 300); names.append(R.shot('range-card'))
go('guitar'); R.run('silence', 300); names.append(R.shot('guitar-menu'))
sel = 0                                                                      # 工具箱菜单的选中项会保留，脚本自己记
for page, sig in ((1, 'silence'), (2, 'chords'), (3, 'beats'), (4, 'silence'), (5, 'silence'), (6, 'silence')):
    go('guitar')                                                             # 每次进来都在工具箱菜单
    for _ in range((page - sel) % 7): R.key(1)
    sel = page
    R.key(2); R.run(sig, 1200)
    if page == 3: R.key(2); R.run('beats', 2500)
    if page == 1: R.key(2); R.run('silence', 200)
    names.append(R.shot(f'guitar-{page}'))
# 总览
ims = [Image.open(OUT / f'{n}.png') for n in names]; cols = 8; rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * 240, rows * 320), (40, 40, 40))
for i, im in enumerate(ims): sheet.paste(im, ((i % cols) * 240, (i // cols) * 320))
sheet.save(OUT / 'overview.png')
print(len(names), '张截图 →', OUT / 'overview.png')
print('问题：' + ('无' if not R.problems else '\n  ' + '\n  '.join(sorted(set(R.problems)))))
