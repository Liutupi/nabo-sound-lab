/* Deterministic counterpart of firmware/main/ui/newgames_model.c. */
(function(root){
  const clamp=(x,a,b)=>Math.max(a,Math.min(b,x)), I=Math.trunc;
  class Flight {
    constructor(seed=1,difficulty=0){this.seed=seed>>>0||1;this.difficulty=clamp(difficulty,0,2);this.state=0;this.lives=3;this.yq=156*256;this.elapsed=this.carry=this.silent_ms=this.flash_ms=this.score=this.cleared=this.seen=this.combo=this.reference=0;this.gate=Array.from({length:4},(_,i)=>({xq:(270+i*120)*256,y:108+this.random()%5*24,passed:0}));}
    random(){let t=this.seed=(this.seed+0x6D2B79F5)>>>0;t=Math.imul(t^(t>>>15),1|t);t^=t+Math.imul(t^(t>>>7),61|t);return(t^(t>>>14))>>>0;}
    gap(){return[88,74,62][this.difficulty];}
    start(midi){this.reference=clamp(midi,2400,9600);this.state=1;}
    tick(midi,voiced){this.elapsed+=20;this.flash_ms=Math.max(0,this.flash_ms-20);if(voiced){this.silent_ms=0;const target=clamp(156*256-I((midi-this.reference)*6*256/100),72*256,232*256);this.yq+=clamp(target-this.yq,-716,716);}else{this.silent_ms=clamp(this.silent_ms+20,0,60000);if(this.silent_ms>300)this.yq=clamp(this.yq+194,72*256,232*256);}for(const g of this.gate){g.xq-=I([60,72,84][this.difficulty]*256*20/1000);if(!g.passed&&g.xq<=54*256){g.passed=1;this.seen++;if(Math.abs(I(this.yq/256)-g.y)<=this.gap()/2-12){this.cleared++;this.combo++;this.score+=10+2*clamp(this.combo,0,5);}else{this.lives--;this.combo=0;this.flash_ms=320;}if(!this.lives||this.seen===12){this.state=this.lives?4:3;return;}}if(g.xq< -20*256){g.xq+=480*256;g.passed=0;g.y=108+this.random()%5*24;}}}
    step(dt,midi,voiced){if(this.state!==1||dt<=0)return;this.carry+=clamp(dt,0,1000);while(this.carry>=20&&this.state===1){this.carry-=20;this.tick(midi,voiced);}}
    snapshot(){return{state:this.state,score:this.score,lives:this.lives,seen:this.seen,cleared:this.cleared,yq:this.yq,seed:this.seed,gate:this.gate.map(g=>[g.xq,g.y,g.passed])};}
  }
  class Clap {
    constructor(bpm=96){this.state=0;this.period=I(60000/clamp(bpm,60,180));this.hit_mask=this.now=this.score=this.last_error=this.judged=this.hits=this.perfect=this.misses=this.extra=this.combo=this.best_combo=0;this.last_hit=-1073741824;}
    start(){this.state=1;}
    step(t){if(this.state!==1)return;this.now=t;while(this.judged<16&&t>(4+this.judged)*this.period+120){if(!(this.hit_mask&(1<<this.judged))){this.misses++;this.combo=0;}this.judged++;}if(this.judged===16)this.state=3;}
    hit(t){if(this.state!==1||t<0||t-this.last_hit<160)return 0;this.last_hit=t;const d=t-4*this.period;let i=d>=0?I((d+I(this.period/2))/this.period):-1;if(d<0&&d>=-120)i=0;const error=i>=0&&i<16?t-(4+i)*this.period:10000,ae=Math.abs(error);if(i<0||i>=16||ae>120){if(t>=4*this.period-120){this.extra++;this.combo=0;this.score=clamp(this.score-10,0,1600);return-1;}return 0;}if((this.hit_mask&(1<<i))||i<this.judged)return 0;this.hit_mask|=1<<i;this.hits++;this.last_error=error;this.combo++;this.best_combo=Math.max(this.best_combo,this.combo);if(ae<=45){this.perfect++;this.score+=100;return 2;}this.score+=60;return 1;}
    snapshot(){return{state:this.state,score:this.score,hits:this.hits,perfect:this.perfect,misses:this.misses,extra:this.extra,combo:this.combo,best_combo:this.best_combo,hit_mask:this.hit_mask};}
  }
  const api={Flight,Clap};if(typeof module!=='undefined')module.exports=api;root.NewGames=api;
})(typeof globalThis!=='undefined'?globalThis:this);
