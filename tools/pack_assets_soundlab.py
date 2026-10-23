"""NABO 声音实验室 · 素材总包 assets.bin

python tools/pack_assets_soundlab.py

收录：
  spr/<片段>/<尺寸>   NABO 动画（FNBO）      game-art/soundlab/sprites/<ID>/<尺寸>/frames.bin
  prop/<名称>          静态道具（FNBR）        game-art/soundlab/sprites/props/*.bin
  font/<名称>          点阵字体（FNFT）        game-art/soundlab/sprites/fonts/*.fnt
FNAS v1 格式与赛博算命机相同：头 '4s FNAS | H 版本 | H 条目数'，目录每条 40 字节（32 字节名称 | I 偏移 | I 长度），数据 4 字节对齐。
输出 firmware/assets/soundlab_assets.bin 与 assets_manifest.json（标出哪些片段还是占位）。
"""
import glob, hashlib, json, os, struct, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SP = os.path.join(ROOT, 'game-art', 'soundlab', 'sprites')
OUT = os.path.join(ROOT, 'firmware', 'assets')


def main():
    items = []
    for p in sorted(glob.glob(os.path.join(SP, 'S-*', '*', 'frames.bin'))):
        cid, size = p.split(os.sep)[-3:-1]
        items.append(('spr/%s/%s' % (cid, size), open(p, 'rb').read()))
    for p in sorted(glob.glob(os.path.join(SP, 'props', '*.bin'))):
        items.append(('prop/' + os.path.basename(p)[:-4], open(p, 'rb').read()))
    for p in sorted(glob.glob(os.path.join(SP, 'fonts', '*.fnt'))):
        items.append(('font/' + os.path.basename(p)[:-4], open(p, 'rb').read()))
    voice = os.path.join(ROOT, 'game-art', 'soundlab', 'audio', 'boot-welcome.wav')
    with wave.open(voice, 'rb') as wav:
        assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (16000, 1, 2), 'boot voice must be 16kHz mono PCM16'
        items.append(('snd/boot-welcome', wav.readframes(wav.getnframes())))
    for name, _ in items:
        assert len(name.encode()) < 32, name
    head = struct.pack('<4sHH', b'FNAS', 1, len(items))
    off = len(head) + 40 * len(items); off = (off + 3) & ~3
    dir_ = bytearray(); body = bytearray(); manifest = {}
    for name, data in items:
        pad = (-(off + len(body))) % 4; body += b'\0' * pad
        dir_ += struct.pack('<32sII', name.encode(), off + len(body), len(data))
        manifest[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()[:16],
                              placeholder=os.path.exists(os.path.join(SP, *name.split('/')[1:], 'PLACEHOLDER')) if name.startswith('spr/') else False)
        body += data
    blob = head + bytes(dir_) + b'\0' * (((len(head) + len(dir_) + 3) & ~3) - len(head) - len(dir_)) + bytes(body)
    assert len(blob) <= 6 * 1024 * 1024, 'assets partition overflow'
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'soundlab_assets.bin'), 'wb').write(blob)
    json.dump(dict(total=len(blob), entries=manifest), open(os.path.join(OUT, 'assets_manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(len(items), '项，', len(blob) // 1024, 'KB，其中占位片段', sum(1 for v in manifest.values() if v['placeholder']))


if __name__ == '__main__':
    main()
