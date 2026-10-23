// 用原型的 renderVoice 录复读标准答案：node record_golden.js <prototype/index.html> <signals_dir> <out_dir>
const {chromium}=require(process.env.PW_MODULE||'playwright');const fs=require('fs'),path=require('path');
const [html,sigDir,outDir]=process.argv.slice(2);
function readNpyF32(f){const b=fs.readFileSync(f),hl=b.readUInt16LE(8),off=10+hl;return new Float32Array(b.buffer.slice(b.byteOffset+off,b.byteOffset+b.length))}
(async()=>{const br=await chromium.launch({executablePath:process.env.PW_CHROME||undefined});fs.mkdirSync(outDir,{recursive:true});
 const p=await br.newPage();await p.goto('file://'+path.resolve(html));
 for(const f of ['voice.npy','tones.npy','beats.npy']){
  const x=Array.from(readNpyF32(path.join(sigDir,f)).subarray(0,48000));   // 最长 3 秒
  const g=await p.evaluate(x=>{const raw=Float32Array.from(x,v=>Math.max(-32768,Math.min(32767,Math.round(v*32767)))/32767);
    const r0=renderVoice(raw,0),out={x16:Array.from(raw,v=>Math.round(v*32767)),d:Array.from(r0.d),g:r0.g,voices:[]};
    for(let k=0;k<VOICES.length;k++){const r=renderVoice(raw,k);out.voices.push({name:VOICES[k].n,y:Array.from(r.out,v=>Math.max(-32768,Math.min(32767,Math.round(v*32768))))})}
    return out},x);
  fs.writeFileSync(path.join(outDir,f.replace('.npy','.json')),JSON.stringify(g));console.log(f,g.d.length,'8k samples, g=',g.g)}
 await br.close()})();
