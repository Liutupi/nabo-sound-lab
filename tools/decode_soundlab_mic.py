"""Strictly decode diagnostic C3 input snapshots; no serial/audio access."""
if not __debug__:
    raise RuntimeError('Run without -O/PYTHONOPTIMIZE')
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct
import wave
import numpy as np


def decode(text):
    captures = []
    current = None
    for line in text.splitlines():
        if line.startswith('SL_MIC_BEGIN '):
            assert current is None, 'Snapshot restarted before END'
            pairs = re.findall(r'(\w+)=([^\s]+)', line)
            fields = dict(pairs)
            assert len(fields) == len(pairs), 'Duplicate snapshot header field'
            assert fields.get('sr') == '16000' and fields.get('samples') == '2048'
            assert fields.get('format') == 'PCM16HEX'
            start, end = int(fields['start']), int(fields['end'])
            assert 0 <= start < 2**32 and 0 <= end < 2**32
            assert (end - start) % 2**32 == 2048
            current = {'header': fields, 'samples': []}
        elif line.startswith('SLMIC '):
            assert current is not None, 'PCM row outside snapshot'
            row = re.fullmatch(r'SLMIC (\d{4}) ([0-9a-f]{128})', line)
            assert row and int(row[1]) == len(current['samples']), 'Bad, missing or duplicate PCM row'
            for at in range(0, 128, 4):
                value = int(row[2][at:at+4], 16)
                current['samples'].append(value if value < 32768 else value - 65536)
        elif line.startswith('SL_MIC_END '):
            assert current and line == 'SL_MIC_END samples=2048'
            assert len(current['samples']) == 2048, 'Incomplete snapshot'
            captures.append(current)
            current = None
        elif line.startswith('SL_MIC_ERROR'):
            raise ValueError(line)
    assert current is None and captures, 'Missing END or no snapshots'
    return captures


def analyze(samples):
    x = np.asarray(samples, dtype=np.float64)
    rms = float(np.sqrt(np.mean(x*x)))
    spectrum = np.abs(np.fft.rfft((x-x.mean()) * np.hanning(len(x))))
    peaks = [k for k in range(2, len(spectrum)-1) if spectrum[k] > spectrum[k-1] and spectrum[k] >= spectrum[k+1]]
    peaks.sort(key=lambda k: spectrum[k], reverse=True)
    answer = []
    for k in peaks[:5]:
        # Interpolate log magnitude; this is a spectral peak, not an f0 verdict.
        a, b, c = np.log(np.maximum(spectrum[k-1:k+2], 1e-30))
        delta = .5*(a-c)/(a-2*b+c) if a-2*b+c else 0
        answer.append({'Hz': float((k+delta)*16000/len(x)), 'relative_dB': float(20*np.log10(max(spectrum[k], 1e-30)/max(float(spectrum.max()), 1e-30)))})
    return {'samples': len(samples), 'duration_s': len(samples)/16000,
            'peak': max(abs(v) for v in samples), 'mean_DC': float(x.mean()),
            'RMS_dBFS': 20*math.log10(rms/32768) if rms else None,
            'rail_samples': sum(v in (-32768, 32767) for v in samples),
            'near_rail_samples': sum(abs(v) >= 32767 for v in samples),
            'spectral_peaks': answer}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    raw = args.raw.read_bytes()
    captures = decode(raw.decode('utf-8'))
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    for i, capture in enumerate(captures):
        pcm = struct.pack('<2048h', *capture['samples'])
        path = args.output / f'{i:03d}-mic.wav'
        with wave.open(str(path), 'wb') as w:
            w.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            w.writeframes(pcm)
        records.append({'index': i, 'header': capture['header'], 'wav': str(path),
                        'PCM_sha256': hashlib.sha256(pcm).hexdigest(), **analyze(capture['samples'])})
    result = {'raw': str(args.raw.resolve()), 'raw_sha256': hashlib.sha256(raw).hexdigest(),
              'scope': '128 ms actual C3 input snapshots; not a continuous recording or external calibrated acoustic measurement',
              'captures': records}
    (args.output / 'MIC_REVIEW.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'snapshots': len(records), 'output': str(args.output)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
