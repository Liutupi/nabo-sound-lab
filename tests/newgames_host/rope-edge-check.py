"""Host-only native raster check for the single rope x-range change.
Copies the existing audit fixture into this test's build directory. Original
audit sources, binaries, assets, serial ports and ESP32 build are untouched.
White palette mask uses the real packed transparency and production affine
rasterizer, centred in screen so its unclipped bbox can be translated to edges.
"""
from pathlib import Path
import ctypes as C, hashlib, json, math, subprocess
import numpy as np
from PIL import Image, ImageDraw

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
AUDIT=ROOT/'dist/soundlab-0.1.2/offline-review/ui'
BUILD=HERE/'build/rope-edge'
BUILD.mkdir(parents=True,exist_ok=True)
SRC=ROOT/'firmware/main/ui/sc_toys.c'
source=SRC.read_text(encoding='utf-8')
assert 'int rx = 45 + rp_prog * 153 / 1000' in source
fixture=(AUDIT/'audit_host.c').read_text(encoding='utf-8')+r'''
/* Test-only controls. Production rp_prog updates/rules remain unchanged. */
__declspec(dllexport) void rope_position(int prog,int tilt){rp_prog=prog;rp_tilt=tilt;}
__declspec(dllexport) int rope_frames(const char *id){char key[64];fc_img_t im;snprintf(key,sizeof key,"spr/%s/76",id);return fc_img_open(key,&im)?im.frames:0;}
__declspec(dllexport) const uint16_t *rope_mask(const char *id,int f,int ang){
  static uint16_t mask[FC_W*FC_H];static uint8_t white[126];
  char key[64];fc_img_t im;snprintf(key,sizeof key,"spr/%s/76",id);
  if(!fc_img_open(key,&im))return NULL;
  memset(white,255,sizeof white);im.pal=white;
  fc_gfx_begin();fc_gfx_bg(0,false);fc_gfx_scanlines(false);
  fc_img_aff(&im,f,120,152,38,(76*29+16)/32,ang,800*256/1000,800*256/1000,255);
  fc_gfx_end(to_fb,mask,true);return mask;
}
'''
p=BUILD/'rope-fixture.c';p.write_text(fixture,encoding='utf-8')
args=[str(ROOT/'dist/soundlab-0.1.1/offline-review/host-tools/tcc/tcc.exe'),'-shared','-I'+str(ROOT/'firmware/main/gfx'),str(p),str(AUDIT/'math_compat.c')]
args += [str(ROOT/'firmware/main'/q) for q in ['ui/sl_ui.c','ui/sl_audio.c','ui/sl_sampler.c','ui/sc_newgames.c','ui/newgames_model.c','ui/scenes.c','gfx/fc_gfx.c','gfx/fc_res.c','mic/nabo_mic.c','voice/nabo_voice.c']]
args += ['-o',str(BUILD/'rope-check.dll')]
r=subprocess.run(args,capture_output=True,text=True)
assert r.returncode==0,r.stderr
lib=C.CDLL(str(BUILD/'rope-check.dll'))
lib.host_init.argtypes=[C.c_char_p,C.c_uint]
lib.audit_prepare.argtypes=[C.c_char_p]
lib.audit_trace.restype=C.c_char_p
lib.host_fb.restype=C.POINTER(C.c_uint16)
lib.rope_frames.argtypes=[C.c_char_p]
lib.rope_mask.argtypes=[C.c_char_p,C.c_int,C.c_int]
lib.rope_mask.restype=C.POINTER(C.c_uint16)
assert lib.host_init(str(AUDIT/'audit-assets.bin').encode(),7)==0

