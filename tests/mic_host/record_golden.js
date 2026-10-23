// 用原型自己的 analyzeHop 录标准答案：node record_golden.js <prototype/index.html> <signals_dir> <out_dir>
const {chromium}=require(process.env.PW_MODULE||'playwright');const fs=require('fs'),path=require('path');
const [html,sigDir,outDir]=process.argv.slice(2);
function readNpyF32(f){const b=fs.readFileSync(f),hl=b.readUInt16LE(8),off=10+hl;return new Float32Array(b.buffer.slice(b.byteOffset+off,b.byteOffset+b.length))}
(async()=>{const br=await chromium.launch({executablePath:process.env.PW_CHROME||undefined});fs.mkdirSync(outDir,{recursive:true});
 for(const f of fs.readdirSync(sigDir).filter(f=>f.endsWith('.npy'))){
  const p=await br.newPage();await p.goto('file://'+path.resolve(html));
  const x=Array.from(readNpyF32(path.join(sigDir,f)));
  const g=await p.evaluate(x=>{setupDSP();const rows=[];const orig=analyzeHop;let lastOn=0;
    analyzeHop=function(){orig();const on=A.onsets.length&&A.onsets[A.onsets.length-1]===A.hop*HOP/SR?1:0;
      rows.push({db:A.db,dbS:A.dbS,bands:Array.from(A.bands),wf:Array.from(WF.buf.subarray(WF.row*WF.cols,WF.row*WF.cols+WF.cols)),
        onset:on,bpm:A.bpm,bpmConf:A.bpmConf,f0:A.f0,f0Conf:A.f0Conf,chroma:Array.from(A.chroma),chord:A.chord,chordConf:A.chordConf})};
    const xs=Float32Array.from(x,v=>Math.max(-32768,Math.min(32767,Math.round(v*32767)))/32767); /* 和真机一样先量化成 int16 */for(let i=0;i<xs.length;i+=512)pushBlock(xs.subarray(i,i+512),false);return rows},x);
  fs.writeFileSync(path.join(outDir,f.replace('.npy','.json')),JSON.stringify(g));console.log(f,g.length,'hops');await p.close()}
 await br.close()})();
