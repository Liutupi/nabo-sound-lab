"""Decode a complete actual-device RGB565HEX capture; reject missing/duplicate rows."""
from pathlib import Path
import argparse, hashlib, json, re, struct
from PIL import Image

def decode(text):
    frames=[]; current=None
    for line in text.splitlines():
        if line.startswith('SL_CAPTURE_BEGIN '):
            assert current is None,'Incomplete/interleaved capture'
            fields=dict(part.split('=',1) for part in line.split()[1:])
            assert fields.get('w')=='240' and fields.get('h')=='320' and fields.get('fmt')=='RGB565HEX'
            current={'fields':fields,'rows':{}}
        elif line.startswith('SLPIX ') and current is not None:
            found=re.fullmatch(r'SLPIX (\d{3}) ([0-9a-fA-F]{960})',line)
            assert found,'Malformed pixel row'
            y=int(found[1]); assert 0<=y<320 and y not in current['rows'],'Duplicate/invalid row'
            current['rows'][y]=[int(found[2][i:i+4],16) for i in range(0,960,4)]
        elif line.startswith('SL_CAPTURE_END ') and current is not None:
            assert line=='SL_CAPTURE_END rows=320'
            assert set(current['rows'])==set(range(320)),'Missing pixel rows'
            frames.append(current); current=None
    assert current is None,'Truncated capture'
    assert frames,'No complete actual-device capture found'
    return frames

def main():
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    results=[]
    for i,f in enumerate(decode(a.log.read_text(encoding='utf-8',errors='replace'))):
        words=[v for y in range(320) for v in f['rows'][y]]
        raw=struct.pack('<76800H',*words)
        colors=[(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in words]
        im=Image.new('RGB',(240,320));im.putdata(colors)
        name=f"{i:03d}-{f['fields']['scene']}-{f['fields']['t']}.png";im.save(a.out/name)
        results.append({**f['fields'],'png':name,'rows':320,'rgb565_sha256':hashlib.sha256(raw).hexdigest(),'origin':'actual device USB capture; not host simulation'})
    (a.out/'captures.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'{len(results)} complete native 240x320 device captures decoded')
if __name__=='__main__':main()
