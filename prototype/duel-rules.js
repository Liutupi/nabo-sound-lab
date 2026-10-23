(function(root){
  function random(a){a.v=(a.v+0x6D2B79F5)>>>0;let t=a.v;t=Math.imul(t^(t>>>15),1|t);t=(t+Math.imul(t^(t>>>7),61|t))^t;return(t^(t>>>14))>>>0;}
  function init(seed,round){let s={at:[],hit:0,n:0,round,combo:0,best:0,earned:0,extras:0,last_index:-1,last_error:999,beat:Math.floor(60000/(90+(round-1)*15))};let a={v:(seed^Math.imul(round,0x9e3779b9))>>>0};for(let i=0;i<8;i++){s.at.push((4+i)*s.beat);if(round>1&&(random(a)&3)<round-1)s.at.push((4+i)*s.beat+Math.floor(s.beat/2));}s.n=s.at.length;s.duration=12*s.beat+250;return s;}
  function tap(s,t){if(t<s.at[0]-180||t>s.duration)return-1;let best=-1,err=181;for(let i=0;i<s.n;i++){let e=Math.abs(t-s.at[i]);if(!(s.hit&(1<<i))&&e<err){best=i;err=e;}}if(best<0){s.extras=Math.min(1000,s.extras+1);s.combo=0;return 0;}s.hit|=1<<best;s.last_error=t-s.at[best];let p=err<=60?100:err<=120?60:20;s.earned+=p;if(best!==s.last_index+1)s.combo=0;s.last_index=best;s.combo++;s.best=Math.max(s.best,s.combo);return p;}
  function value(s){return Math.max(0,Math.floor((s.earned-s.extras*20)*1000/(s.n*100)));}
  const api={init,tap,value};if(typeof module!=='undefined')module.exports=api;else root.duelRules=api;
})(globalThis);
