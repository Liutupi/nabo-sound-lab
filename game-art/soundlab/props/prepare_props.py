"""Prepare requested 4x and native-size derivatives of image_gen prop masters."""
import json
import shutil
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parent
BG = (10, 7, 22, 255)
LABELS = ['吉他', '话筒', '鼓棒', '平衡杆', '伞', '派对帽']


def main():
    plan = json.loads((ROOT / 'generation-sources.json').read_text(encoding='utf-8'))
    (ROOT / 'masters').mkdir(exist_ok=True)
    board = Image.new('RGBA', (1200, 760), BG)
    draw = ImageDraw.Draw(board)
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 23)
    for i, item in enumerate(plan['items']):
        key, size = item['id'], tuple(item['size'])
        master = ROOT / 'masters' / f'{key}.png'
        if not master.exists():
            shutil.copyfile(item['source'], master)
        im = Image.open(master).convert('RGBA')
        assert im.getchannel('A').getextrema() == (0, 255), key
        bbox = im.getchannel('A').point(lambda x: 255 if x > 24 else 0).getbbox()
        crop = im.crop(bbox)
        big_size = tuple(x * 4 for x in size)
        fitted = ImageOps.contain(crop, (big_size[0] - 4, big_size[1] - 4), Image.Resampling.LANCZOS)
        big = Image.new('RGBA', big_size)
        big.alpha_composite(fitted, ((big_size[0] - fitted.width) // 2, (big_size[1] - fitted.height) // 2))
        big.save(ROOT / f'{key}-4x.png')
        native = big.resize(size, Image.Resampling.LANCZOS)
        native.save(ROOT / f'{key}.png')
        x, y = (i % 3) * 400, (i // 3) * 380
        draw.text((x + 24, y + 18), f'{LABELS[i]} / {key}', font=font, fill=(236, 232, 255))
        preview = ImageOps.contain(crop, (340, 225), Image.Resampling.LANCZOS)
        board.alpha_composite(preview, (x + (400 - preview.width) // 2, y + 60 + (225 - preview.height) // 2))
        draw.text((x + 24, y + 306), f'{size[0]} × {size[1]} px / 4× 母图', font=font, fill=(53, 240, 220))
        board.alpha_composite(native, (x + 260, y + 330 - native.height // 2))
    board.convert('RGB').save(ROOT / 'props-overview.png')
    print('Prepared 6 transparent native-size props, 6 exact 4x props, and overview.')


if __name__ == '__main__':
    main()
