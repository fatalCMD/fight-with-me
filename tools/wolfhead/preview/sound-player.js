// The native cue sheet, with the same bounded envelopes and quiet persistent beds.
export class SoundPlayer {
  constructor(data, container, toggle, status) {
    this.data=data;this.container=container;this.toggle=toggle;this.status=status;this.voices=[];this.life=0;this.generation=0;
    toggle.onchange=()=>{this.reset();status.textContent='';};
    document.addEventListener('visibilitychange',()=>{if(document.hidden)this.pause();});
  }
  pause(){for(const v of this.voices)v.audio.pause();}
  reset(){this.generation++;for(const v of this.voices){v.audio.pause();v.audio.currentTime=0;v.done=v.blocked=v.pending=false;}}
  unlock(){for(const v of this.voices)v.blocked=false;}
  load(cues,life){
    this.reset();this.life=life;this.container.replaceChildren();this.status.textContent='';
    this.voices=cues.map(cue=>{
      const source=this.data.assets[cue.form];
      if(!source)throw Error(`Missing preview sound ${cue.form.toString(16)}`);
      const audio=new Audio('out/'+source.file);audio.preload='auto';audio.loop=!!cue.loop;
      audio.dataset.pitch=String(cue.pitch??1);audio.dataset.form=String(cue.form);audio.dataset.sound=source.name;audio.dataset.loop=String(!!cue.loop);
      this.container.append(audio);return{audio,cue,done:false,blocked:false,pending:false};
    });
  }
  update(t,playing,rate){
    for(const v of this.voices){
      const {audio,cue}=v,start=cue.at<0?Math.max(0,this.life+cue.at):cue.at;
      const end=Math.min(this.life,cue.seconds>0?start+cue.seconds:this.life),age=t-start,left=end-t;
      if(!this.toggle.checked||!playing||document.hidden){audio.pause();continue;}
      if(age<0)continue;
      if(left<=0||(!cue.loop&&audio.ended)){audio.pause();v.done=true;continue;}
      const fadeIn=cue.loop?Math.min(1,age/(cue.channel?.1:.8)):1,fadeOut=Math.min(1,left/(cue.channel?.25:cue.loop?2.4:.2));
      audio.volume=Math.max(0,Math.min(1,cue.volume*fadeIn*fadeOut));audio.preservesPitch=(cue.pitch??1)===1;audio.playbackRate=rate*(cue.pitch??1);
      if(audio.paused&&!v.done&&!v.blocked&&!v.pending){
        if(Number.isFinite(audio.duration)&&audio.duration>0){
          if(!cue.loop&&age*(cue.pitch??1)>=audio.duration){v.done=true;continue;}
          const offset=age*(cue.pitch??1);audio.currentTime=cue.loop?offset%audio.duration:Math.min(offset,audio.duration);
        }
        v.pending=true;const generation=this.generation;
        audio.play().then(()=>{if(generation===this.generation)this.status.textContent='';}).catch(error=>{
          if(generation===this.generation&&error.name!=='AbortError'){
            v.blocked=true;this.status.textContent=error.name==='NotAllowedError'?'Click Restart to hear sound':'Sound unavailable';
          }
        }).finally(()=>{if(generation===this.generation)v.pending=false;});
      }
    }
  }
}
