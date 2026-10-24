"""Self-contained accuracy and C/JS parity checks; no device or golden cache."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import numpy as np

from test_nabo_mic import SRC, build, run_c

HERE = Path(__file__).resolve().parent
SR = 16000


def tone(hz, seconds=1, harmonics=(1,), phase=0.13):
    t = np.arange(int(SR * seconds)) / SR
    return 0.3 * sum(a * np.sin(2 * np.pi * hz * (i + 1) * t + phase)
                     for i, a in enumerate(harmonics)) / sum(harmonics)


def noisy_a4(snr):
    t = np.arange(SR * 3) / SR
    rng = np.random.default_rng(20261024)
    return (0.2 * np.sin(2 * np.pi * 440 * t) +
            0.2 / np.sqrt(2) * 10 ** (-snr / 20) * rng.standard_normal(len(t)))


def steady(result):
    # Analyze each even hop once; discard the first 80 ms of history filling.
    return result['f0'][5::2]


def accuracy(name, result, hz, min_coverage=1.0, max_cents=6):
    f0 = steady(result)
    voiced = f0[f0 > 0]
    assert len(voiced) >= min_coverage * len(f0), f'{name}: insufficient pitch coverage'
    if len(voiced):
        cents = np.abs(1200 * np.log2(voiced / hz))
        assert np.max(cents) <= max_cents, f'{name}: false pitch ({np.max(cents):.1f} cents)'
    assert np.all(result['f0Conf'][result['f0'] == 0] == 0), f'{name}: stale confidence'


def main():
    cases = []
    for hz in (60.5, 65.41, 82.41, 110, 146.83, 196, 261.63, 329.63,
               440, 659.25, 880, 1046.5):
        for label, harmonics in (('sine', (1,)), ('harmonic', (1, .5, .33, .2)),
                                 ('weak fundamental', (.2, 1, .6))):
            cases.append((f'{hz} Hz {label}', tone(hz, harmonics=harmonics), hz, 1, 6))
    for snr, coverage in ((20, 1), (12, 1), (10, 1), (9, 1), (8, .85), (7, .05), (6, 0)):
        cases.append((f'440 Hz SNR {snr} dB', noisy_a4(snr), 440, coverage, 30))
    rng = np.random.default_rng(440)
    for label, signal in (('silence', np.zeros(SR)), ('DC', np.full(SR, .25)),
                          ('white noise', .2 * rng.standard_normal(SR))):
        cases.append((label, signal, None, 0, 0))
    cases.append(('near-full-scale 440 Hz', tone(440) * 3.3, 440, 1, 6))
    transition = np.concatenate([tone(440), np.zeros(SR), tone(110), np.zeros(SR)])
    cases.append(('tone/silence transitions', transition, None, 0, 0))

    with tempfile.TemporaryDirectory(prefix='nabo-pitch-') as directory:
        lib = build(directory)
        results = []
        for name, signal, hz, coverage, cents in cases:
            result = run_c(lib, signal)
            results.append(result)
            if hz is not None:
                accuracy(name, result, hz, coverage, cents)
            elif name != 'tone/silence transitions':
                assert not np.any(steady(result)), f'{name}: spurious pitch'
            else:
                for second, target in ((0, 440), (1, None), (2, 110), (3, None)):
                    segment = result['f0'][second * 50 + 5:(second + 1) * 50:2]
                    if target is None:
                        assert not np.any(segment), 'Silence retained stale pitch'
                    else:
                        assert np.all(np.abs(segment - target) < 1), 'Pitch transition failed'
            print(f'PASS {name}')

        node = os.environ.get('NABO_NODE') or shutil.which('node')
        if not node:
            raise RuntimeError('Install Node.js or set NABO_NODE for prototype parity checks')
        # Use the same PCM16 quantization as the C shim; all fixtures are synthetic.
        samples = [np.clip(np.round(signal * 32767), -32768, 32767).astype(np.int16).tolist()
                   for _, signal, *_ in cases]
        reference = json.loads(subprocess.check_output(
            [node, str(HERE / 'pitch_reference.js')], input=json.dumps(samples), text=True))
        assert len(reference) == len(cases), 'Missing prototype signal results'
        for (name, *_), result, rows in zip(cases, results, reference):
            expected = np.asarray(rows)
            actual = result['f0'][1::2]
            assert len(expected) == len(actual), f'{name}: missing reference frames'
            assert np.array_equal(actual > 0, expected[:, 0] > 0), f'{name}: C/JS voiced disagreement'
            both = actual > 0
            if np.any(both):
                cents = np.abs(1200 * np.log2(actual[both] / expected[both, 0]))
                assert np.max(cents) < 3, f'{name}: C/JS pitch disagreement'
                assert np.max(np.abs(result['f0Conf'][1::2][both] - expected[both, 1])) < .002
        print(f'PASS C/JS parity: {len(cases)} signals, {sum(len(r) for r in reference)} pitch frames')

        # Prove the regression check detects the old accepted subharmonics.
        original = (SRC / 'nabo_mic.c').read_text(encoding='utf-8')
        guard = 'd[tau] <= d[best] + 1966u'
        assert original.count(guard) == 1, 'Update mutation anchor after changing the rejection rule'
        mutant = Path(directory) / 'unguarded.c'
        mutant.write_text(original.replace(guard, '0'), encoding='utf-8')
        mutant_dir = Path(directory) / 'mutant'
        mutant_lib = build(mutant_dir, mutant)
        result = run_c(mutant_lib, noisy_a4(8))
        try:
            accuracy('removed ambiguity guard', result, 440, .85, 30)
        except AssertionError:
            print('PASS removed ambiguity guard rejected by accuracy regression')
        else:
            raise AssertionError('The old subharmonic regression escaped the test')
    print('HOST ONLY: physical microphone noise, C3 timing and listening remain unverified')


if __name__ == '__main__':
    main()
