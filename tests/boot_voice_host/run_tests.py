"""Real sl_audio/sl_sampler/fc_res host tests. Never opens a device or writes production files."""
from __future__ import annotations
import array, ctypes as C, hashlib, importlib.util, json, math, pathlib, struct, subprocess, sys, wave

HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[1]
OUT=ROOT/'dist/soundlab-0.1.2/offline-review/audio'
TCC=ROOT/'dist/soundlab-0.1.1/offline-review/host-tools/tcc/tcc.exe'
SOURCES=[ROOT/p for p in ('firmware/main/ui/sl_audio.c','firmware/main/ui/sl_audio.h',
 'firmware/main/ui/sl_sampler.c','firmware/main/ui/sl_sampler.h','firmware/main/ui/sc_core.c',
 'firmware/main/voice/nabo_voice.c','firmware/main/gfx/fc_res.c','tools/pack_assets_soundlab.py')]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def digest(b):return hashlib.sha256(b).hexdigest()
def cdiv(a,b):return a//b if a>=0 else -((-a)//b)
def clamp(a):return max(-32768,min(32767,a))
def fnas(data,name=b'snd/boot-welcome'):
    return struct.pack('<4sHH32sII',b'FNAS',1,1,name,48,len(data))+data
def parse(blob):
    magic,ver,n=struct.unpack_from('<4sHH',blob);assert (magic,ver)==(b'FNAS',1)
    ans={}
    for i in range(n):
        name,off,size=struct.unpack_from('<32sII',blob,8+40*i);name=name.split(b'\0')[0].decode()
        assert name not in ans and off%4==0 and off>=8+n*40 and off+size<=len(blob)
        ans[name]=(off,blob[off:off+size])
    return ans
