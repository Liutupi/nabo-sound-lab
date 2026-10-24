"""Deterministic voice-quality invariants, streaming and actual C/JS parity.

No recorded speech, fixture download or historical golden directory is needed.
These checks measure DSP behavior; they cannot establish speaker/listening quality.
"""
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import subprocess
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/voice-quality'
SR = 16000


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ['-fsanitize=undefined', '-fno-sanitize-recover=all'] if os.environ.get('NV_UBSAN') else []
    path = OUT / 'quality.so'
    subprocess.run([os.environ.get('CC', 'cc'), '-O2', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    *flags, '-Ifirmware/main/voice', 'firmware/main/voice/nabo_voice.c',
                    'tests/voice_host/nv_shim.c', '-o', str(path), '-lm'], cwd=ROOT, check=True)
    return C.CDLL(str(path))


def pcm(x):
    return np.clip(np.rint(x), -32768, 32767).astype(np.int16)


def speech(amplitude=700):
    """Synthetic voiced syllables, with pauses, harmonics and consonant energy."""
    t = np.arange(SR * 6 // 5) / SR
    phase = 2 * np.pi * (155*t + 13*t*t)
    envelope = np.maximum(0, np.sin(2*np.pi*2.8*t)) ** 0.7
    envelope[:SR//8] = 0
    carrier = np.sin(phase) + .4*np.sin(phase*2) + .22*np.sin(phase*5) + .1*np.sin(2*np.pi*2500*t)
    return pcm(amplitude * envelope * carrier)


def record(lib, x, chunk=None):
    x = np.ascontiguousarray(x, dtype=np.int16)
    args = [x.ctypes.data_as(C.c_void_p), len(x)]
    n = lib.nv_record_chunked(*args, chunk) if chunk else lib.nv_record(*args)
    decoded = np.zeros(n, np.int16)
    gain = lib.nv_decode_all(decoded.ctypes.data_as(C.c_void_p))
    return decoded, gain


def render(lib, voice, chunk=320):
    out = np.zeros(SR * 20, np.int16)
    n = lib.nv_render_chunked(voice, out.ctypes.data_as(C.c_void_p), len(out), chunk)
    assert n < len(out), 'Voice never reached its end'
    return out[:n]


def rms(x):
    if not len(x): return 0.0
    return float(np.sqrt(np.mean(np.asarray(x, dtype=float)**2)))


def main():
    lib = build()
    node = os.environ.get('NODE') or shutil.which('node')
    assert node, 'Node.js is needed for the actual prototype reference'
    quiet, normal, loud = speech(220), speech(1200), speech(17000)
    click = quiet.copy(); click[SR//2:SR//2+3] = [32767, -32768, 32767]
    t = np.arange(SR) / SR
    cases = {'empty': np.zeros(0, np.int16), 'quiet': quiet, 'normal': normal, 'loud': loud, 'handling-click': click,
             'dc-offset': pcm(normal.astype(float)+8000), 'silence': np.zeros(SR, np.int16),
             'faint-noise': pcm(np.random.default_rng(913).normal(0, 8, SR)),
             'short': quiet[:259], 'odd-length': quiet[:10001],
             'full-scale': np.tile(np.array([32767, -32768, 32767, 32767], np.int16), SR//4),
             'over-capacity': np.tile(normal, 4)}
    parity = []
    outputs = {}
    for name, x in cases.items():
        decoded, gain = record(lib, x)
        for chunk in (1, 319):
            got, g = record(lib, x, chunk)
            assert np.array_equal(decoded, got) and g == gain, (name, 'Recording chunk boundary changes audio')
        fixture, reference = OUT/(name+'.json'), OUT/(name+'-js.json')
        fixture.write_text(json.dumps({'x16': x.tolist()}))
        subprocess.run([node, str(ROOT/'tests/voice_host/render_reference.js'),
                        str(ROOT/'prototype/index.html'), str(fixture), str(reference)], check=True, stdout=subprocess.DEVNULL)
        js = json.loads(reference.read_text())
        assert np.array_equal(decoded, np.array(js['d'], np.int16)) and gain == js['g'], (name, 'ADPCM or gain parity')
        voices = []
        for voice, expected in enumerate(js['voices']):
            got = render(lib, voice)
            assert np.array_equal(got, render(lib, voice, 17)), (name, voice, 'Playback chunk boundary changes audio')
            ref = np.array(expected['y'], dtype=np.int16)
            assert abs(len(ref)-len(got)) <= 2, (name, voice, 'Duration differs from prototype')
            n = min(len(got), len(ref)); error = rms(got[:n].astype(float)-ref[:n])
            assert error <= max(4, rms(ref[:n])*.011), (name, voice, error, rms(ref[:n]))
            assert np.max(np.abs(got.astype(int)), initial=0) < 30720, (name, voice, 'Limiter headroom lost')
            if name == 'silence': assert not np.any(got), ('Silence makes noise', voice)
            assert not len(got) or (got[0] == 0 and got[-1] == 0), (name, voice, 'Edge ramp')
            voices.append({'voice': expected['name'], 'rms_error': round(error, 3), 'samples': len(got)})
            if voice == 11: outputs[name] = got
        parity.append({'case': name, 'gain_q8': gain, 'decoded_samples': len(decoded), 'voices': voices})
        print('PASS', name, ': 12 voices, C/JS parity, recording/playback chunk invariance, limiter, edges')

    active = slice(SR//5, SR)
    quiet_rms = rms(outputs['quiet'][active]); normal_rms = rms(outputs['normal'][active])
    loud_rms = rms(outputs['loud'][active]); click_rms = rms(outputs['handling-click'][active])
    assert quiet_rms > 2200 and max(normal_rms,loud_rms)/quiet_rms < 2.2, 'Quiet speech stays too quiet'
    assert click_rms > quiet_rms*.8, 'A handling click attenuates the whole phrase'
    dc = outputs['dc-offset'][SR//5:SR]
    assert abs(np.mean(dc)) < 80, 'DC offset consumes speaker headroom'
    decoded_rms = {}
    for hz in (1000, 3000, 6000):
        d, _ = record(lib, pcm(5000*np.sin(2*np.pi*hz*t)))
        decoded_rms[hz] = rms(d[800:])
    alias_db = 20*np.log10(decoded_rms[6000]/decoded_rms[1000])
    assert alias_db < -25, ('High-frequency noise aliases into speech', alias_db)
    assert decoded_rms[3000]/decoded_rms[1000] > .65, 'Consonant band is excessively muffled'
    record(lib, normal)
    radio = render(lib, 8); tail_rms = rms(radio[len(normal):])
    assert tail_rms < 650, 'Radio hiss dominates speech'
    original = render(lib, 11)
    for invalid in (-1, 12):
        assert np.array_equal(original, render(lib, invalid)), 'Invalid voice does not fall back safely'
    record(lib, pcm(1500*np.sin(2*np.pi*200*t)))
    def spectrum(y):
        y = y[SR//8:-SR//8].astype(float)
        power = np.abs(np.fft.rfft(y*np.hanning(len(y))))
        return np.fft.rfftfreq(len(y), 1/SR), power
    pitches = {}
    for voice, hz in ((0,300), (1,144), (7,270), (11,200)):
        freq, power = spectrum(render(lib, voice))
        if voice == 7:  # Fast vibrato deliberately moves power into FM sidebands.
            band = (freq > 100) & (freq < 500)
            peak = np.sum(freq[band]*power[band]**2)/np.sum(power[band]**2)
        else:
            peak = freq[np.argmax(power)]
        pitches[voice] = float(peak)
        assert abs(peak-hz) < 8, ('Voice loses its pitch character', voice, peak, hz)
    freq, power = spectrum(render(lib, 2))
    for hz in (130,200,270):
        assert np.max(power[np.abs(freq-hz)<3]) > power.max()*.45, ('Robot loses voice or metallic sidebands', hz)
    sizes = C.c_int(); player_size = lib.nv_sizes(C.byref(sizes))
    assert sizes.value < 12800 and player_size < 18000, 'Recording/player RAM budget grew unexpectedly'
    report = {'scope': 'Synthetic DSP checks; not listening or hardware acceptance', 'checks': parity,
              'quiet_original_rms': quiet_rms, 'normal_original_rms': normal_rms, 'loud_original_rms': loud_rms,
              'handling_click_rms': click_rms, 'dc_mean': float(np.mean(dc)), 'alias_6k_db': alias_db,
              'decoded_3k_to_1k_ratio': decoded_rms[3000]/decoded_rms[1000], 'radio_tail_rms': tail_rms,
              'effect_pitch_peaks_hz': pitches, 'record_bytes': sizes.value, 'player_bytes': player_size}
    (OUT/'quality-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS quality invariants:', json.dumps({k:v for k,v in report.items() if k not in ('checks','scope')}))


if __name__ == '__main__':
    main()
