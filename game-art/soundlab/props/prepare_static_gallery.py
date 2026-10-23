"""Build only the compact Round 4 review gallery; never edit an asset PNG."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE=Path(__file__).resolve().parent
BG='#0A0716'; CYAN='#35F0DC'; GOLD='#FFC94A'; INK='#C9C0E1'; DIM='#A18AB8'
canvas=Image.new('RGB',(1200,1500),BG)
draw=ImageDraw.Draw(canvas)
font_path='C:/Windows/Fonts/arial.ttf'
font=ImageFont.truetype(font_path,16)
small=ImageFont.truetype(font_path,12)
large=ImageFont.truetype(font_path,25)
section=ImageFont.truetype(font_path,19)

def label(x,y,text,colour=INK,f=font):
    draw.text((x,y),text,font=f,fill=colour)

def sprite(name,x,y,scale=1):
    im=Image.open(HERE/f'{name}.png').convert('RGBA')
    if scale!=1:
        im=im.resize((round(im.width*scale),round(im.height*scale)),Image.Resampling.NEAREST)
    canvas.paste(im,(x,y),im)
    return im

def line(y):
    draw.line((28,y,1172,y),fill='#29203E',width=1)

label(28,20,'NABO SOUND LAB / ROUND 4',CYAN,large)
label(28,55,'38 static assets | transparent PNG | target sizes and pixel enlargements',DIM)

sprite('title',28,88,2)
label(28,202,'title / 200 x 56 / 2x',GOLD,small)
sprite('title',516,108)
label(516,174,'title / native',GOLD,small)
sprite('head-24',858,86,4)
sprite('head-24',1000,123)
label(850,202,'head-24 / 4x + native',GOLD,small)
line(232)

label(28,248,'MENU GROUPS + TOOL ICONS',CYAN,section)
icons=['group-play','group-see','group-practice']+['icon-'+n for n in ['echo','shout','candle','rope','range','spectrum','waterfall','scope','chord','db','tuner','guitar','beat','pitch','settings']]
for i,name in enumerate(icons):
    x=28+(i%9)*128
    y=288+(i//9)*120
    label(x,y,name,GOLD,small)
    sprite(name,x+5,y+44)
    sprite(name,x+40,y+28,4)
    label(x,y+97,'16 px / native + 4x',DIM,small)
line(529)

label(28,546,'SIX VOICE PORTRAITS / 96 x 96 / 2x',CYAN,section)
for j,name in enumerate(['card-bass','card-baritone','card-tenor','card-alto','card-mezzo','card-soprano']):
    x=24+j*192
    sprite(name,x,582,2)
    label(x+12,784,name,GOLD,font)
line(817)

label(28,834,'ENVIRONMENT + BIRTHDAY PROPS',CYAN,section)
sprite('cake',28,879,2)
label(28,1085,'cake / 200 x 100 / 2x / no candles',GOLD,small)

sprite('ground-top',475,898,4)
sprite('ground-fill',568,887,4)
label(466,864,'ground-top + ground-fill / 4x',GOLD,small)
tile=Image.new('RGBA',(80,32))
top=Image.open(HERE/'ground-top.png')
fill=Image.open(HERE/'ground-fill.png')
for x in range(0,80,16):
    tile.paste(top,(x,0));tile.paste(fill,(x,8));tile.paste(fill,(x,24))
tile=tile.resize((320,128),Image.Resampling.NEAREST)
canvas.paste(tile,(466,960),tile)
label(466,1096,'Repeat preview / horizontal + vertical fill',DIM,small)

sprite('rope-post',829,887,2)
label(805,1060,'rope-post',GOLD,small)
label(805,1080,'12 x 80 / 2x',DIM,small)
for i,name in enumerate(['candle-a','candle-b']):
    x=912+i*105
    sprite(name,x+10,931,4)
    sprite(name,x+48,974)
    label(x,1038,name,GOLD,small)
    label(x,1060,'6 x 22 / 4x',DIM,small)
label(910,1093,'Unlit bodies; flames by engine',DIM,small)
line(1132)

label(28,1150,'FIVE BLANK SEAL BACKPLATES / 64 x 28 / 2x',CYAN,section)
for j in range(1,6):
    x=28+(j-1)*173
    sprite(f'stamp-{j}',x,1202,2)
    label(x+27,1270,f'stamp-{j}',GOLD,font)

sprite('card-frame',938,1151)
label(938,1452,'card-frame / 220 x 292',GOLD,small)
label(28,1340,'Card, seal and icon assets contain no lettering.',INK)
label(28,1372,'The NABO cap mark is the preserved identity reference.',DIM)
label(28,1404,'All displayed assets are unmodified; gallery uses #0A0716.',DIM)
label(28,1461,'2026-10-23',GOLD,small)

canvas.save(HERE/'static-gallery.png')
print(HERE/'static-gallery.png')
