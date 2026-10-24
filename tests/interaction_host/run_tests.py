"""Drive real UI/audio handlers and renderer. No device, asset writes or golden cache."""
from pathlib import Path
import ctypes as C
import os
import shlex
import subprocess
import tempfile
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SRC = ROOT / 'firmware/main'
OUT = ROOT / 'out/interaction-host'


class Settings(C.Structure):
    _fields_ = [(name, C.c_int16) for name in ('a4', 'quiet', 'cal', 'sens')] + [
        ('calm', C.c_uint8), ('volume', C.c_uint8)]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nabo-interaction-') as directory:
        work = Path(directory)
        gfx = work / 'fc_gfx.c'
        original = (SRC / 'gfx/fc_gfx.c').read_text(encoding='utf-8')
        assert original.count('void fc_text_n(') == 1
        gfx.write_text(original.replace('void fc_text_n(', 'void ix_real_text_n('), encoding='utf-8')
        sources = [p for p in sorted((SRC / 'ui').glob('*.c'))
                   if p.name not in ('sc_core.c', 'sc_echo.c')]
        sources += [gfx, SRC / 'gfx/fc_res.c', SRC / 'mic/nabo_mic.c',
                    SRC / 'voice/nabo_voice.c', ROOT / 'tests/ui_host/sl_host.c', HERE / 'probe.c']
        sanitizer = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.environ.get('NABO_UBSAN') else []
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-O2', '-shared', '-fPIC', *sanitizer]
        command += ['-I' + str(SRC / p) for p in ('gfx', 'ui', 'mic', 'voice')]
        command += list(map(str, sources)) + ['-o', str(work / 'ui.so'), '-lm']
        subprocess.run(command, check=True)
        lib = C.CDLL(str(work / 'ui.so'))
        lib.host_fb.restype = C.POINTER(C.c_uint16)
        lib.host_scene.restype = C.c_char_p
        lib.ix_texts.restype = C.c_char_p
        for name in ('sl_rec_on', 'sl_voice_playing'):
            getattr(lib, name).restype = C.c_bool
        assert lib.host_init(str(ROOT / 'firmware/assets/soundlab_assets.bin').encode(), 7) == 0
        settings = Settings.in_dll(lib, 'SET')
        clock = C.c_uint32.in_dll(lib, 'VT')
        screenshots = []

        def step(ms):
            lib.host_step(ms)

        def key(k, down):
            lib.host_key(k, down)

        def tap(k, wait=0):
            key(k, 1); key(k, 0)
            if wait:
                step(wait)

        def go(name):
            lib.host_go(name.encode()); step(1600)

        def frame(name=None):
            lib.ix_frame_reset()
            mismatch, operations = C.c_int(), C.c_int()
            lib.host_frame(0, C.byref(mismatch), C.byref(operations))
            assert mismatch.value == 0, 'Incremental rendering differs from a full frame'
            assert not operations.value & (1 << 30), 'Display list overflow'
            assert lib.ix_missing() == 0, 'Missing glyph in the actual selected font'
            assert lib.ix_outside() == 0, 'Text ink outside the screen'
            pixels = np.ctypeslib.as_array(lib.host_fb(), shape=(240 * 320,)).copy().reshape(320, 240)
            if name:
                rgb = np.stack([((pixels >> 11) & 31) * 255 // 31,
                                ((pixels >> 5) & 63) * 255 // 63,
                                (pixels & 31) * 255 // 31], -1).astype('uint8')
                image = Image.fromarray(rgb)
                image.save(OUT / (name + '.png')); screenshots.append(image)
            return pixels, lib.ix_texts().decode()

        def feed(ms, sound=False):
            output = []
            for _ in range(ms // 20):
                t = (np.arange(320) + (clock.value * 16)) / 16000
                signal = (.25 * np.sin(2 * np.pi * 220 * t) if sound else np.zeros(320))
                pcm = (signal * 32767).astype('int16')
                played = np.zeros(320, 'int16')
                written = lib.host_feed(pcm.ctypes.data_as(C.c_void_p), len(pcm), played.ctypes.data_as(C.c_void_p))
                assert written == 320
                output.append(played); step(20)
            return np.concatenate(output) if output else np.empty(0, 'int16')

        go('settings')
        assert settings.volume == 80 and not lib.ix_editing()
        frame('01-settings')
        tap(2); assert lib.ix_editing() and settings.volume == 80
        _, text = frame('02-volume-edit')
        assert '减' in text and '加' in text and '完成' in text
        step(200); before, _ = frame()
        saves, applies = lib.ix_saves(), lib.ix_applies()
        lib.toast(b'old notice')
        tap(0)
        lit, text = frame('03-up-feedback')
        assert 'old notice' not in text, 'Old toast masked the next button feedback'
        assert np.any(lit[300:] != before[300:]), 'UP feedback was delayed'
        assert settings.volume == 80 and lib.ix_saves() == saves
        step(299); assert settings.volume == 80
        step(1); assert settings.volume == 70
        assert lib.ix_applied() == 70 and lib.ix_applies() == applies + 1
        frame('04-volume-70')
        tap(1); assert settings.volume == 80
        for _ in range(12):
            tap(1)
        assert settings.volume == 100
        saves = lib.ix_saves(); tap(1)
        assert settings.volume == 100 and lib.ix_saves() == saves
        for _ in range(12):
            tap(0, 300)
        assert settings.volume == 0
        saves = lib.ix_saves(); tap(0, 300)
        assert settings.volume == 0 and lib.ix_saves() == saves
        frame('05-volume-muted')
        tap(2); assert not lib.ix_editing()
        tap(2); assert lib.ix_editing()
        settings.volume = 80
        saves = lib.ix_saves()
        tap(0); step(100); tap(0)
        assert not lib.ix_editing() and settings.volume == 80 and lib.ix_saves() == saves
        step(400); assert settings.volume == 80
        tap(0); step(100); tap(0)
        assert lib.host_scene() == b'menu'
        step(400); assert settings.volume == 80
        print('PASS volume edit, one-step decrease, increase, bounds, persistence hooks and double-UP return')

        go('settings'); tap(2)
        clock.value = 0xFFFFFF80
        tap(0); frame(); step(299); assert settings.volume == 80
        step(1); assert settings.volume == 70
        tap(2); go('menu')
        step(200); before, _ = frame()
        key(2, 1); assert lib.host_scene() == b'echo'
        key(2, 1); assert not lib.sl_rec_on(), 'Duplicate DOWN repeated the action'
        key(2, 0)
        step(1600); before, _ = frame()
        key(1, 1); key(1, 0)
        lit, _ = frame()
        # Echo mode toast temporarily owns the footer; test the common feedback
        # pulse on settings, where no notification masks it.
        go('settings'); before, _ = frame(); tap(1); lit, _ = frame()
        assert np.any(lit[300:] != before[300:])
        step(160); after, _ = frame()
        assert not np.any(after[300:] != before[300:]), 'Feedback pulse did not expire'
        go('menu'); print('PASS immediate feedback, duplicate DOWN suppression, pulse expiry and uint32 time wrap')

        feed(200)
        go('echo')
        # The preceding mode switch selected auto; restore manual while quiet.
        feed(200)
        if lib.ix_echo_mode():
            tap(1); step(1600)
        _, text = frame('06-echo-ready')
        assert '录音' in text, (text, lib.ix_echo_state(), lib.ix_echo_mode())
        key(2, 1); feed(600, True)
        assert lib.sl_rec_on() and lib.ix_echo_state() == 1
        _, text = frame('07-echo-recording')
        assert '松开' in text and '310:声音' not in text
        voice, mode = lib.ix_echo_voice(), lib.ix_echo_mode()
        tap(0, 300); tap(1)
        assert lib.ix_echo_voice() == voice and lib.ix_echo_mode() == mode
        key(2, 0); assert lib.sl_voice_playing() and lib.ix_echo_state() == 2
        played = feed(200)
        assert np.any(played), 'The real voice mixer never produced audio'
        _, text = frame('08-echo-playing')
        assert '停止' in text and 'ADPCM' not in text and '310:录音' not in text
        tap(0, 300); tap(1)
        assert lib.ix_echo_voice() == voice and lib.ix_echo_mode() == mode
        key(2, 1)
        assert not lib.sl_voice_playing() and not lib.sl_rec_on() and lib.ix_echo_state() == 0
        key(2, 0); assert not lib.sl_rec_on()
        stopped = feed(20)
        assert not np.any(stopped[80:]), 'Audio continued after the existing 5 ms stop fade'
        frame('09-echo-stopped')
        key(2, 1); assert lib.sl_rec_on()
        feed(600, True); key(2, 0)
        assert lib.sl_voice_playing()
        feed(1600)
        assert not lib.sl_voice_playing() and lib.ix_echo_state() == 0
        print('PASS manual recording, state-specific hints, locked active voice/mode, stop without re-record and natural completion')

        tap(1); feed(500)
        assert lib.ix_echo_mode() and lib.ix_echo_state() == 0
        feed(400, True); assert lib.ix_echo_state() == 1
        _, text = frame('10-auto-recording')
        assert '310:松开' not in text and '310:录音' not in text
        for _ in range(80):
            feed(20)
            if lib.ix_echo_state() == 2:
                break
        assert lib.sl_voice_playing() and lib.ix_echo_state() == 2
        _, text = frame('11-auto-playing'); assert '停止' in text
        tap(2); assert lib.ix_echo_state() == 0 and not lib.sl_voice_playing() and not lib.sl_rec_on()
        feed(300); assert lib.ix_echo_state() == 0
        feed(400, True); assert lib.ix_echo_state() == 1
        tap(0); step(100); tap(0)
        assert lib.host_scene() == b'menu' and not lib.sl_rec_on() and not lib.sl_voice_playing()
        print('PASS auto recording/playback, stop cooldown and recording teardown on return')
        sheet = Image.new('RGB', (240 * 4, 320 * ((len(screenshots) + 3) // 4)), '#0a0716')
        for i, image in enumerate(screenshots):
            sheet.paste(image, ((i % 4) * 240, (i // 4) * 320))
        sheet.save(OUT / 'overview.png')
        print(f'PASS {len(screenshots)} rendered states: font coverage, text bounds, display-list capacity and incremental/full pixel equality')
        print('HOST ONLY: physical keys, Chinese panel rendering, codec volume and listening remain unverified')


if __name__ == '__main__':
    main()
