// Evaluate only the prototype's pure voice reference, without opening a browser.
const fs=require('fs'),vm=require('vm');
const [htmlPath,inputPath,outputPath]=process.argv.slice(2);
const html=fs.readFileSync(htmlPath,'utf8');
const begin=html.indexOf('const STEP=['),end=html.indexOf('// ================= 屏幕',begin);
if(begin<0||end<begin)throw Error('Voice reference section not found');
const clamp=html.split(/\r?\n/).find(x=>x.startsWith('const clamp='));
const random=html.split(/\r?\n/).find(x=>x.startsWith('function mulberry32('));
if(!clamp||!random)throw Error('Prototype numeric helpers not found');
const context=vm.createContext({SR:16000});
vm.runInContext(clamp+'\n'+random+'\n'+html.slice(begin,end),context);
const previous=JSON.parse(fs.readFileSync(inputPath,'utf8'));
context.inputPCM=previous.x16;
const output=vm.runInContext(`(()=>{
  const raw=Float32Array.from(inputPCM,x=>x/32767),base=renderVoice(raw,0);
  return {x16:inputPCM,d:Array.from(base.d),g:base.g,voices:VOICES.map((v,k)=>({name:v.n,y:Array.from(renderVoice(raw,k).out,x=>clamp(Math.round(x*32768),-32768,32767))}))};
})()`,context);
fs.writeFileSync(outputPath,JSON.stringify(output));
console.log('Prototype pure voice reference:',output.d.length,'8k samples,',output.voices.length,'voices');
