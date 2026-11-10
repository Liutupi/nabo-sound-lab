/* Same scripted run as test_model.c --trace: two full rounds, snapshot every 200 ms. */
const {Circus}=require('../../prototype/circus-model.js');
const s=new Circus(20261110),out=[];let clock=0,clapT=0;
function input(i){
  const v=i%97<88;
  return{blow:i%40<30?150+(i*37)%851:0,midi100:6000+((Math.trunc(i/45)%7)-3)*110,voiced:v,clap_ms:clapT};
}
for(let round=0;round<2;round++){
  s.ok();
  for(let i=0;i<9000&&s.phase!==4;i++){
    if(s.phase===2&&s.stage===3){clapT+=20;const p=s.clap.period;if(clapT>=4*p){const k=Math.trunc((clapT-4*p)/p);if((clapT-4*p)%p<20&&k%5!==4)s.clapHit(clapT+(k%3===0?0:k%3===1?60:-100));}}else clapT=0;
    if(i===40&&round===1)s.ok();
    if(i===5000&&round===1)s.skip();
    s.step(i%7===0?40:20,input(i));clock++;
    if(i%10===0)out.push(s.snapshot());
  }
  out.push(s.snapshot());
}
console.log(JSON.stringify(out));
