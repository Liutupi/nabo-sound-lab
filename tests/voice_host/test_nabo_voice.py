"""nabo_voice（C 流式定点）对拍原型 renderVoice（JS 浮点整段）。用法：python test_nabo_voice.py"""
import ctypes as C, json, os, pathlib, subprocess, sys, shutil, numpy as np
HERE = pathlib.Path(__file__).parent
SRC = HERE.parents[1] / 'firmware' / 'main' / 'voice'
LIB = HERE / 'build' / 'libnv.so'

def build():
    LIB.parent.mkdir(exist_ok=True)
    extra = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.environ.get('NV_UBSAN') else []
    subprocess.check_call(['gcc', '-O2', *extra, '-shared', '-fPIC', '-Wall', '-I', str(SRC), str(SRC / 'nabo_voice.c'), str(HERE / 'nv_shim.c'), '-o', str(LIB), '-lm'])
    return C.CDLL(str(LIB))

def snr(ref, got):
    n = min(len(ref), len(got)); r = ref[:n].astype(float); e = r - got[:n]
    return 10 * np.log10((np.sum(r * r) + 1) / (np.sum(e * e) + 1))

def main():
    lib = build(); fails = []
    rs = C.c_int(); ps = lib.nv_sizes(C.byref(rs))
    print(f'nv_rec_t = {rs.value} 字节，nv_player_t = {ps} 字节')
    for f in sorted((HERE / 'golden').glob('*.json')):
        # Keep historical raw/ADPCM fixtures; derive current playback reference
        # from the actual prototype so local edge ramps are covered by parity.
        current = HERE / 'build' / ('current-' + f.name)
        node = os.environ.get('NODE') or shutil.which('node')
        if not node: raise RuntimeError('Node.js is required for current prototype voice parity')
        subprocess.run([node, str(HERE / 'render_reference.js'), str(HERE.parents[1] / 'prototype/index.html'), str(f), str(current)], check=True)
        g = json.loads(current.read_text(encoding='utf-8'))
        x16 = np.array(g['x16'], dtype=np.int16)
        n8 = lib.nv_record(x16.ctypes.data_as(C.c_void_p), len(x16))
        d = np.zeros(n8, np.int16); gain = lib.nv_decode_all(d.ctypes.data_as(C.c_void_p))
        jd = np.array(g['d'], dtype=np.int16)
        ok = n8 == len(jd) and np.array_equal(d, jd) and gain == g['g']
        print(f'[{f.stem}] ADPCM 解码逐样一致：{ok}（{n8} 个 8 kHz 采样，增益 {gain}/{g["g"]}）'); ok or fails.append(f'{f.stem} adpcm')
        for k, v in enumerate(g['voices']):
            ref = np.array(v['y'], dtype=np.int16)
            out = np.zeros(len(ref) + 4000, np.int16)
            n = lib.nv_render(k, out.ctypes.data_as(C.c_void_p), len(out))
            s = snr(ref, out[:n]); dl = n - len(ref)
            good = abs(dl) <= 2 and s >= 40
            print(f'  {"ok  " if good else "FAIL"} {v["name"]:<6} 长度 C {n} / 原型 {len(ref)}（差 {dl}），信噪比 {s:.1f} dB（要求 ≥40）')
            good or fails.append(f'{f.stem} {v["name"]}')
    print('\n全部通过' if not fails else f'\n{len(fails)} 项不通过：{fails}')
    sys.exit(1 if fails else 0)

if __name__ == '__main__': main()
