"""Host only: production C rules/audio + browser JS parity + deliberate mutation.
No ESP-IDF build, serial, flash, frozen package or production file changes.
"""
from pathlib import Path
import json,os,re,shutil,subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SRC=ROOT/'firmware/main/ui'
BUILD=HERE/'build'
TCC=ROOT/'dist/soundlab-0.1.1/offline-review/host-tools/tcc/tcc.exe'
NODE=shutil.which('node') or r'C:\Users\pc\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
def compile_model(model,output):
    cmd=[str(TCC),'-I'+str(SRC),str(model),str(SRC/'sl_sampler.c'),str(HERE/'test_model.c'),'-o',str(output)]
    subprocess.run(cmd,check=True,capture_output=True)
def main():
    BUILD.mkdir(exist_ok=True)
    exe=BUILD/'newgames-test.exe';compile_model(SRC/'newgames_model.c',exe)
    units=subprocess.run([str(exe)],capture_output=True,text=True,check=True).stdout.strip();print(units)
    c=json.loads(subprocess.check_output([str(exe),'--trace'],text=True))
    js=json.loads(subprocess.check_output([NODE,str(HERE/'trace.js')],text=True))
    assert c==js, next((f'snapshot {i}: C={a} JS={b}' for i,(a,b) in enumerate(zip(c,js)) if a!=b),'length mismatch')
    print(f'PASS C/JS exact parity: {len(c)} snapshots, same seed / timestamps / inputs')
    inline=re.search(r'<script>([\s\S]*?)</script>',(ROOT/'prototype/new-games.html').read_text(encoding='utf-8')).group(1)
    (BUILD/'prototype-inline.js').write_text(inline,encoding='utf-8')
    subprocess.run([NODE,'--check',str(BUILD/'prototype-inline.js')],check=True)
    print('PASS browser prototype inline JavaScript syntax')
    mutant=BUILD/'mutant.c'
    mutant.write_text((SRC/'newgames_model.c').read_text().replace('ae>120','ae>80'),encoding='utf-8')
    mexe=BUILD/'mutant-test.exe';compile_model(mutant,mexe)
    env=dict(os.environ);env['SDL_VIDEODRIVER']='dummy'
    bad=subprocess.run([str(mexe)],capture_output=True,env=env)
    assert bad.returncode!=0,'deliberate hit-window regression escaped tests'
    print('PASS deliberate narrower hit-window mutation rejected')
    report={'scope':'new games host rules and sampler audio only','unit_result':units,'c_js_snapshots':len(c),'exact_parity':True,'mutation_rejected':True,'sample_bytes':8000,'device_tested':False,'esp32_built':False}
    (HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