def run():
    OUT.mkdir(parents=True,exist_ok=True)
    hashes={str(p.relative_to(ROOT)):sha(p) for p in SOURCES}
    wavpath=ROOT/'game-art/soundlab/audio/boot-welcome.wav'
    with wave.open(str(wavpath),'rb') as w:
        assert (w.getframerate(),w.getnchannels(),w.getsampwidth(),w.getcomptype())==(16000,1,2,'NONE')
        pcm=w.readframes(w.getnframes());samples=list(struct.unpack('<%dh'%(len(pcm)//2),pcm))
    # Execute the actual packer, changing only OUT to our isolated evidence directory.
    spec=importlib.util.spec_from_file_location('audio_audit_packer',ROOT/'tools/pack_assets_soundlab.py')
    pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
    pack.OUT=str(OUT/'isolated-pack');pack.main()
    blob=(OUT/'isolated-pack/soundlab_assets.bin').read_bytes();entries=parse(blob)
    off,packed=entries['snd/boot-welcome'];assert packed==pcm and not packed.startswith(b'RIFF')
    manifest=json.loads((OUT/'isolated-pack/assets_manifest.json').read_text(encoding='utf-8'))
    assert manifest['entries']['snd/boot-welcome']['bytes']==len(pcm)
    assert manifest['entries']['snd/boot-welcome']['sha256']==digest(pcm)[:16]
    dll=OUT/'audio-host.dll'
    cmd=[str(TCC),'-shared','-I'+str(ROOT/'firmware/main/gfx')]+[str(HERE/p) for p in ('memory.c','audio.c','sampler.c','resources.c')]+[str(ROOT/'firmware/main/voice/nabo_voice.c'),'-o',str(dll)]
    build=subprocess.run(cmd,capture_output=True,text=True);assert build.returncode==0,build.stderr
    lib=C.CDLL(str(dll));u8=C.POINTER(C.c_uint8);i16=C.POINTER(C.c_int16)
    for name in ('sl_boot_voice_start','sl_boot_voice_playing','sl_audio_busy','sl_sampler_open','sl_sampler_play','sl_sampler_recording','sl_sampler_playing','sl_voice_playing'):
        getattr(lib,name).restype=C.c_bool
    lib.fc_res_init.argtypes=[u8,C.c_uint32];lib.fc_res.argtypes=[C.c_char_p,C.POINTER(C.c_uint32)];lib.fc_res.restype=u8
    lib.sl_audio_fill.argtypes=[i16,C.c_int];lib.sl_sampler_push.argtypes=[i16,C.c_int]
    lib.sl_rec_begin.argtypes=[i16,C.c_int];lib.sl_rec_push.argtypes=[i16,C.c_int]
    for name in ('av_boot_pos','av_boot_samples','sl_audio_time','av_memory'):getattr(lib,name).restype=C.c_uint32
    lib.av_boot_pointer.restype=u8
    keep=[]
    def mount(b):
        a=(C.c_uint8*len(b)).from_buffer_copy(b);keep[:]=[a];lib.fc_res_init(a,len(b));return a
    def reset(b=blob):
        lib.sl_sampler_close();lib.sl_audio_init();mount(b);lib.av_memory_reset()
    def fill(n=320):
        a=(C.c_int16*n)(*([12345]*n));lib.sl_audio_fill(a,n);return list(a)
    def memory():return dict(zip(('live','peak','alloc_calls','free_calls','max_lock_depth','bad_unlock','remaining_lock_depth'),[lib.av_memory(i) for i in range(7)]))
    checks={}
    reset();assert not lib.sl_boot_voice_playing() and fill()==[0]*320
    checks['silent_initial_mixer']=True
    reset();assert lib.sl_boot_voice_start()
    ptr=lib.fc_res(b'snd/boot-welcome',None)
    assert C.cast(lib.av_boot_pointer(),C.c_void_p).value==C.cast(ptr,C.c_void_p).value
    got=[];sizes=[1,7,159,320,81];j=0
    while len(got)<len(samples)+640:got+=fill(sizes[j%len(sizes)]);j+=1
    assert got[:len(samples)]==samples and not any(got[len(samples):])
    assert not lib.sl_boot_voice_playing() and lib.av_boot_pos()==len(samples)
    assert lib.sl_audio_time()==len(got) and memory()['alloc_calls']==0
    assert not lib.sl_audio_busy()
    checks['actual_wav_one_shot']={'samples_compared':len(samples),'mismatches':0,'duration_seconds':len(samples)/16000,'post_end_silence_samples':len(got)-len(samples),'same_FNAS_pointer':True,'memory':memory()}
    for stop in ('sl_boot_voice_stop','sl_audio_stop_all'):
        reset();assert lib.sl_boot_voice_start();assert fill(200)==samples[:200]
        getattr(lib,stop)();assert not lib.sl_boot_voice_playing() and fill()==[0]*320
        assert lib.sl_boot_voice_start() and fill(50)==samples[:50]
    checks['stop_and_explicit_restart']={'boot_stop':True,'stop_all_stops_boot':True,'explicit_new_start_rewinds':True,'natural_end_never_auto_loops':True}
    rejected=[]
    for label,b in [('missing',fnas(pcm,b'other')),('empty',fnas(b'')),('one_byte',fnas(b'\0')),('odd',fnas(b'\0\0\0')),('out_of_bounds',fnas(b'\0\0')[:-1])]:
        reset(b);assert not lib.sl_boot_voice_start() and not lib.sl_boot_voice_playing() and fill()==[0]*320;rejected.append(label)
    checks['invalid_resource_rejected']=rejected
    # Known deterministic unclipped pluck provides an independent dry-mix trace.
    def dry_pluck(n):
        reset();lib.sl_pluck(69,0,20,5000);a=[]
        while len(a)<n:a+=fill(min(320,n-len(a)))
        return a
    duck_n=((len(samples)+640+319)//320)*320
    dry=dry_pluck(duck_n);assert max(abs(v) for v in dry)<8000
    reset();lib.sl_pluck(69,0,20,5000);assert lib.sl_boot_voice_start();mixed=[]
    while len(mixed)<duck_n:mixed+=fill()
    expected=[clamp(cdiv(a,4)+samples[i]) if i<len(samples) else a for i,a in enumerate(dry)]
    assert mixed==expected
    checks['actual_PCM_duck_exact']={'samples_compared':duck_n,'dry_gain':'integer truncation toward zero /4','mismatches':0,'unducked_samples_after_PCM_end':duck_n-len(samples)}
    fixture=[32767 if v>=0 else -32768 for v in dry[:6400]]
    reset(fnas(struct.pack('<%dh'%len(fixture),*fixture)));lib.sl_pluck(69,0,20,5000);assert lib.sl_boot_voice_start();mixed=[]
    while len(mixed)<len(fixture):mixed+=fill()
    expect=[clamp(cdiv(a,4)+b) for a,b in zip(dry,fixture)]
    assert mixed==expect and 32767 in mixed and -32768 in mixed
    checks['saturation_fixture']={'samples_compared':len(fixture),'positive_clamps':mixed.count(32767),'negative_clamps':mixed.count(-32768),'mismatches':0,'fixture_is_not_shipped_PCM':True}
    # Real sampler decimation/interpolation/fades at all three pitch settings.
    source=[round(16000*math.sin(i*.071)+2000*math.sin(i*.193)) for i in range(8000)]
    pairs=[cdiv(source[i]+source[i+1],2) for i in range(0,len(source),2)]
    peak=max(abs(v) for v in pairs);gain=max(512,min(16384,12000*4096//peak))
    def record():
        assert lib.sl_sampler_open();assert lib.sl_sampler_open();lib.sl_sampler_begin();j=0
        while j<len(source):
            n=min([1,127,320,53][j%4],len(source)-j);a=(C.c_int16*n)(*source[j:j+n]);lib.sl_sampler_push(a,n);j+=n
        assert lib.sl_sampler_end()==4000 and not lib.sl_sampler_recording()
    def reference(rate):
        pos=age=0;ans=[]
        while (pos>>16)+1<len(pairs):
            at=pos>>16;v=pairs[at]+((pairs[at+1]-pairs[at])*(pos&65535)>>16)
            env=age*512 if age<64 else 32768;remaining=(len(pairs)*65536-pos)//rate
            if remaining<64:env=min(env,remaining*512)
            v=cdiv(v*gain*env,32768*4096);ans.append(max(-18000,min(18000,v)));pos+=rate;age+=1
        return ans
    sampler=[]
    for key,rate in enumerate([32768,41285,49097]):
        reset();record();assert memory()['live']==8000 and memory()['alloc_calls']==1
        assert lib.sl_sampler_play(key);expect=reference(rate);a=[]
        while len(a)<len(expect)+320:a+=fill()
        assert a[:len(expect)]==expect and not any(a[len(expect):]) and not lib.sl_sampler_playing()
        # Replaying the sampler alongside actual boot PCM tests compatibility and duck ordering.
        assert lib.sl_sampler_play(key) and lib.sl_boot_voice_start();a=[]
        while len(a)<len(samples)+320:a+=fill()
        expected=[clamp(cdiv(expect[i] if i<len(expect) else 0,4)+v) for i,v in enumerate(samples)]
        assert a[:len(samples)]==expected and not any(a[len(samples):])
        assert lib.sl_sampler_play(key) and lib.sl_boot_voice_start();fill(200);lib.sl_boot_voice_stop()
        assert fill()==expect[200:520] and lib.sl_sampler_playing()
        before=memory();lib.sl_sampler_close();after=memory()
        assert before['alloc_calls']==1 and before['peak']==8000 and after['live']==0 and after['free_calls']==1
        assert after['max_lock_depth']<=1 and after['bad_unlock']==0 and after['remaining_lock_depth']==0
        sampler.append({'key':key,'rate_Q16':rate,'output_samples':len(expect),'solo_mismatches':0,'boot_overlay_mismatches':0,'boot_stop_preserves_sampler_and_restores_gain':True,'memory_after_close':after})
    checks['actual_sampler_all_three_keys']=sampler
    # Existing ADPCM record/play still passes through the same actual mixer.
    reset();a=(C.c_int16*len(source))(*source);lib.sl_rec_begin(None,0);lib.sl_rec_push(a,len(source));assert lib.sl_rec_end()==4000
    lib.sl_voice_play(11);out=[]
    for _ in range(27):out+=fill()
    assert any(out) and not lib.sl_voice_playing() and memory()['alloc_calls']==0
    checks['existing_ADPCM_record_play']={'samples16_recorded':len(source),'ended':True,'nonzero_output':True,'memory':memory()}
    # Stop a live, nonzero recording in the middle of a waveform. An abrupt
    # jump to zero is audible as a click even when no sample is clipped.
    stop_checks=[]
    for amplitude in (20000,-20000):
        reset();constant=(C.c_int16*8000)(*([amplitude]*8000));lib.sl_rec_begin(None,0);lib.sl_rec_push(constant,8000);lib.sl_rec_end()
        lib.sl_voice_play(11);playing=fill(320);last=playing[-1];assert abs(last)>10000
        lib.sl_audio_stop_all();tail=fill(320);steps=[abs(b-a) for a,b in zip([last]+tail,tail)]
        assert max(steps)<=abs(last)//80+2 and abs(tail[0])>0 and not any(tail[80:])
        assert all(abs(a)>=abs(b) for a,b in zip(tail,tail[1:])) and not lib.sl_voice_playing()
        assert memory()['alloc_calls']==0
        stop_checks.append({'last_live_sample':last,'maximum_stop_step':max(steps),'old_abrupt_step':abs(last),'tail_samples_nonzero':sum(v!=0 for v in tail),'silent_after_5ms':True,'no_heap_allocation':True})
    checks['forced_voice_stop_declick']=stop_checks
    # Bind BOOT lifecycle to the actual source: isolate exact functions for a tiny C guard test.
    core=(ROOT/'firmware/main/ui/sc_core.c').read_text(encoding='utf-8')
    assert 'static void boot_update(int dt)' in core and 'EL() >= 1400' in core and '.leave = boot_leave' in core
    lifecycle='\n'.join(line for line in core.splitlines() if line.startswith(('static bool boot_voice_started','static void boot_enter','static void boot_update','static void boot_leave')))
    guard=OUT/'boot-lifecycle.c'
    guard.write_text('#include <stdbool.h>\nstatic int elapsed,starts,stops;\nint EL(void){return elapsed;}\nvoid sl_boot_voice_stop(void){stops++;}\nbool sl_boot_voice_start(void){starts++;return true;}\n'+lifecycle+'\nint main(void){boot_enter();for(elapsed=0;elapsed<1400;elapsed++)boot_update(1);if(starts)return 1;for(elapsed=1400;elapsed<6000;elapsed++)boot_update(1);if(starts!=1)return 2;boot_leave();if(stops!=2)return 3;boot_enter();elapsed=1400;boot_update(1);return starts==2&&stops==3?0:4;}\n',encoding='utf-8')
    guardcmd=[str(TCC),str(guard),'-o',str(OUT/'boot-lifecycle.exe')]
    cp=subprocess.run(guardcmd,capture_output=True,text=True);assert cp.returncode==0,cp.stderr
    assert subprocess.run([str(OUT/'boot-lifecycle.exe')]).returncode==0
    checks['BOOT_source_lifecycle']={'extracted_exact_source_functions':True,'before_1400ms_no_start':True,'one_trigger_per_enter':True,'leave_calls_stop':True,'full_UI_not_executed_here':True}
    after={str(p.relative_to(ROOT)):sha(p) for p in SOURCES};assert hashes==after,'Production sources changed during test; rerun audit.'
    result={'status':'passed','scope':'source-host integration and isolated actual packer; not final release or C3/I2S acceptance',
      'wav':{'path':str(wavpath),'sha256':sha(wavpath),'PCM_sha256':digest(pcm),'PCM_bytes':len(pcm),'samples':len(samples),'sample_rate':16000,'peak':max(abs(v) for v in samples)},
      'isolated_pack':{'path':str(OUT/'isolated-pack/soundlab_assets.bin'),'sha256':digest(blob),'entries':len(entries),'boot_PCM_offset':off,'byte_identical_to_WAV_payload':True},
      'source_SHA256':hashes,'test_SHA256':{str(p.relative_to(ROOT)):sha(p) for p in sorted(HERE.glob('*')) if p.is_file()},'compiler':{'path':str(TCC),'sha256':sha(TCC),'build_command':cmd,'stdout':build.stdout,'stderr':build.stderr,'dll_SHA256':sha(dll),'lifecycle_build_command':guardcmd},
      'checks':checks,'hardware_accessed':False,'production_files_modified':False,
      'limitations':['Final release assets/app not present at start; isolated packer output only.','Host uses no-op platform lock with depth counters, not FreeRTOS scheduling.','No I2S device, physical volume, startup/exit timing under scheduler or speaker listening verified.','Mute here means inactive/silent sources; production has fixed BSP volume80 and no exposed mixer mute setting.','Portable TCC lacks ASan/UBSan.']}
    (OUT/'AUDIO_REVIEW.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS:',len(checks),'groups;',len(samples),'actual PCM samples; sampler 3 keys; no boot allocation.')
if __name__=='__main__':run()