def rgb(fb):
    a=np.asarray(fb,dtype=np.uint16)
    return np.stack((((a>>11)&31)*255//31,((a>>5)&63)*255//63,(a&31)*255//31),-1).reshape(320,240,3).astype(np.uint8)

def shot(case,progress,tilt,ms):
    assert lib.audit_prepare(case.encode())==0
    lib.rope_position(progress,tilt);lib.audit_clock(ms);lib.audit_clear()
    mm=C.c_int();ops=C.c_int();lib.host_frame(0,C.byref(mm),C.byref(ops))
    tr=json.loads(lib.audit_trace().decode())
    t=next(x for x in tr if x.get('requested','').startswith('S-ROPE-'))
    image=Image.fromarray(rgb(np.ctypeslib.as_array(lib.host_fb(),shape=(240*320,)).copy()))
    return dict(case=case,progress=progress,tilt=tilt,ms=ms,mismatch=mm.value,overflow=bool(ops.value&(1<<30)),sprite=t),image

# Every source frame at every integer rotation reachable by the target formula:
# abs(base)<=116 plus abs(jitter)<=41. Filter cannot leave [-157,157].
mask_records=[]
for ident,angles in [('S-ROPE-READY',[0]),('S-ROPE-WALK',range(-40,41)),('S-ROPE-WOBBLE',list(range(-157,-40))+list(range(41,158))),('S-ROPE-BOW',[0])]:
    nf=lib.rope_frames(ident.encode());assert nf>0
    for f in range(nf):
        for ang in angles:
            a=np.ctypeslib.as_array(lib.rope_mask(ident.encode(),f,ang),shape=(320,240))
            ys,xs=np.nonzero(a)
            assert len(xs)>0
            bb=[int(xs.min()-120),int(ys.min()-152),int(xs.max()-120),int(ys.max()-152)]
            # Includes the entire rendered sprite, stricter than pole alone.
            mask_records.append(dict(id=ident,frame=f,angle=ang,bbox_relative_to_foot=bb,left_at_start=45+bb[0],right_at_end=198+bb[2]))
    print(ident,nf,'frames checked',flush=True)
minimum=min(x['left_at_start'] for x in mask_records)
maximum=max(x['right_at_end'] for x in mask_records)
bad=[x for x in mask_records if x['left_at_start']<0 or x['right_at_end']>239]
ys=[152+(44*((rx-14)*1000//212)//1000)*(1000-(rx-14)*1000//212)//1000 for rx in range(45,199)]
# Conservative vertical enclosure: include maximum -4px BOW hop for every
# sprite/rotation, even though only BOW can actually use that hop.
vertical=[min(ys)-4+min(x['bbox_relative_to_foot'][1] for x in mask_records),max(ys)+max(x['bbox_relative_to_foot'][3] for x in mask_records)]
assert vertical[0]>=26 and vertical[1]<300,vertical

# Real complete production scenes: start/end, max sway both directions, and
# every BOW frame (including its engine-driven hop) plus terminal hold.
shots=[];images=[]
for case,progress,tilt in [('rope-ready',0,0),('rope-walk',0,40),('rope-walk',1000,-40),('rope-wobble',0,-157),('rope-wobble',0,157),('rope-wobble',1000,-157),('rope-wobble',1000,157),('rope-wobble',1000,151)]:
    row,im=shot(case,progress,tilt,0);shots.append(row);images.append(im)
for f in range(lib.rope_frames(b'S-ROPE-BOW')):
    row,im=shot('rope-bow',1000,0,math.ceil(f*1000/12));shots.append(row);images.append(im)
row,im=shot('rope-bow',1000,0,2000);shots.append(row);images.append(im)
selected=[0,1,2,3,4,5,6,7,8,13,18,len(shots)-1]
gallery=Image.new('RGB',(4*240,3*344+240),'#0A0716');draw=ImageDraw.Draw(gallery)
for i,j in enumerate(selected):
    row,im=shots[j],images[j]
    x=i%4*240;y=i//4*344
    draw.text((x+3,y+3),f"{row['case']} p={row['progress']} a={row['tilt']} t={row['ms']}",fill='white')
    gallery.paste(im,(x,y+24))
for i,j in enumerate([3,7]):
    # Exact native frame crop, nearest-neighbour enlarged for pole/head edge QA.
    x=i*480;y=3*344
    draw.text((x+3,y+3),f"Native edge crop 3x: {shots[j]['case']} tilt={shots[j]['tilt']}",fill='white')
    crop=(0,90,80,162) if i==0 else (160,90,240,162)
    gallery.paste(images[j].crop(crop).resize((240,216),Image.Resampling.NEAREST),(x,y+24))
gallery.save(HERE/'rope-edge-check.png')
images[3].save(BUILD/'left-extreme-native.png');images[7].save(BUILD/'right-extreme-native.png')
report=dict(scope='Host rendering only; not device acceptance. Intended FALL trajectory exits screen and is excluded.',source=str(SRC),source_sha256=hashlib.sha256(SRC.read_bytes()).hexdigest(),asset_sha256=hashlib.sha256((AUDIT/'audit-assets.bin').read_bytes()).hexdigest(),production_change='rx = 45 + rp_prog * 153 / 1000; rules unchanged',method=__doc__,mask_cases=len(mask_records),integer_tilt_bound=[-157,157],relative_sprite_x_bounds=[min(x['bbox_relative_to_foot'][0] for x in mask_records),max(x['bbox_relative_to_foot'][2] for x in mask_records)],minimum_sprite_x=minimum,maximum_sprite_x=maximum,conservative_sprite_y_bounds=vertical,viewport_clip=[0,26,240,300],out_of_bounds_cases=len(bad),bad_cases=bad,all_sprite_bounds=mask_records,scene_renders=len(shots),incremental_mismatches=sum(x['mismatch'] for x in shots),overflow_renders=sum(x['overflow'] for x in shots),scene_records=shots,native_extreme_screenshots=['build/rope-edge/left-extreme-native.png','build/rope-edge/right-extreme-native.png'],visual_review='Confirmed full hat, body, pole and pole end caps visible in both worst endpoint native scenes and success BOW hop/hold gallery.',device_tested=False)
(HERE/'rope-edge-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('sprite x bounds',minimum,maximum,'out of bounds',len(bad),'scene renders',len(shots),'mismatches',report['incremental_mismatches'],'overflow',report['overflow_renders'])
assert not bad and minimum>=1 and maximum<=238
assert report['incremental_mismatches']==0 and report['overflow_renders']==0
