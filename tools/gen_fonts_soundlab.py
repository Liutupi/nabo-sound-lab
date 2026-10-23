"""NABO 声音实验室 · 点阵字体子集（4 bpp），供固件直绘文字使用

python tools/gen_fonts_soundlab.py

字符集从 prototype/index.html 和固件 C 源码里的字符串自动统计，不手写。
大号字体（mono48、disp56）只放用得到的少数字符。
输出 game-art/soundlab/sprites/fonts/<名称>.fnt 和 fonts.json（源字体 SHA256、字号、字数、字节数）。
FNFT v1 格式与赛博算命机相同，见 tools/gen_fonts_fortune.py 的说明。
"""
import hashlib, json, os, re, struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, 'tools', 'fonts')
OUT = os.path.join(ROOT, 'game-art', 'soundlab', 'sprites', 'fonts')
FALLBACK = 'SourceHanSansCN-Medium.otf'

FACES = {   # 名称: (文件, 像素字号, 字符集)；真机上正文至少 14 px 才看得清
    'disp24': ('ZCOOLQingKeHuangYou-Regular.ttf', 26, 'cjk'),
    'disp18': ('ZCOOLQingKeHuangYou-Regular.ttf', 19, 'cjk'),
    'sans16': ('SourceHanSansCN-Medium.otf', 16, 'cjk'),  # 正文、底栏与升号音名
    'sans14': ('SourceHanSansCN-Medium.otf', 14, 'cjk'),
    'mono14': ('SpaceMono-Bold.ttf', 14, 'ascii'),
    'mono22': ('SpaceMono-Bold.ttf', 22, 'ascii'),
    'mono48': ('SpaceMono-Bold.ttf', 50, '0123456789-.:ABCDEFG#'),
    'disp56': ('ZCOOLQingKeHuangYou-Regular.ttf', 60, 'ABCDEFG#—-?'),
}
SYMS = '·—–…“”‘’★☆○×→←↑↓▲▼◀▶♯♭¢±⇅⊐⋮✲∿≋◔♩〽◉♮▦◎⤴⏤↕⚙♨✈✋♫♪!'
EXTRA = {'mono22': '开关', 'mono14': '·'}  # 实际数值栏也有中文状态和和弦分隔符
REQUIRED = {'sans16': '±▲▼#', 'sans14': '±', 'mono22': '开关', 'mono14': '·', 'mono48': '-', 'disp56': '—'}


def charset():
    s = open(os.path.join(ROOT, 'prototype', 'index.html'), encoding='utf-8').read()
    for d, _, fs in os.walk(os.path.join(ROOT, 'firmware', 'main')):
        for f in fs:
            if f.endswith('.c') or f.endswith('.h'):
                s += ''.join(re.findall(r'"((?:[^"\\\n]|\\.)*)"', open(os.path.join(d, f), encoding='utf-8').read()))
    cs = set(ch for ch in s if ('　' <= ch <= '鿿') or ('＀' <= ch <= '￯') or ch in SYMS)
    cs |= set(chr(c) for c in range(0x20, 0x7f))
    cs.add('±')  # 难度/阈值文案不可因字符统计过滤而悄悄漏掉
    return sorted(cs)


def cmap(path):
    from fontTools.ttLib import TTFont
    return TTFont(path, fontNumber=0).getBestCmap()


def build(name, file, px, chars):
    path = os.path.join(FONTS, file)
    font = ImageFont.truetype(path, px)
    fb = ImageFont.truetype(os.path.join(FONTS, FALLBACK), px)
    have = cmap(path); fbh = cmap(os.path.join(FONTS, FALLBACK)); fb_used = []; missing = []
    asc, desc = font.getmetrics()
    glyphs = []
    for ch in chars:
        if ch == ' ':
            continue
        f = font
        if ord(ch) not in have:
            if ord(ch) not in fbh:
                missing.append(ch); continue
            f = fb; fb_used.append(ch)
        bbox = f.getbbox(ch)
        if not bbox or bbox[2] <= bbox[0]:
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-x0, -y0), ch, fill=255, font=f)
        px4 = [(v + 8) // 17 for v in im.tobytes()]
        data = bytearray()
        for y in range(h):
            row = px4[y * w:(y + 1) * w] + [0]
            for x in range(0, w, 2):
                data.append((row[x] << 4) | row[x + 1])
        glyphs.append((ord(ch), w, h, x0, y0, int(round(f.getlength(ch))), bytes(data)))
    head = struct.pack('<4sHHBBBB', b'FNFT', 1, len(glyphs), asc + desc, asc, int(round(font.getlength(' '))), 0)
    idx = bytearray(); body = bytearray(); base = len(head) + 13 * len(glyphs)
    for cp, w, h, x0, y0, adv, data in glyphs:
        idx += struct.pack('<IIBBbbB', cp, base + len(body), w, h, x0, y0, adv)
        body += data
    blob = head + bytes(idx) + bytes(body)
    present = {g[0] for g in glyphs}
    required_missing = [ch for ch in REQUIRED.get(name, '') if ord(ch) not in present]
    if required_missing:
        raise ValueError('%s missing required UI glyphs: %s' % (name, ''.join(required_missing)))
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, name + '.fnt'), 'wb').write(blob)
    return dict(source=file, source_sha256=hashlib.sha256(open(path, 'rb').read()).hexdigest(), px=px, glyphs=len(glyphs),
                line_h=asc + desc, ascent=asc, bytes=len(blob), fallback=''.join(fb_used), missing=''.join(missing))


def main():
    chars = charset(); info = {}
    for name, (f, px, kind) in FACES.items():
        cs = chars if kind == 'cjk' else [c for c in chars if ord(c) < 0x80] if kind == 'ascii' else sorted(set(kind))
        cs = sorted(set(cs) | set(EXTRA.get(name, '')))
        info[name] = build(name, f, px, cs)
        print(name, info[name]['glyphs'], '字', info[name]['bytes'] // 1024, 'KB', '缺字:' + info[name]['missing'] if info[name]['missing'] else '', flush=True)
    info['_charset_size'] = len(chars)
    json.dump(info, open(os.path.join(OUT, 'fonts.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('合计', sum(v['bytes'] for k, v in info.items() if not k.startswith('_')) // 1024, 'KB')


if __name__ == '__main__':
    main()
