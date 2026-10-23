"""nabo_mic（C 定点）对拍原型（JS 浮点）。用法：python test_nabo_mic.py
先跑 gen_signals.py 和 record_golden.js 生成 signals/ 与 golden/。"""
import ctypes as C, json, pathlib, subprocess, sys, numpy as np
HERE = pathlib.Path(__file__).parent
SRC = HERE.parents[1] / 'firmware' / 'main' / 'mic'
LIB = HERE / 'build' / 'libnm.so'
NN = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B']

def build():
    LIB.parent.mkdir(exist_ok=True)
    import os
    extra = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.environ.get('NM_UBSAN') else []
    subprocess.check_call(['gcc', '-O2', *extra, '-shared', '-fPIC', '-Wall', '-I', str(SRC), str(SRC / 'nabo_mic.c'), str(HERE / 'nm_shim.c'), '-o', str(LIB), '-lm'])
    return C.CDLL(str(LIB))

def run_c(lib, x):
    hops = len(x) // 320
    xi = np.clip(np.round(x.astype(np.float64) * 32767), -32768, 32767).astype(np.int16)
    a = lambda dt, n: np.zeros(n, dt)
    db, dbs, onset, bpm, f0, f0c, chord = a(np.int16, hops), a(np.int16, hops), a(np.uint8, hops), a(np.uint16, hops), a(np.uint16, hops), a(np.uint16, hops), a(np.int8, hops)
    bands, wf, chroma = a(np.uint8, hops * 32), a(np.uint8, hops * 120), a(np.uint8, hops * 12)
    p = lambda v: v.ctypes.data_as(C.c_void_p)
    size = lib.nm_run(p(xi), hops, 0, p(db), p(dbs), p(bands), p(wf), p(onset), p(bpm), p(f0), p(f0c), p(chroma), p(chord))
    return dict(db=db / 100, dbS=dbs / 100, bands=bands.reshape(hops, 32) / 255, wf=wf.reshape(hops, 120).astype(int), onset=onset,
                bpm=bpm / 10, f0=f0 / 10, f0Conf=f0c / 1000, chroma=chroma.reshape(hops, 12) / 255, chord=chord, size=size)

def chord_name(c): return '' if c < 0 else NN[c // 2] + ('m' if c % 2 else '')

def compare(name, g, c):
    fails = []
    def check(cond, msg): print(('  ok   ' if cond else '  FAIL ') + msg); cond or fails.append(f'{name}: {msg}')
    J = lambda k: np.array([r[k] for r in g], dtype=float)
    jdb, jdbs = J('db'), J('dbS')
    m = jdb > -90
    check(np.max(np.abs(jdb[m] - c['db'][m]), initial=0) <= 0.1, f"音量 dB 最大差 {np.max(np.abs(jdb[m]-c['db'][m]), initial=0):.3f}（≤0.1）")
    m2 = jdbs > -90
    check(np.max(np.abs(jdbs[m2] - c['dbS'][m2]), initial=0) <= 0.2, f"平滑 dB 最大差 {np.max(np.abs(jdbs[m2]-c['dbS'][m2]), initial=0):.3f}（≤0.2）")
    jb = np.array([r['bands'] for r in g]); d = np.abs(jb - c['bands'])
    check(np.mean(d <= 3 / 255) >= 0.99, f"频段 |差|≤3/255 的比例 {np.mean(d <= 3/255)*100:.2f}%（≥99%），最大 {d.max()*255:.1f}/255")
    jw = np.array([r['wf'] for r in g]); dw = np.abs(jw - c['wf'])
    check(np.mean(dw <= 1) >= 0.99, f"瀑布 |差|≤1 级的比例 {np.mean(dw <= 1)*100:.2f}%（≥99%）")
    jo = set(np.nonzero(J('onset'))[0]); co = set(np.nonzero(c['onset'])[0])
    hit = sum(1 for h in jo if any(abs(h - k) <= 1 for k in co)); extra = sum(1 for k in co if not any(abs(h - k) <= 1 for h in jo))
    check(hit >= 0.95 * len(jo) and extra <= max(1, 0.05 * len(jo)), f"起音：原型 {len(jo)} 个，C 命中 {hit}，多出 {extra}")
    jf, jc = J('f0'), J('f0Conf'); both = (jf > 0) & (c['f0'] > 0)
    cents = 1200 * np.abs(np.log2(c['f0'][both] / jf[both])) if both.any() else np.zeros(1)
    check(np.percentile(cents, 99) <= 3, f"音高（两边都有）99% 分位差 {np.percentile(cents, 99):.2f} 音分（≤3）")
    strong = (jf > 0) & (jc > 0.8)
    check(not strong.any() or np.mean(c['f0'][strong] > 0) >= 0.98, f"原型有把握的音高，C 也给出的比例 {np.mean(c['f0'][strong] > 0)*100 if strong.any() else 100:.1f}%（≥98%）")
    same = np.mean((jf > 0) == (c['f0'] > 0))
    check(same >= 0.98, f"有没有音高逐帧一致 {same*100:.1f}%（≥98%）")
    jbpm = J('bpm')
    check(abs(jbpm[-1] - c['bpm'][-1]) <= 1.0, f"最终 BPM 原型 {jbpm[-1]:.1f} / C {c['bpm'][-1]:.1f}（差 ≤1）")
    jch = np.array([r['chroma'] for r in g]); dc = np.abs(jch - c['chroma'])
    check(np.mean(dc <= 0.03) >= 0.98, f"音级 |差|≤0.03 的比例 {np.mean(dc <= 0.03)*100:.2f}%（≥98%），最大 {dc.max():.3f}")
    jn = [r['chord'] for r in g]; cn = [chord_name(int(v)) for v in c['chord']]
    agree = np.mean([a == b for a, b in zip(jn, cn)])
    check(agree >= 0.97, f"和弦一致比例 {agree*100:.1f}%（≥97%）")
    return fails

def main():
    lib = build(); allf = []
    for f in sorted((HERE / 'signals').glob('*.npy')):
        g = json.loads((HERE / 'golden' / (f.stem + '.json')).read_text())
        c = run_c(lib, np.load(f))
        print(f'[{f.stem}] {len(g)} 帧，nm_state_t = {c["size"]} 字节')
        allf += compare(f.stem, g, c)
    print('\n全部通过' if not allf else f'\n{len(allf)} 项不通过')
    sys.exit(1 if allf else 0)

if __name__ == '__main__': main()
