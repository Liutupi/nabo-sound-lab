'use strict';
const rules=require('../../prototype/duel-rules.js');
const seeds=[0,1,2,42,20261023,0xffffffff,0x80000000,0x9e3779b9,7,31,123,999,65535,123456789,0x12345678,0xdeadbeef];
const offsets=[-181,-180,-121,-120,-61,-60,-1,0,1,60,61,120,121,180,181],out=[];
function snapshot(s,seed,scenario,t,result){out.push({seed,scenario,t,result,round:s.round,beat:s.beat,duration:s.duration,n:s.n,at:[...s.at],hit:s.hit,combo:s.combo,best:s.best,earned:s.earned,extras:s.extras,last_index:s.last_index,last_error:s.last_error,value:rules.value(s)});}
function event(s,seed,scenario,t){snapshot(s,seed,scenario,t,rules.tap(s,t));}
for(let round=1;round<=3;round++)for(let z=0;z<seeds.length;z++){
  const seed=seeds[z];let s;
  for(let k=0;k<offsets.length;k++){
    s=rules.init(seed,round);snapshot(s,seed,k,-999,-999);
    const index=offsets[k]>0?s.n-1:0;
    event(s,seed,k,s.at[index]+offsets[k]);event(s,seed,k,s.at[index]+offsets[k]);
  }
  s=rules.init(seed,round);snapshot(s,seed,100,-999,-999);
  event(s,seed,100,-100);event(s,seed,100,s.at[0]-181);
  for(let i=0;i<s.n;i++){event(s,seed,100,s.at[i]);event(s,seed,100,s.at[i]+1);}
  event(s,seed,100,s.duration);event(s,seed,100,s.duration+1);
  s=rules.init(seed,round);snapshot(s,seed,101,-999,-999);
  for(let i=0;i<s.n;i++)event(s,seed,101,s.at[i]+offsets[(i+z+round)%15]);
  event(s,seed,101,s.duration+1);
  s=rules.init(seed,round);snapshot(s,seed,102,-999,-999);
  for(let k=0;k<1002;k++)event(s,seed,102,s.at[0]);
  s=rules.init(seed,round);snapshot(s,seed,103,0,-999);
  snapshot(s,seed,103,s.at[0],-999);snapshot(s,seed,103,s.at[s.n-1],-999);
  snapshot(s,seed,103,s.duration,-999);
  s=rules.init(seed,round);snapshot(s,seed,104,-999,-999);
  event(s,seed,104,s.at[0]);event(s,seed,104,s.at[2]);event(s,seed,104,s.at[3]);
  event(s,seed,104,s.at[1]);event(s,seed,104,s.at[4]);
}
process.stdout.write(JSON.stringify(out));
