"""Compile real production duel model/rules with TCC; simulate link on host only."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
from host_tools import find_tcc

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SRC=ROOT/'firmware/main/ui'
TCC=find_tcc()

def compile_c(model,out,snapshot):
    subprocess.run([str(TCC),'-I'+str(snapshot),str(model),str(snapshot/'duel_rules.c'),str(HERE/'network_test.c'),'-o',str(out)],check=True,capture_output=True,text=True)

def run(exe,arg=None):
    p=subprocess.run([str(exe)]+([arg] if arg else []),capture_output=True,text=True)
    diagnostic=p.stderr.strip()
    for prefix in (ROOT.as_posix()+'/',str(ROOT)+'\\'):
        diagnostic=diagnostic.replace(prefix,'')
    return {'passed':p.returncode==0,'exit_code':p.returncode,'stdout':p.stdout.strip(),'stderr':diagnostic}

def main():
    source_bytes={p.name:p.read_bytes() for p in [SRC/'duel_model.c',SRC/'duel_model.h',SRC/'duel_rules.c',SRC/'duel_rules.h']}
    production=source_bytes['duel_model.c'].decode('utf-8')
    report={'scope':'production pure C state machine, simulated two-clock packet queue; no ESP-IDF, radio, hardware, display or physical key validation',
            'device_tested':False,'esp32_built':False,'compiler':'existing TCC host compiler',
            'production_sha256':{name:hashlib.sha256(data).hexdigest() for name,data in source_bytes.items()},
            'test_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [HERE/'network_test.c',Path(__file__)]},
            'simulation':{'tick_ms':5,'base_delivery_ms':8,'bounded_main_packet_jitter_ms':23,'duplicate_extra_delay_max_ms':600,
                          'loss_percent':20,'duplicate_percent':25,'one_send_inflight_ms':8,
                          'host_guest_clock_offsets_ms':[0,4157,-27103,15037,-9123],
                          'uint32_clock_wrap':True,'packet_crc_corruption_bits':352,'scores':'host all perfect; guest alternating +90/-90/0ms',
                          'timestamp_source':'local monotonic pressed_at, not packet arrival',
                          'not_covered':['physical key/debounce latency','LCD/audio presentation timing','real ESP-NOW RF','clock drift','malicious authenticated peer resistance']}}
    with tempfile.TemporaryDirectory(prefix='nabo-duel-net-') as td:
        td=Path(td);snapshot=td/'production-snapshot';snapshot.mkdir()
        for name,data in source_bytes.items():(snapshot/name).write_bytes(data)
        exe=td/'network.exe';compile_c(snapshot/'duel_model.c',exe,snapshot)
        report['tests']={name:run(exe,arg) for name,arg in [('wire','--wire'),('identity','--identity'),('replay','--replay'),('timeouts','--timeouts'),('ack_loss','--ack-loss'),('partitions','--partitions'),('old_packet_keepalive','--keepalive'),('room_bounds','--rooms'),('round_bound','--round-bound'),('final_confirmation','--final-confirmation'),('full',None)]}
        mutations=[
            ('session_guard','session!=m->session','false','--identity'),
            ('wire_checksum','sd_get32(p+40)!=hash(p)','false','--wire'),
            ('same_round_conflict','m->got_score&&score!=m->theirs[round-1]','false','--replay'),
            ('disconnect_timeout','now-m->rx_at>8000','now-m->rx_at>800000','--timeouts'),
            ('ack_commit','if(!m->ack&&el>=(m->host?-500:-300))','if(m->host&&!m->ack&&el>=-500)','--ack-loss'),
            ('old_sequence_guard','!seq||(m->rx_seq&&(int32_t)(seq-m->rx_seq)<=0)','!seq','--keepalive'),
            ('final_ok_guard','else if(m->stage==SD_RESULT&&m->round<3&&sd_settled(m))','else if(m->stage==SD_RESULT&&sd_settled(m))','--round-bound'),
            ('round_start_guard','||(m->stage==SD_RESULT&&m->round<3&&sd_settled(m))','||(m->stage==SD_RESULT&&sd_settled(m))','--round-bound'),
            ('host_final_authority','(m->host?m->got_score:sd_settled(m))','sd_settled(m)','--final-confirmation'),
            ('abort_reason','emit(m,SD_LEAVE,1,0,0)','emit(m,SD_LEAVE,0,0,0)','--final-confirmation'),
            ('abort_after_final','m->stage!=SD_FINAL||a==1','m->stage!=SD_FINAL','--final-confirmation'),
        ]
        report['mutations']=[]
        for name,before,after,arg in mutations:
            if before not in production:
                report['mutations'].append({'name':name,'executed':False,'reason':'source guard text changed; adapt mutation before claiming coverage'});continue
            mutant=td/(name+'.c');mutant.write_text(production.replace(before,after,1),encoding='utf-8');mexe=td/(name+'.exe');compile_c(mutant,mexe,snapshot)
            r=run(mexe,arg);report['mutations'].append({'name':name,'executed':True,'rejected':not r['passed'],'test_result':r})
    report['passed']=all(t['passed'] for t in report['tests'].values()) and all(t.get('rejected',False) for t in report['mutations'])
    if report['tests']['full']['passed']:report['totals']=json.loads(report['tests']['full']['stdout'])
    (HERE/'network-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    raise SystemExit(0 if report['passed'] else 1)

if __name__=='__main__':main()
