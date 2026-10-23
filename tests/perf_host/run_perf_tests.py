"""Real-asset cache tests and pixel-exact old/new trace raster comparisons.

Run with the bundled Python; --cc may select a host C compiler. The default
portable TCC is the already-downloaded offline-review tool. No ESP32 build,
asset generation, serial access or edits to the frozen 0.1.1 delivery occur.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import time

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = HERE / 'out'
FROZEN = ROOT / 'dist/soundlab-0.1.1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def assets(blob):
    assert blob[:4] == b'FNAS'
    result = {}
    for i in range(struct.unpack_from('<H', blob, 6)[0]):
        name, offset, n = struct.unpack_from('<32sII', blob, 8 + 40 * i)
        result[name.split(b'\0')[0].decode()] = blob[offset:offset + n]
    return result


def decode_all(blob):
    assert blob[:4] == b'FNBO'
    size, count = struct.unpack_from('<HH', blob, 6)
    state = np.zeros((size, size), dtype=np.uint8)
    result = []
    for k in range(count):
        offset = struct.unpack_from('<I', blob, 137 + 4 * k)[0]
        for y in range(size):
            p = offset + struct.unpack_from('<H', blob, offset + 2 * y)[0]
            x = 0
            while x < size:
                code = blob[p]
                p += 1
                n = (code & 127) + 1
                assert x + n <= size
                if code & 128:
                    if not k:
                        state[y, x:x + n] = 0
                else:
                    state[y, x:x + n] = np.frombuffer(blob[p:p + n], np.uint8)
                    p += n
                x += n
        result.append(state.tobytes())
    return result


class Driver:
    def __init__(self, path, blob):
        self.lib = lib = C.CDLL(str(path))
        self.buffer = C.create_string_buffer(blob)
        lib.pf_init.argtypes = [C.c_void_p, C.c_uint]
        lib.pf_frame.argtypes = [C.c_char_p, C.c_int]
        lib.pf_frame.restype = C.c_void_p
        lib.pf_image.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int]
        lib.pf_plot.argtypes = [C.c_int] * 8
        lib.pf_columns.argtypes = [C.c_int, C.c_int]
        lib.pf_fb.restype = C.c_void_p
        lib.pf_init(self.buffer, len(blob))

    def pixels(self):
        return C.string_at(self.lib.pf_fb(), 240 * 320 * 2)

    def stats(self):
        return {key: getattr(self.lib, 'pf_' + key)() for key in
                ('cache_bytes', 'live_bytes', 'peak_bytes', 'alloc_calls', 'free_calls')}


def build(cc, baseline):
    target = OUT / ('baseline.dll' if baseline else 'fixed.dll')
    if os.name != 'nt':
        target = target.with_suffix('.so')
    command = [str(cc), '-shared', '-I' + str(ROOT / 'firmware/main/gfx')]
    if os.name != 'nt':
        command += ['-fPIC', '-O2', '-std=c11']
    if baseline:
        command += ['-DPF_BASELINE']
    command += [str(HERE / 'cache_driver.c'), str(HERE / 'raster_driver.c'),
                str(HERE / 'layout_probe.c'), '-o', str(target)]
    if os.name != 'nt':
        command += ['-lm']
    completed = subprocess.run(command, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    return target, {'command': command, 'stdout': completed.stdout, 'stderr': completed.stderr,
                    'sha256': sha(target.read_bytes())}


def test_single(driver, gold, name):
    driver.lib.pf_reset()
    n = len(gold[name])
    order = list(range(n)) * 6 + list(reversed(range(n))) * 3 + [n - 1, 0, n - 1, 1, 0]
    for frame in order:
        driver.lib.pf_epoch()
        pointer = driver.lib.pf_frame(name.encode(), frame)
        assert pointer and C.string_at(pointer, len(gold[name][frame])) == gold[name][frame]
        assert driver.lib.pf_cache_bytes() == driver.lib.pf_live_bytes()
    return {'requests': len(order), 'loop_and_reverse_pixel_matches': len(order), **driver.stats()}


def test_pinned_chorus(driver, gold, large, small):
    driver.lib.pf_reset()
    for tick in range(160):
        driver.lib.pf_epoch()
        requests = [(large, tick % len(gold[large])), (small, tick % len(gold[small])),
                    (small, (tick + 3) % len(gold[small]))]
        held = []
        for name, frame in requests:
            pointer = driver.lib.pf_frame(name.encode(), frame)
            assert pointer
            held.append((pointer, gold[name][frame]))
        # A later request must not mutate/evict any earlier pointer in this epoch.
        for pointer, expected in held:
            assert C.string_at(pointer, len(expected)) == expected
        assert driver.lib.pf_cache_bytes() == driver.lib.pf_live_bytes()
    return {'epochs': 160, 'simultaneous_decoded_bytes_required': 120 * 120 + 2 * 76 * 76,
            'all_three_images_present_and_unchanged': True, **driver.stats()}


def test_budget_pressure(driver, gold, large, small):
    driver.lib.pf_reset()
    driver.lib.pf_epoch()
    held = []
    for name, frame in [(large, 0), (small, 0), (small, 1), (small, 2)]:
        pointer = driver.lib.pf_frame(name.encode(), frame)
        assert pointer
        held.append((pointer, gold[name][frame]))
    rejected = driver.lib.pf_frame(small.encode(), 3)
    assert not rejected, 'An impossible extra request must fail rather than invalidate pinned frames'
    for pointer, expected in held:
        assert C.string_at(pointer, len(expected)) == expected
    before = driver.stats()
    driver.lib.pf_epoch()
    assert driver.lib.pf_frame(small.encode(), 3)
    return {'over_budget_request_rejected_without_corruption': True,
            'next_epoch_reuse_succeeded': True, 'pinned_peak': before}


def test_chorus_render(old, fixed, gold, large, small):
    old.lib.pf_reset(); fixed.lib.pf_reset()
    for tick in range(110):
        for driver in (old, fixed):
            driver.lib.pf_begin()
            driver.lib.pf_image(small.encode(), tick % len(gold[small]), 10, 190, 1)
            driver.lib.pf_image(small.encode(), (tick + 3) % len(gold[small]), 154, 190, 1)
            driver.lib.pf_image(large.encode(), tick % len(gold[large]), 60, 70, 1)
            driver.lib.pf_end(0)
            assert driver.lib.pf_mismatch() == 0 and driver.lib.pf_overflow() == 0
        assert old.pixels() == fixed.pixels(), ('chorus bitmap mismatch', tick)
    return {'frames': 110, 'old_vs_fixed_pixel_mismatch': 0,
            'incremental_vs_full_mismatch': 0, 'all_characters_retained': True,
            'fixed_cache': fixed.stats()}


def test_plots(old, fixed):
    for seed in range(160):
        dx, dy = [0, -9, 13, 2][seed % 4], [0, 11, -7, 3][seed % 4]
        clip = [(0, 0, 0, 0), (7, 55, 213, 199), (40, 96, 83, 25), (0, 285, 240, 20)][seed % 4]
        values = []
        for driver, fast in ((old, 0), (fixed, 0), (fixed, 1)):
            x, n = [(0, 240), (23, 173), (-13, 222)][(seed // 4) % 3]
            driver.lib.pf_columns(x, n)
            driver.lib.pf_plot(seed, fast, dx, dy, *clip)
            driver.lib.pf_end(0)
            assert driver.lib.pf_mismatch() == 0 and driver.lib.pf_overflow() == 0
            values.append(driver.pixels())
        assert values[0] == values[1] == values[2], ('waveform pixel mismatch', seed)
    # Same unchanged frame must genuinely skip strips in incremental mode.
    fixed.lib.pf_columns(0, 240)
    fixed.lib.pf_plot(9, 1, 0, 0, 0, 0, 0, 0); fixed.lib.pf_end(0)
    fixed.lib.pf_plot(9, 1, 0, 0, 0, 0, 0, 0)
    strips = fixed.lib.pf_end(0)
    assert strips == 0 and fixed.lib.pf_mismatch() == 0
    return {'cases': 160, 'variants': ['sine', 'dense noise', 'impulse', 'near-flat'],
            'offset_and_clipping_cases': True, 'old_callback_vs_new_spans_pixel_mismatch': 0,
            'nonzero_and_negative_source_column_origins': True,
            'incremental_vs_full_mismatch': 0, 'unchanged_frame_dirty_strips': strips}


def benchmark(driver, fast, count):
    driver.lib.pf_columns(0, 240)
    elapsed = []
    for repeat in range(3):
        start = time.perf_counter()
        for i in range(count):
            driver.lib.pf_plot(4 * i + 3, fast, 0, 0, 0, 0, 0, 0)
            driver.lib.pf_end(1)   # two complete passes, the second checks pixels
        elapsed.append((time.perf_counter() - start) * 1000 / count)
    return {'iterations_per_repeat': count, 'repeats': 3, 'ms_per_two_pass_host_frame': elapsed,
            'median_ms': statistics.median(elapsed)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, default=FROZEN / 'offline-review/host-tools/tcc/tcc.exe')
    parser.add_argument('--benchmark-frames', type=int, default=200)
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    blob = (FROZEN / 'soundlab_assets.bin').read_bytes()
    assert sha(blob) == '126cc7858872001ed6d387c7bfd9ea693e4e25b4f4fb35749c7d0489ca8b686f'
    entries = assets(blob)
    names = ('spr/S-IDLE/120', 'spr/S-TALK/120', 'spr/S-TALK/76')
    gold = {name: decode_all(entries[name]) for name in names}
    builds = {label: build(args.cc, label == 'baseline') for label in ('baseline', 'fixed')}
    old, fixed = (Driver(builds[label][0], blob) for label in ('baseline', 'fixed'))
    layout = {label: {'op_bytes': driver.lib.pf_op_size(), 'display_list_bytes': driver.lib.pf_display_list_bytes()}
              for label, driver in [('baseline', old), ('fixed', fixed)]}
    assert layout['baseline'] == layout['fixed'], 'Span op must not enlarge the display list'
    single = {label: test_single(driver, gold, names[0]) for label, driver in [('baseline', old), ('fixed', fixed)]}
    assert single['fixed']['alloc_calls'] == 1 and single['fixed']['cache_bytes'] == 14400
    chorus = test_pinned_chorus(fixed, gold, names[1], names[2])
    assert chorus['peak_bytes'] <= 32768 and chorus['alloc_calls'] == 3
    pressure = test_budget_pressure(fixed, gold, names[1], names[2])
    rendered = test_chorus_render(old, fixed, gold, names[1], names[2])
    plots = test_plots(old, fixed)
    old_timing = benchmark(fixed, 0, args.benchmark_frames)
    new_timing = benchmark(fixed, 1, args.benchmark_frames)
    result = {'status': 'passed', 'frozen_assets_SHA256': sha(blob),
              'frozen_app_SHA256': sha((FROZEN / 'nabo-soundlab-passport.bin').read_bytes()),
              'compiler': str(args.cc), 'compiler_SHA256': sha(args.cc.read_bytes()),
              'builds': {label: info for label, (_, info) in builds.items()},
              'source_SHA256': {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in
                               [ROOT / 'firmware/main/gfx/fc_res.c', ROOT / 'firmware/main/gfx/fc_gfx.c',
                                HERE / 'baseline/fc_res_0_1_1.c', HERE / 'baseline/fc_gfx_0_1_1.c']},
              'single_image_loop_and_reverse': single, 'simultaneous_chorus_cache': chorus,
              'static_display_list_layout': layout,
              'pinned_budget_pressure': pressure, 'rendered_chorus': rendered, 'waveform_pixels': plots,
              'waveform_host_benchmark': {'callback': old_timing, 'vertical_spans': new_timing,
                                          'median_speedup': old_timing['median_ms'] / new_timing['median_ms'],
                                          'scope': 'Synthetic four-layer waveform workload, TCC/host, two render passes per timing sample; not C3/SPI performance'},
              'hardware_accessed': False, 'ESP32_build_performed': False, 'old_dist_repacked': False,
              'limitations': ['Host results do not prove physical free heap, SPI frame timing or audible output.',
                              'Cache accounting tracks requested index-buffer bytes, excluding allocator overhead.',
                              'Baseline is frozen pre-fix source, not execution of the old C3 app.',
                              'The UI owner must route trace_draw through fc_vspans for scope waveform to use this path.']}
    (OUT / 'performance-host.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'single': single, 'chorus': chorus,
                      'raster_cases': plots['cases'], 'waveform_host_speedup': result['waveform_host_benchmark']['median_speedup']}, indent=2))


if __name__ == '__main__':
    main()
