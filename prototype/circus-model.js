/* Deterministic counterpart of firmware/main/ui/circus_model.c. Same seed and inputs give the same state at every step. */
(function(root){
  const NG=typeof module!=='undefined'?require('./new-games-model.js'):root.NewGames;
  const clamp=(x,a,b)=>Math.max(a,Math.min(b,x)), I=Math.trunc;
  const TITLE=0,INTRO=1,PLAY=2,SCORE=3,FINAL=4, CANDLE=0,ROPE=1,FLIGHT=2,CLAP=3,STAGES=4;
  const EV_PLAY=1,EV_SCORE=2,EV_FINAL=4, CANDLES=6;
  const INTRO_MS=2600,SCORE_MS=2400,CANDLE_LIMIT_MS=12000,ROPE_LIMIT_MS=14000,ROPE_WALK_MS=6000,FLIGHT_WAIT_MS=8000,CLAP_BPM=96;
  class Circus {
    constructor(seed=1){this.init(seed);}
    init(seed){
      this.seed=seed>>>0||1;this.phase=TITLE;this.stage=0;this.t=0;this.carry=0;this.total=0;this.score=[0,0,0,0];
      this.life=new Array(CANDLES).fill(0);this.lit=0;this.blow_t0=0;this.blow_ms=0;
      this.target=0;this.cents=0;this.lock=0;this.prog=0;this.dev_n=0;this.dev_sum=0;
      this.flight={state:0,cleared:0,lives:0,yq:0};this.hum_ms=0;   /* zeroed like the C struct until the stage begins */this.clap=new NG.Clap(CLAP_BPM);
    }
    random(){let t=this.seed=(this.seed+0x6D2B79F5)>>>0;t=Math.imul(t^(t>>>15),1|t);t^=t+Math.imul(t^(t>>>7),61|t);return(t^(t>>>14))>>>0;}
    stageBegin(){
      this.phase=PLAY;this.t=0;this.carry=0;
      if(this.stage===CANDLE){for(let i=0;i<CANDLES;i++)this.life[i]=500+((i*7)%5)*150;this.lit=CANDLES;this.blow_t0=-1;this.blow_ms=0;}
      else if(this.stage===ROPE){this.target=this.cents=this.lock=this.prog=this.dev_n=this.dev_sum=0;}
      else if(this.stage===FLIGHT){this.flight=new NG.Flight(this.random(),0);this.hum_ms=0;}
      else{this.clap=new NG.Clap(CLAP_BPM);this.clap.start();}
    }
    stageScore(){
      if(this.stage===CANDLE){
        if(this.lit)return I((CANDLES-this.lit)*40/CANDLES);
        return 100-clamp(I((this.blow_ms-1500)*60/6500),0,60);
      }
      if(this.stage===ROPE){
        if(!this.dev_n)return 0;
        const mean=I(this.dev_sum/this.dev_n),q=clamp(1000-I(mean*1000/80),0,1000);
        return I(I(this.prog*100/ROPE_WALK_MS)*(350+I(650*q/1000))/1000);
      }
      if(this.stage===FLIGHT)return I(this.flight.cleared*100/12);
      return clamp(I(this.clap.score/16),0,100);
    }
    stageEnd(){
      this.score[this.stage]=clamp(this.stageScore(),0,100);
      this.total=0;for(let i=0;i<=this.stage;i++)this.total+=this.score[i];
      this.phase=SCORE;this.t=0;this.carry=0;return EV_SCORE;
    }
    advance(){
      this.t=0;this.carry=0;
      if(this.stage+1>=STAGES){this.phase=FINAL;return EV_FINAL;}
      this.stage++;this.phase=INTRO;return 0;
    }
    tick(input){
      this.t+=20;
      if(this.stage===CANDLE){
        if(input.blow){
          if(this.blow_t0<0)this.blow_t0=this.t-20;
          for(let i=0;i<CANDLES;i++)if(this.life[i]>0){
            this.life[i]-=I(clamp(input.blow,0,1000)*3/100);
            if(this.life[i]<=0){this.life[i]=0;this.lit--;}
          }
        }
        if(!this.lit){this.blow_ms=this.t-this.blow_t0;return this.stageEnd();}
        if(this.t>=CANDLE_LIMIT_MS)return this.stageEnd();
      }else if(this.stage===ROPE){
        if(!this.target){
          this.lock=input.voiced?this.lock+1:0;
          if(this.lock>=8)this.target=clamp(input.midi100,2400,9600);
        }else if(input.voiced){
          const d=clamp(input.midi100-this.target,-200,200);
          this.cents+=I((d-this.cents)*2/10);
          this.dev_sum+=Math.abs(this.cents);this.dev_n++;
          this.prog+=20;
          if(this.prog>=ROPE_WALK_MS)return this.stageEnd();
        }
        if(this.t>=ROPE_LIMIT_MS)return this.stageEnd();
      }else if(this.stage===FLIGHT){
        if(this.flight.state===0){
          this.hum_ms=input.voiced?this.hum_ms+20:0;
          if(this.hum_ms>=300)this.flight.start(input.midi100);
          else if(this.t>=FLIGHT_WAIT_MS)return this.stageEnd();
        }else{
          this.flight.step(20,input.midi100,input.voiced);
          if(this.flight.state===3||this.flight.state===4)return this.stageEnd();
        }
      }else{
        this.clap.step(input.clap_ms);
        if(this.clap.state===3)return this.stageEnd();
      }
      return 0;
    }
    step(dt,input){
      if(dt<=0||this.phase===TITLE||this.phase===FINAL)return 0;
      let ev=0;this.carry+=clamp(dt,0,1000);
      while(this.carry>=20){
        this.carry-=20;
        if(this.phase===PLAY){ev|=this.tick(input);if(ev)break;}
        else{
          this.t+=20;
          if(this.phase===INTRO&&this.t>=INTRO_MS){this.stageBegin();ev|=EV_PLAY;break;}
          if(this.phase===SCORE&&this.t>=SCORE_MS){ev|=this.advance();break;}
        }
      }
      if(ev)this.carry=0;
      return ev;
    }
    ok(){
      if(this.phase===INTRO){this.stageBegin();return EV_PLAY;}
      if(this.phase===SCORE)return this.advance();
      if(this.phase===PLAY)return 0;
      if(this.phase===FINAL)this.init(this.seed);
      this.stage=0;this.phase=INTRO;this.t=0;this.carry=0;return 0;
    }
    skip(){return this.phase===PLAY?this.stageEnd():0;}
    clapHit(t){return this.phase===PLAY&&this.stage===CLAP?this.clap.hit(t):0;}
    snapshot(){return{phase:this.phase,stage:this.stage,t:this.t,total:this.total,score:this.score.slice(),lit:this.lit,blow_ms:this.blow_ms,target:this.target,cents:this.cents,prog:this.prog,dev_sum:this.dev_sum,dev_n:this.dev_n,flight:[this.flight.state,this.flight.cleared,this.flight.lives,this.flight.yq],clap:[this.clap.state,this.clap.score,this.clap.hits],seed:this.seed};}
  }
  const notes=t=>t>=340?5:t>=260?4:t>=160?3:t>=80?2:1, rank=t=>t>=340?3:t>=260?2:t>=160?1:0;
  const api={Circus,notes,rank,TITLE,INTRO,PLAY,SCORE,FINAL,CANDLE,ROPE,FLIGHT,CLAP,STAGES,EV_PLAY,EV_SCORE,EV_FINAL,CANDLES,INTRO_MS,SCORE_MS,CANDLE_LIMIT_MS,ROPE_LIMIT_MS,ROPE_WALK_MS,FLIGHT_WAIT_MS,CLAP_BPM};
  if(typeof module!=='undefined')module.exports=api;root.NaboCircus=api;
})(typeof globalThis!=='undefined'?globalThis:this);
