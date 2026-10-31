// Soft light, dust and diffuse wisps. No filled polygons, stars or paper-like pieces.
const {piece,sprite,ribbon,soft,FX,textured}=require('../fx/meshkit.js');
const families=[
 ['Ember glow','Heat wisps','Quick glimmers','Fine sparks'],
 ['Harmonic mist','Paired glimmers','Silver dust','Sound threads'],
 ['Violet haze','Dim embers','Shadow wisps','Drifting mist'],
 ['Gold fireflies','Warm gleams','Crimson mist','Rising glimmers']
];
function ellipse(p,x,y,sx,sy,alpha,angle=0){
 const start=p.verts.length;sprite(p,1,{alpha});
 const c=Math.cos(angle),s=Math.sin(angle);
 for(let j=start;j<p.verts.length;j++){
  const v=p.verts[j],a=v.pos[0]*sx,b=v.pos[1]*sy;v.pos=[x+a*c-b*s,y+a*s+b*c,0];
 }
}
function glimmer(p,x,y,size,alpha=.8){
 ellipse(p,x,y,size*4,size*4,alpha*.12);ellipse(p,x,y,size,size,alpha);
}
function filament(p,seed,length,bend,width,alpha){
 const points=Array.from({length:13},(_,i)=>{const f=i/12;return [Math.sin(f*4+seed)*bend,(f-.5)*length,0];});
 ribbon(p,points,f=>width*(.2+.8*Math.sin(Math.PI*f)),f=>Math.pow(Math.sin(Math.PI*f),1.5)*alpha,{flat:true,v:()=>.5});
}
function mist(p,seed,length,width,alpha){
 // Overlapping feathered glows form a soft curl with no enclosed contour.
 for(let j=0;j<7;j++){const f=j/6,e=Math.sin(Math.PI*f);
  ellipse(p,Math.sin(seed+f*4)*3,(f-.5)*length,width*(.55+.45*e),width*1.3,alpha*e,seed*.3);}
}
function dust(p,seed,count,radius,alpha){
 for(let j=0;j<count;j++){const a=seed+j*2.399963,r=radius*(.3+.7*((j*3+1)%7)/6);
  ellipse(p,Math.cos(a)*r,Math.sin(a)*r,1.6+j%3*.45,1.6+j%3*.45,alpha);}
}
function cluster(song,index){
 const p=piece(),kind=index%4,seed=index*2.399963;
 if(index%8<4){
  ellipse(p,0,0,19,19,.17);ellipse(p,0,0,3.7,3.7,.95);
  for(let j=0;j<3;j++){const a=seed+j*2.094,rad=4+j*4;ellipse(p,Math.cos(a)*rad,Math.sin(a)*rad,2.4+j*.65,2.4+j*.65,.65-j*.12);}
  ellipse(p,0,-5,1.7,13,.25);return p;
 }
 ellipse(p,0,0,16,16,.065);dust(p,seed,3,10,.28);
 if(song===0){
  if(kind===0){glimmer(p,0,2,3,.82);ellipse(p,0,-3,3,12,.24);}
  else if(kind===1){mist(p,seed,23,8,.075);filament(p,seed,20,2.5,1.4,.28);}
  else if(kind===2){glimmer(p,0,5,2.4,.8);ellipse(p,-1,-2,1.6,16,.33);}
  else{dust(p,seed+.7,5,13,.62);ellipse(p,1,1,1.3,8,.28);}
 }else if(song===1){
  if(kind===0){mist(p,seed,24,10,.065);glimmer(p,0,0,2,.45);}
  else if(kind===1){glimmer(p,-2,-4,2.8,.72);glimmer(p,2,5,2,.65);mist(p,seed,15,5,.04);}
  else if(kind===2){dust(p,seed,7,14,.52);glimmer(p,1,0,1.5,.72);}
  else{filament(p,seed,27,3,1.3,.42);mist(p,seed,24,6,.035);}
 }else if(song===2){
  if(kind===0){mist(p,seed,22,11,.06);ellipse(p,0,0,4,8,.18);}
  else if(kind===1){dust(p,seed,6,12,.32);glimmer(p,0,-2,2,.45);}
  else if(kind===2){filament(p,seed,25,4,2.8,.19);mist(p,seed,21,7,.05);}
  else{mist(p,seed,18,13,.05);dust(p,seed+.4,4,9,.22);}
 }else{
  if(kind===0){glimmer(p,0,0,2.9,.9);dust(p,seed,3,9,.48);}
  else if(kind===1){ellipse(p,0,0,2,14,.38);glimmer(p,0,0,2.5,.7);mist(p,seed,16,5,.04);}
  else if(kind===2){mist(p,seed,23,10,.065);glimmer(p,0,1,1.7,.44);}
  else{filament(p,seed,22,2,1.1,.35);glimmer(p,Math.sin(seed+4)*2,10,2,.72);}
 }
 const a=seed*.27,c=Math.cos(a),s=Math.sin(a);
 for(const v of p.verts){const [x,y]=v.pos;v.pos[0]=x*c-y*s;v.pos[1]=x*s+y*c;}
 return p;
}
function mote(w,prefix,index,song){
 const body=w.shape(prefix+'Glow'+index,cluster(song,index),textured(soft(song===2?2.9:3.5),FX.glowSoft));
 const family=index%8<4?'SpiritDust':families[song][index%4].replaceAll(' ','');
 const type=w.node('NiNode',family+index,[body]);
 return w.node('NiNode',prefix+index,[w.billboard(prefix+'Facing'+index,[type])]);
}
module.exports={families,cluster,mote};
