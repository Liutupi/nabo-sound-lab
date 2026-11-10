"""Host only: four-stage challenge rules, C/JS parity, and the real UI driven with synthetic sound.

Compiles production C with a native compiler (CC, default cc). No ESP-IDF build, serial port,
flashing or asset writes. Synthetic blowing, humming and clapping are not a microphone acceptance.
"""
from pathlib import Path
import ctypes as C
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'firmware/main'
UI = SRC / 'ui'
OUT = ROOT / 'out/circus-host'
CC = shlex.split(os.environ.get('CC', 'cc'))
NODE = os.environ.get('NABO_NODE') or shutil.which('node')
SANITIZE = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.environ.get('NABO_UBSAN') else []


def build_model(model, output):
    subprocess.run([*CC, '-std=c11', '-Wall', '-Wextra', '-Werror', *SANITIZE, '-I' + str(UI), str(model),
                    str(UI / 'newgames_model.c'), str(HERE / 'test_model.c'), '-o', str(output)], check=True)


def rules(work):
    exe = work / 'circus-model'
    build_model(UI / 'circus_model.c', exe)
    print(subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.strip())
    c = json.loads(subprocess.check_output([str(exe), '--trace'], text=True))
    snapshots = len(c)
    if NODE:
        js = json.loads(subprocess.check_output([NODE, str(HERE / 'trace.js')], text=True))
        assert c == js, next((f'snapshot {i}: C={a} JS={b}' for i, (a, b) in enumerate(zip(c, js)) if a != b), 'length mismatch')
        print(f'PASS C/JS exact parity: {snapshots} snapshots over two full rounds')
    else:
        print('SKIP C/JS parity: node not found')
    finals = [s for s in c if s['phase'] == 4]
    assert len(finals) == 2 and all(sum(s['score']) == s['total'] for s in finals)
    # Deliberate regressions must be caught by the unit tests.
    source = (UI / 'circus_model.c').read_text(encoding='utf-8')
    mutations = {'unfinished candles worth as much as finished': ('* 40 / CR_CANDLES', '* 100 / CR_CANDLES'),
                 'rope ignores pitch error': ('mean * 1000 / 80', 'mean * 0 / 80'),
                 'flight never auto-starts': ('s->hum_ms >= 300', 's->hum_ms >= 30000'),
                 'OK ends a running stage': ('if (s->phase == CR_PLAY) return 0;', 'if (s->phase == CR_PLAY) return stage_end(s);')}
    for label, (old, new) in mutations.items():
        assert source.count(old) == 1, old
        mutant = work / 'mutant.c'
        mutant.write_text(source.replace(old, new), encoding='utf-8')
        build_model(mutant, work / 'mutant')
        assert subprocess.run([str(work / 'mutant')], capture_output=True).returncode != 0, 'mutation escaped: ' + label
    print(f'PASS {len(mutations)} deliberate rule mutations rejected')
    return snapshots, len(mutations)


def status(lib):
    text = C.create_string_buffer(256)
    lib.sl_circus_status(text, len(text))
    fields = dict(part.split('=') for part in text.value.decode().split()[1:])
    return {'phase': int(fields['phase']), 'stage': int(fields['stage']), 't': int(fields['t']),
            'scores': [int(v) for v in fields['scores'].split('/')], 'total': int(fields['total']), 'best': int(fields['best'])}


