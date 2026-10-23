"""Prepare AI illustrations and exact-font title at 4x, then native size."""
import argparse
import json
import shutil
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parent
FONTROOT = ROOT.parents[2] / 'prototype' / 'fonts'


def title():
    latin = ImageFont.truetype(str(FONTROOT / 'SpaceMono-Bold.ttf'), 27 * 4)
    chinese = ImageFont.truetype(str(FONTROOT / 'ZCOOLQingKeHuangYou-Regular.ttf'), 24 * 4)
    texts = [('NABO', latin), ('声音实验室', chinese)]
    sizes = [(round(f.getlength(t)), f.getbbox(t)[3] - f.getbbox(t)[1]) for t, f in texts]
    total = sum(w for w, _ in sizes) + 7 * 4
    canvas = Image.new('RGBA', (800, 224))
    draw = ImageDraw.Draw(canvas)
    x = (800 - total) // 2
    for (text, face), (width, height) in zip(texts, sizes):
        y = (224 - height) // 2
        draw.text((x, y), text, font=face, anchor='lt', fill='#FF3DA6', stroke_width=8, stroke_fill='#FF3DA6')
        draw.text((x, y), text, font=face, anchor='lt', fill='#35F0DC', stroke_width=4, stroke_fill='#35F0DC')
        draw.text((x, y), text, font=face, anchor='lt', fill='#FFFFFF')
        x += width + 7 * 4
    assert canvas.getbbox()[0] > 0 and canvas.getbbox()[2] < 800
    canvas.save(ROOT / 'title-4x.png')
    canvas.resize((200, 56), Image.Resampling.LANCZOS).save(ROOT / 'title.png')
    source = {'text': 'NABO 声音实验室', 'layout': 'one line', 'size': [200, 56],
              'fonts': ['prototype/fonts/SpaceMono-Bold.ttf', 'prototype/fonts/ZCOOLQingKeHuangYou-Regular.ttf'],
              'fills': ['#FFFFFF', '#35F0DC', '#FF3DA6'], 'renderer': 'Pillow font outlines at4x'}
    (ROOT / 'title-source.json').write_text(json.dumps(source, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def raster():
    (ROOT / 'masters').mkdir(exist_ok=True)
    jobs = sorted((ROOT / 'static-jobs').glob('*.json'))
    for path in jobs:
        item = json.loads(path.read_text(encoding='utf-8'))
        key, size = item['id'], item['size']
        suffix = '-r' + str(item['revision']) if item.get('revision') else ''
        master = ROOT / 'masters' / f'{key}{suffix}.png'
        if not master.exists():
            shutil.copyfile(item['source'], master)
        im = Image.open(master).convert('RGBA')
        assert im.getchannel('A').getextrema() == (0, 255), key
        bbox = im.getchannel('A').point(lambda a: 255 if a > 32 else 0).getbbox()
        crop = im.crop(bbox)
        width, height = size[0] * 4, size[1] * 4
        padding = 4 if key == 'cake' else 8 if key.startswith('card-') else 2
        fit = ImageOps.contain(crop, (width - padding * 2, height - padding * 2), Image.Resampling.LANCZOS)
        high = Image.new('RGBA', (width, height))
        high.alpha_composite(fit, ((width - fit.width) // 2, (height - fit.height) // 2))
        high.save(ROOT / f'{key}-4x.png')
        high.resize(tuple(size), Image.Resampling.LANCZOS).save(ROOT / f'{key}.png')
    print('Prepared raster static jobs:', len(jobs))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--title-only', action='store_true')
    args = parser.parse_args()
    title()
    if not args.title_only:
        raster()
