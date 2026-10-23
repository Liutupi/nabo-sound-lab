const {Flight,Clap}=require('../../prototype/new-games-model.js');
const f=new Flight(20261023,1);f.start(6000);const out=[];
for(let i=0;i<600;i++){f.step(20,6000+(Math.trunc(i/50)%5-2)*300,i%91<80);if(i%20===0)out.push(f.snapshot());}
const c=new Clap(120);c.start();for(let i=0;i<16;i++){let t=(i+4)*500+(i%4===0?-120:i%4===1?45:i%4===2?100:0);c.hit(t);c.step((i+4)*500+121);out.push(c.snapshot());}
console.log(JSON.stringify(out));
