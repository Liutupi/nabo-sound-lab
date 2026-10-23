"""Production C sprites and raster output under a fragmented-host allocator.

Every cache malloc above 13824 B, then 7680 B, is rejected. Build products live
only in a temporary directory. This does not build, flash or access an ESP32.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from host_tools import find_tcc

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
GFX = ROOT / 'firmware/main/gfx'
ASSETS = ROOT / 'firmware/assets/soundlab_assets.bin'
BUDGET = 32 * 1024
STAT_NAMES = ('attempts', 'allocations', 'frees', 'live_bytes', 'peak_bytes',
              'failures', 'bad_frees', 'guard_errors', 'live_blocks', 'largest_block', 'cache_bytes')


class Surface(C.Structure):
    _fields_ = [('part', C.c_void_p * 2), ('w', C.c_uint16), ('split', C.c_uint16)]


def read_assets(blob):
    assert blob[:4] == b'FNAS'
    count = struct.unpack_from('<H', blob, 6)[0]
    result = {}
    for i in range(count):
        name, offset, size = struct.unpack_from('<32sII', blob, 8 + i * 40)
        assert offset + size <= len(blob)
        data = blob[offset:offset + size]
        if data[:4] not in (b'FNBO', b'FNBR'):
            continue
        name = name.split(b'\0', 1)[0].decode()
        if data[:4] == b'FNBO':
            w, frames = struct.unpack_from('<HH', data, 6)
            h, offs = w, 137
        else:
            w, h, frames = struct.unpack_from('<HHH', data, 6)
            offs = 139
        result[name] = {'data': data, 'w': w, 'h': h, 'frames': frames, 'offs': offs}
    return count, result


def decode_all(image):
    """Independent asset-format oracle, including FNBR and delta skip runs."""
    data, w, h = image['data'], image['w'], image['h']
    pixels = bytearray(w * h)
    result = []
    for frame in range(image['frames']):
        offset = struct.unpack_from('<I', data, image['offs'] + frame * 4)[0]
        for y in range(h):
            at = offset + struct.unpack_from('<H', data, offset + y * 2)[0]
            x = 0
            while x < w:
                code = data[at]
                at += 1
                size = (code & 127) + 1
                assert x + size <= w
                if code & 128:
                    if not frame:
                        pixels[y * w + x:y * w + x + size] = bytes(size)
                else:
                    pixels[y * w + x:y * w + x + size] = data[at:at + size]
                    at += size
                x += size
        result.append(bytes(pixels))
    return result


class Driver:
    def __init__(self, path, blob):
        self.lib = lib = C.CDLL(str(path))
        self.blob = C.create_string_buffer(blob)
        lib.frag_init.argtypes = [C.c_void_p, C.c_uint]
        lib.frag_reset.argtypes = [C.c_uint, C.c_uint]
        lib.frag_stat.argtypes = [C.c_int]
        lib.frag_stat.restype = C.c_uint
        for name in ('frag_surface', 'frag_linear'):
            getattr(lib, name).argtypes = [C.c_char_p, C.c_int]
            getattr(lib, name).restype = C.c_void_p
        lib.frag_row.argtypes = [C.c_void_p, C.c_int]
        lib.frag_row.restype = C.c_void_p
        lib.frag_begin.argtypes = [C.c_int] * 7
        lib.frag_add.argtypes = [C.c_char_p] + [C.c_int] * 8
        lib.frag_fb.restype = C.c_void_p
        lib.frag_init(self.blob, len(blob))

    def stats(self):
        values = {name: self.lib.frag_stat(i) for i, name in enumerate(STAT_NAMES)}
        assert values['bad_frees'] == values['guard_errors'] == 0, values
        assert values['live_bytes'] == values['cache_bytes'], values
        assert values['live_bytes'] <= BUDGET, values
        return values

    def reset(self, limit=0, fail_at=0):
        self.clear()
        assert self.lib.frag_reset(limit, fail_at), self.stats()

    def clear(self):
        self.lib.frag_clear()
        stats = self.stats()
        assert stats['live_bytes'] == stats['live_blocks'] == 0, stats
        assert stats['allocations'] == stats['frees'], stats

    def surface(self, name, frame):
        return self.lib.frag_surface(name.encode(), frame)

    def describe(self, pointer):
        s = Surface.from_address(pointer)
        return s.part[0], s.part[1], s.w, s.split

    def pixels(self, pointer, image):
        assert pointer
        part0, part1, w, split = self.describe(pointer)
        assert part0 and w == image['w'] and 0 < split <= image['h']
        assert bool(part1) == (split < image['h'])
        result = []
        for y in range(image['h']):
            row = self.lib.frag_row(pointer, y)
            expected = part0 + y * w if y < split else part1 + (y - split) * w
            assert row == expected, ('row does not follow surface split', y, self.describe(pointer))
            result.append(C.string_at(row, w))
        return b''.join(result)

    def framebuffer(self):
        return C.string_at(self.lib.frag_fb(), 240 * 320 * 2)

    def close(self):
        # Windows keeps loaded DLLs locked, so unload before removing the temp directory.
        import _ctypes
        try:
            self.clear()
        finally:
            if os.name == 'nt':
                _ctypes.FreeLibrary(self.lib._handle)
            else:
                _ctypes.dlclose(self.lib._handle)
            self.lib = None


def build(cc, directory, name):
    target = directory / (name + ('.dll' if os.name == 'nt' else '.so'))
    command = [str(cc), '-shared', '-I' + str(GFX)]
    if os.name != 'nt':
        command += ['-fPIC', '-O2', '-std=c11']
    command += [str(HERE / 'fragmented_cache_driver.c'), str(GFX / 'fc_gfx.c'), '-o', str(target)]
    completed = subprocess.run(command, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    return target


def test_full_decode(reference, limited, images, gold, limit):
    frames = rejected = segmented = 0
    for name, image in images.items():
        reference.reset(); limited.reset(limit)
        size = image['w'] * image['h']
        fits_budget = size <= BUDGET
        fits_split = image['w'] * ((image['h'] + 1) // 2) <= limit
        for frame in range(image['frames']):
            reference.lib.frag_epoch(); limited.lib.frag_epoch()
            original = reference.surface(name, frame)
            actual = limited.surface(name, frame)
            if not fits_budget:
                assert not original and not actual, ('cache budget', name, frame)
                rejected += 1
                continue
            assert original and reference.pixels(original, image) == gold[name][frame], (name, frame, 'oracle')
            assert reference.lib.frag_linear(name.encode(), frame) == reference.describe(original)[0]
            if size > limit and not fits_split:
                assert not actual, ('unsupported two-part allocation must fail cleanly', name)
                assert limited.stats()['live_bytes'] == 0
                rejected += 1
                continue
            assert actual and limited.pixels(actual, image) == gold[name][frame], (name, frame, limit)
            assert limited.pixels(actual, image) == reference.pixels(original, image)
            descriptor = limited.describe(actual)
            linear = limited.lib.frag_linear(name.encode(), frame)
            if size > limit:
                assert descriptor[1] and not linear, (name, 'linear pointer exposed for split allocation')
                segmented += 1
            else:
                assert not descriptor[1] and linear == descriptor[0]
            assert limited.surface(name, frame) == actual
            assert limited.stats()['largest_block'] <= limit
            frames += 1
        reference.clear(); limited.clear()
    return {'matched_frames': frames, 'segmented_frames': segmented, 'expected_rejections': rejected}


def test_animation_reuse(driver, images, gold, limit):
    actions = [name for name in images if name.startswith('spr/') and images[name]['w'] == 120]
    assert len(actions) >= 6
    requests = 0
    for name in actions:
        driver.reset(limit)
        image = images[name]; n = image['frames']
        order = list(range(n)) * 3 + list(reversed(range(n))) * 2 + [n - 1, 0, n - 1, 1, 0, -7, n + 5]
        allocation_count = None
        for frame in order:
            driver.lib.frag_epoch()
            pointer = driver.surface(name, frame)
            expected = gold[name][max(0, min(frame, n - 1))]
            assert driver.pixels(pointer, image) == expected, (name, frame, limit)
            stats = driver.stats()
            if allocation_count is None:
                allocation_count = (stats['attempts'], stats['allocations'])
            assert (stats['attempts'], stats['allocations']) == allocation_count, (name, frame, stats)
            requests += 1
        assert allocation_count == (3, 2), (name, allocation_count)
        assert driver.stats()['live_bytes'] == 120 * 120
        driver.clear()
    return {'actions': len(actions), 'loop_reverse_wrap_requests': requests, 'allocations_per_action': 2}


def assert_held(driver, held, images):
    for pointer, descriptor, name, expected in held:
        assert driver.describe(pointer) == descriptor, ('pinned descriptor mutated', name)
        assert driver.pixels(pointer, images[name]) == expected, ('pinned pixels mutated', name)


def test_pinned_and_budget(driver, images, gold, limit):
    driver.reset(limit)
    large, small = 'spr/S-TALK/120', 'spr/S-TALK/76'
    for tick in range(100):
        driver.lib.frag_epoch(); held = []
        for name, frame in [(large, tick % images[large]['frames']),
                            (small, tick % images[small]['frames']),
                            (small, (tick + 3) % images[small]['frames'])]:
            pointer = driver.surface(name, frame)
            assert pointer
            held.append((pointer, driver.describe(pointer), name, gold[name][frame]))
            assert_held(driver, held, images)
        stats = driver.stats()
        assert stats['live_bytes'] == 25952 and stats['allocations'] == 4, stats
    driver.clear(); driver.reset(limit)
    # Copy a pinned segmented base to another segmented frame, with a third image.
    driver.lib.frag_epoch(); held = []
    for name, frame in [(large, 0), (large, 7), ('spr/S-IDLE/56', 2)]:
        pointer = driver.surface(name, frame)
        assert pointer
        held.append((pointer, driver.describe(pointer), name, gold[name][frame]))
        assert_held(driver, held, images)
    before = driver.stats()
    assert before['live_bytes'] == 31936
    assert not driver.surface(small, 5), 'Budget overflow invalidated pinned surfaces'
    after = driver.stats()
    assert before == after, ('budget rejection allocated or evicted pinned data', before, after)
    assert_held(driver, held, images)
    driver.lib.frag_epoch()
    assert driver.surface(small, 5), 'An unpinned cache entry was not evicted next epoch'
    driver.stats(); driver.clear()
    return {'mixed_epochs': 100, 'same_image_different_frames_pinned': True,
            'segmented_base_copied': True, 'budget_rejection_preserved_surfaces': True}


def test_failures(driver, images, gold):
    name = 'spr/S-TALK/120'
    # Attempt 1: 14400 rejected; attempt 2: first 7200 succeeds; attempt 3: fail.
    driver.reset(7680, 3); driver.lib.frag_epoch()
    assert not driver.surface(name, 4)
    stats = driver.stats()
    assert stats['attempts'] == 3 and stats['allocations'] == stats['frees'] == 1, stats
    assert stats['live_bytes'] == stats['live_blocks'] == stats['cache_bytes'] == 0, stats
    assert [driver.lib.frag_request(i) for i in range(driver.lib.frag_request_count())] == [14400, 7200, 7200]
    # Failed slots must be reusable immediately, without clearing or a new epoch.
    pointer = driver.surface(name, 4)
    assert driver.pixels(pointer, images[name]) == gold[name][4]
    driver.clear()
    # All attempted allocation sizes fail; no successful buffer can be retained.
    driver.reset(1); driver.lib.frag_epoch()
    assert not driver.surface(name, 0)
    stats = driver.stats()
    assert stats['allocations'] == stats['live_bytes'] == 0, stats
    driver.clear()
    # A failed second part also rolls back before stale-LRU eviction and retry.
    driver.reset(7680, 6)
    assert driver.surface(name, 0)
    driver.lib.frag_epoch()
    other = 'spr/S-IDLE/120'; pointer = driver.surface(other, 5)
    assert driver.pixels(pointer, images[other]) == gold[other][5]
    stats = driver.stats()
    assert (stats['attempts'], stats['allocations'], stats['frees']) == (9, 5, 3), stats
    assert stats['live_bytes'] == 14400
    driver.clear()
    # Zero-pinned LRU churn covers release of both segments, not just clear().
    driver.reset(7680)
    actions = [key for key in images if key.startswith('spr/') and images[key]['w'] == 120]
    for tick in range(50):
        driver.lib.frag_epoch(); action = actions[tick % len(actions)]
        frame = tick % images[action]['frames']
        pointer = driver.surface(action, frame)
        assert driver.pixels(pointer, images[action]) == gold[action][frame]
        driver.stats()
    assert driver.stats()['frees'] > 0
    driver.clear()
    return {'second_part_rollback': True, 'immediate_retry': True, 'stale_lru_retry': True,
            'total_failure': True, 'eviction_epochs': 50}


def render_pair(reference, limited, name, frame, tick, kind, multiple=False):
    settings = [(0, 0, 0, 0, 0, 0, 0), (-7, 9, 13, 53, 207, 211, 1),
                (11, -5, 29, 77, 166, 102, 0), (0, 0, 0, 0, 240, 320, 1)][tick % 4]
    for driver in (reference, limited):
        driver.lib.frag_begin(*settings)
        assert driver.lib.frag_add(name.encode(), frame, -9 if tick % 5 == 0 else 53,
                                   41, kind, (tick * 37) % 1024, 173 + tick % 3 * 43,
                                   217 + tick % 4 * 31, (255, 191, 0, 73)[tick % 4])
        if multiple:
            for other, f, x in [('spr/S-TALK/76', tick % 24, 5),
                                ('spr/S-TALK/76', (tick + 3) % 24, 157)]:
                assert driver.lib.frag_add(other.encode(), f, x, 192, 1, tick * 11, 256, 256, 255)
        driver.lib.frag_end(0)
        assert driver.lib.frag_mismatches() == driver.lib.frag_overflow() == 0
        driver.stats()
    assert reference.framebuffer() == limited.framebuffer(), (name, frame, tick, kind, 'raster mismatch')


def test_raster(reference, limited, images, limit):
    reference.reset(); limited.reset(limit)
    count = 0
    for name, image in images.items():
        # Static OP_IMG images stream directly even when their decoded image cannot fit.
        render_pair(reference, limited, name, image['frames'] - 1, count, 0)
        count += 1
        size = image['w'] * image['h']
        if size <= BUDGET and image['w'] * ((image['h'] + 1) // 2) <= limit:
            render_pair(reference, limited, name, image['frames'] // 2, count, 1)
            count += 1
            render_pair(reference, limited, name, 0, count, 2)
            count += 1
        reference.clear(); limited.clear()
    # Animated production OP_IMG and OP_AFF, mixed sizes, and repeated dirty passes.
    large = [name for name in images if name.startswith('spr/') and images[name]['w'] == 120]
    for name in large:
        reference.reset(); limited.reset(limit)
        for tick in range(images[name]['frames'] * 2 + 3):
            frame = (tick if tick % 2 else -tick) % images[name]['frames']
            render_pair(reference, limited, name, frame, tick, tick % 3, multiple=True)
            count += 1
        assert limited.stats()['allocations'] == 4, limited.stats()
    # An unchanged display list must really skip all strips in incremental mode.
    for driver in (reference, limited):
        driver.reset(limit if driver is limited else 0)
    render_pair(reference, limited, large[0], 2, 1, 1)
    for driver in (reference, limited):
        driver.lib.frag_begin(-7, 9, 13, 53, 207, 211, 1)
        driver.lib.frag_add(large[0].encode(), 2, 53, 41, 1, 37, 216, 248, 191)
        assert driver.lib.frag_end(0) == 0
        assert driver.lib.frag_mismatches() == 0
        driver.clear()
    return {'pixel_exact_frames': count, 'incremental_full_mismatches': 0,
            'op_img_affine_crop_offset_clip_alpha': True, 'unchanged_dirty_strips': 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, help='TinyCC executable (overrides NABO_TCC/PATH discovery)')
    args = parser.parse_args()
    args.cc = find_tcc(args.cc)
    blob = ASSETS.read_bytes()
    entry_count, images = read_assets(blob)
    assert entry_count == 95, ('Expected the current 95-entry asset pack', entry_count)
    assert {image['data'][:4] for image in images.values()} == {b'FNBO', b'FNBR'}
    gold = {name: decode_all(image) for name, image in images.items()}
    result = {'status': 'passed', 'scope': 'production C host decoder/raster; no hardware verification',
              'asset_entries': entry_count, 'images': len(images),
              'assets_sha256': hashlib.sha256(blob).hexdigest(), 'limits': {}}
    with tempfile.TemporaryDirectory(prefix='nabo-fragmented-cache-') as temp:
        directory = Path(temp)
        reference = Driver(build(args.cc, directory, 'reference'), blob)
        limited = Driver(build(args.cc, directory, 'limited'), blob)
        try:
            for limit in (13824, 7680):
                result['limits'][str(limit)] = {
                    'full_decode': test_full_decode(reference, limited, images, gold, limit),
                    '120px_animation': test_animation_reuse(limited, images, gold, limit),
                    'pinned_budget': test_pinned_and_budget(limited, images, gold, limit),
                    'raster': test_raster(reference, limited, images, limit)}
            result['failure_injection'] = test_failures(limited, images, gold)
        finally:
            try:
                limited.close()
            finally:
                reference.close()
    result['source_sha256'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in (GFX / 'fc_res.c', GFX / 'fc_res.h', GFX / 'fc_gfx.c')}
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
