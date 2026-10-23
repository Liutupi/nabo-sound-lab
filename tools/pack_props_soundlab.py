"""NABO 声音实验室 · Codex 静态素材 → FNBR（63 色 + 透明，RLE），逐像素回解

python tools/pack_props_soundlab.py

输入：game-art/soundlab/props/<名称>.png（原尺寸，不含 -4x 放大图）
输出：game-art/soundlab/sprites/props/<固件名>.bin 与 props-manifest.json
半透明像素先与屏幕底色 #0A0716 预混合，alpha < 0.35 视为透明（与动画一致）。
菜单图标按场景 id 改名：spectrum→spec，waterfall→water，chord→chroma。
"""
import glob, json, os, struct, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clip_to_sprites_soundlab import BG, rle_row, decode_rows, rgb565   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'game-art', 'soundlab', 'props')
OUT = os.path.join(ROOT, 'game-art', 'soundlab', 'sprites', 'props')
RENAME = {'icon-spectrum': 'icon-spec', 'icon-waterfall': 'icon-water', 'icon-chord': 'icon-chroma'}
SKIP_MARKERS = ('-4x', 'overview', 'gallery', 'review')
REFERENCE_FILES = {'guitar.png', 'mic.png', 'umbrella.png', 'pole.png', 'drumsticks.png', 'party-hat.png'}


def should_pack(filename):
    # 道具母图按完整文件名排除；icon-guitar.png 是固件图标。
    return filename.lower() not in REFERENCE_FILES and not any(s in filename.lower() for s in SKIP_MARKERS)


def pack(path):
    a = np.asarray(Image.open(path).convert('RGBA'), np.float32)
    h, w = a.shape[:2]
    al = a[..., 3] / 255
    mixed = np.uint8(np.clip(a[..., :3] * al[..., None] + BG * (1 - al[..., None]), 0, 255))
    m = al >= 0.35
    pix = mixed[m].reshape(-1, 1, 3)
    pal_img = Image.fromarray(pix).quantize(colors=63, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = pal_img.getpalette()[:63 * 3]; raw = raw + [0] * (63 * 3 - len(raw))
    pal = np.array(raw, np.uint8).reshape(63, 3)
    idx = np.asarray(Image.fromarray(mixed).quantize(palette=pal_img, dither=Image.Dither.NONE), np.uint8) + 1
    idx[~m] = 0
    rows = [rle_row(r) for r in idx]
    hdr = bytearray(); body = bytearray(); base = 2 * h
    for r in rows:
        hdr += struct.pack('<H', base + len(body)); body += r
    blob = bytes(hdr + body)
    assert len(blob) < 65536
    ok = np.array_equal(decode_rows(blob, None, w, h, False), idx)
    palb = b''.join(struct.pack('<H', rgb565(p)) for p in pal)
    head = struct.pack('<4sHHHHB', b'FNBR', 1, w, h, 1, 0)
    data = head + palb + struct.pack('<I', len(head) + len(palb) + 4) + blob
    err = float(np.abs(pal[idx[m] - 1].astype(np.float32) - mixed[m]).mean()) if m.any() else 0.0
    return data, dict(w=w, h=h, bytes=len(data), roundtrip_ok=ok, mean_color_err=round(err, 2), src_colors=int(len(np.unique(mixed[m], axis=0))))


def main():
    os.makedirs(OUT, exist_ok=True)
    man = {}
    for p in sorted(glob.glob(os.path.join(SRC, '*.png'))):
        b = os.path.basename(p)
        if not should_pack(b): continue
        name = RENAME.get(b[:-4], b[:-4])
        data, info = pack(p)
        open(os.path.join(OUT, name + '.bin'), 'wb').write(data)
        info['source'] = 'props/' + b; man[name] = info
        assert info['roundtrip_ok'], name
    json.dump(man, open(os.path.join(OUT, 'props-manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(len(man), '个静态素材，', sum(v['bytes'] for v in man.values()) // 1024, 'KB，回解全部一致；最大色差',
          max(v['mean_color_err'] for v in man.values()))


if __name__ == '__main__':
    main()