def ui(work):
    OUT.mkdir(parents=True, exist_ok=True)
    gfx = work / 'fc_gfx.c'
    gfx.write_text((SRC / 'gfx/fc_gfx.c').read_text(encoding='utf-8').replace('void fc_text_n(', 'void ix_real_text_n('), encoding='utf-8')
    sources = [p for p in sorted(UI.glob('*.c')) if p.name not in ('sc_core.c', 'sc_echo.c')]
    sources += [gfx, SRC / 'gfx/fc_res.c', SRC / 'mic/nabo_mic.c', SRC / 'voice/nabo_voice.c',
                ROOT / 'tests/ui_host/sl_host.c', ROOT / 'tests/interaction_host/probe.c']
    command = [*CC, '-O2', '-shared', '-fPIC', *SANITIZE, *['-I' + str(SRC / p) for p in ('gfx', 'ui', 'mic', 'voice')]]
    subprocess.run(command + list(map(str, sources)) + ['-o', str(work / 'ui.so'), '-lm'], check=True)
    lib = C.CDLL(str(work / 'ui.so'))
    lib.host_fb.restype = C.POINTER(C.c_uint16)
    lib.host_scene.restype = C.c_char_p
    lib.ix_texts.restype = C.c_char_p
    assert lib.host_init(str(ROOT / 'firmware/assets/soundlab_assets.bin').encode(), 7) == 0
    best = C.c_uint16.in_dll(lib, 'CIRCUS_BEST')
    rng = np.random.default_rng(11)
    state = {'n': 0, 'frames': 0}
    shots = []

    def frame(name=None):
        lib.ix_frame_reset()
        mismatch, operations = C.c_int(), C.c_int()
        lib.host_frame(0, C.byref(mismatch), C.byref(operations))
        text = lib.ix_texts().decode()
        assert mismatch.value == 0, 'Incremental rendering differs from a full frame'
        assert not operations.value & (1 << 30), 'Display list overflow'
        assert lib.ix_missing() == 0, 'Missing glyph in the frozen font: ' + text
        assert lib.ix_outside() == 0, 'Text ink outside the screen: ' + text
        state['frames'] += 1
        if name:
            pixels = np.ctypeslib.as_array(lib.host_fb(), shape=(240 * 320,)).copy().reshape(320, 240)
            rgb = np.stack([((pixels >> 11) & 31) * 255 // 31, ((pixels >> 5) & 63) * 255 // 63, (pixels & 31) * 255 // 31], -1).astype('uint8')
            image = Image.fromarray(rgb); image.save(OUT / (name + '.png')); shots.append(image)
        return text

    def hop(kind='silence', burst=False):
        """One 20 ms microphone hop, then one UI step; every third hop is rendered and checked."""
        t = (np.arange(320) + state['n'] * 320) / 16000
        if kind == 'hum':
            signal = .25 * np.sin(2 * np.pi * 220 * t)
        elif kind == 'blow':        # loud, unpitched, low-frequency noise
            noise = np.cumsum(rng.standard_normal(320)); noise -= noise.mean()
            signal = .5 * noise / (np.abs(noise).max() + 1e-9)
        else:
            signal = np.zeros(320)
        if burst:
            signal = signal.copy(); signal[:96] += .8 * rng.uniform(-1, 1, 96)
        pcm = (np.clip(signal, -1, 1) * 32767).astype('int16')
        assert lib.host_feed(pcm.ctypes.data_as(C.c_void_p), 320, None) == 320
        lib.host_step(20); state['n'] += 1
        if state['n'] % 3 == 0:
            frame()

    def tap(key):
        lib.host_key(key, 1); lib.host_key(key, 0)

    def until(test, kind='silence', limit_ms=60000):
        for _ in range(limit_ms // 20):
            if test(status(lib)):
                return
            hop(kind)
        raise AssertionError(f'timeout feeding {kind}: {status(lib)}')

    # The menu opens on the challenge as the first toy.
    lib.host_go(b'menu'); lib.host_step(400)
    assert '声音挑战赛' in frame('00-menu')
    tap(2); assert lib.host_scene() == b'circus'
    for _ in range(10): hop()
    text = frame('01-title')
    assert status(lib)['phase'] == 0 and '四关连玩' in text and '最佳' not in text
    saves = lib.ix_saves()
    tap(2); hop(); assert status(lib)['phase'] == 1
    for _ in range(40): hop()
    assert '第 1 关' in frame('02-intro-candle')
    until(lambda s: s['phase'] == 2)                       # intro times out by itself

    # Stage 1: blowing puts every candle out.
    for _ in range(15): hop()
    frame('03-candle-wait')
    for _ in range(8): hop('blow')
    assert '还剩' in frame('04-candle-blowing')
    until(lambda s: s['phase'] == 3, 'blow', 12000)
    candle = status(lib)['scores'][0]
    assert candle >= 40, f'blowing never finished the candles: {status(lib)}'
    for _ in range(25): hop()
    assert '本关得分' in frame('05-candle-score')

    # Stage 2: a steady hum walks the whole rope. OK skips the cards.
    tap(2); hop(); assert status(lib)['phase'] == 1 and status(lib)['stage'] == 1
    tap(2); hop(); assert status(lib)['phase'] == 2
    for _ in range(20): hop()
    assert '唱一个长音' in frame('06-rope-wait')            # silence does not start the walk
    for _ in range(100): hop('hum')
    assert '走了' in frame('07-rope-walking')
    until(lambda s: s['phase'] == 3, 'hum', 14000)
    rope = status(lib)['scores'][1]
    assert rope >= 90, f'a steady 220 Hz tone should walk cleanly: {status(lib)}'

    # Stage 3: humming starts the flight without a key press.
    tap(2); hop(); tap(2); hop(); assert status(lib)['stage'] == 2 and status(lib)['phase'] == 2
    for _ in range(15): hop()
    assert '先哼一个音' in frame('08-flight-wait')
    for _ in range(150): hop('hum')
    assert '穿门' in frame('09-flight')
    until(lambda s: s['phase'] == 3, 'hum', 40000)
    flight = status(lib)['scores'][2]

    # Stage 4: four count-in clicks from the speaker, then sixteen claps on the beat.
    tap(2); hop(); tap(2); hop(); assert status(lib)['stage'] == 3 and status(lib)['phase'] == 2
    period = 60000 // 96
    for k in range(1, 3000):
        if status(lib)['phase'] != 2:
            break
        ms = k * 20 - 45                                    # the game subtracts 45 ms of latency
        hop(burst=ms >= 4 * period and (ms - 4 * period) % period < 20)
        if k == 60: assert '先听四拍' in frame('10-clap-count-in')
        if k == 300: assert '拍' in frame('11-clap')
    clap = status(lib)['scores'][3]
    assert clap >= 60, f'claps on the beat scored too low: {status(lib)}'

    # Final card, record saving and play again.
    tap(2); hop()
    s = status(lib)
    assert s['phase'] == 4 and s['total'] == candle + rope + flight + clap == sum(s['scores'])
    assert best.value == s['total'] and lib.ix_saves() == saves + 1, 'best total was not saved exactly once'
    for _ in range(30): hop()
    text = frame('12-final')
    assert '新纪录' in text and '合计' in text
    tap(2); hop()
    assert status(lib)['phase'] == 1 and status(lib)['total'] == 0 and best.value == s['total']
    # A skipped round scores lower, keeps the record and does not save again.
    for stage in range(4):
        until(lambda s, stage=stage: s['phase'] == 2 and s['stage'] == stage)
        tap(1); hop(); assert status(lib)['phase'] == 3
        tap(2); hop()
    low = status(lib)
    assert low['phase'] == 4 and low['total'] == 0 and best.value == s['total'] and lib.ix_saves() == saves + 1
    for _ in range(30): hop()
    text = frame('13-final-low')
    assert '再练一次' in text and '最佳' in text and '新纪录' not in text
    # Double UP returns to the menu and releases the onset hook and audio.
    tap(0); lib.host_step(100); tap(0)
    assert lib.host_scene() == b'menu'
    assert C.c_void_p.in_dll(lib, 'g_onset_hook').value is None
    lib.host_go(b'circus'); hop()
    assert status(lib)['phase'] == 0 and '最佳' in frame('14-title-with-best')

    sheet = Image.new('RGB', (240 * 5, 320 * 3), (10, 7, 22))
    for i, image in enumerate(shots):
        sheet.paste(image, (i % 5 * 240, i // 5 * 320))
    sheet.save(OUT / 'overview.png')
    print(f'PASS UI round: candle={candle} rope={rope} flight={flight} clap={clap} total={s["total"]}; '
          f'{state["frames"]} frames checked for glyphs, text bounds, display-list capacity and incremental/full equality')
    return {'candle': candle, 'rope': rope, 'flight': flight, 'clap': clap, 'total': s['total'], 'frames': state['frames']}


def main():
    with tempfile.TemporaryDirectory(prefix='nabo-circus-') as directory:
        work = Path(directory)
        snapshots, mutations = rules(work)
        round_result = ui(work)
    report = {'scope': 'host only: circus rules, C/JS parity and UI with synthetic sound', 'c_js_snapshots': snapshots,
              'exact_parity': bool(NODE), 'mutations_rejected': mutations, 'ui_round': round_result,
              'assets_changed': False, 'device_tested': False, 'esp32_built': False}
    (HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('HOST ONLY: real microphone, speaker, physical keys and device memory remain unverified')


if __name__ == '__main__':
    main()
