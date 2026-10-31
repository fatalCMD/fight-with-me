// Mirrors the runtime particle and spectral-lute poses.
const smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
export const rankGlow=rank=>rank>=3?1.6:rank===2?1:.65;
export const rankSpread=rank=>rank>=3?1.22:rank===2?1:.88;
export const bardCount=rank=>rank>=3?96:rank===2?48:24;
export const hitCount=rank=>rank>=3?64:rank===2?32:16;
export const waveCount=rank=>rank>=3?96:rank===2?48:24;
export function rankMote(mote,rank){const spread=rankSpread(rank);return {...mote,x:mote.x*spread,y:mote.y*spread,z:mote.z*(1+(spread-1)*.35),alpha:mote.alpha*rankGlow(rank),scale:mote.scale*spread};}
export function waveMote(song,i,age,rank,wave){
 if(i>=waveCount(rank)||age<0||age>=wave.reach/wave.speed)return {x:0,y:0,z:0,alpha:0,scale:1};
 const angle=i*2.399963+(song===2?-.18:.18)*age+.035*Math.sin(age*3+i),r=Math.min(wave.reach,age*wave.speed);
 const fraction=.25+i%13/12*.70+.025*Math.sin(i*3.7+age*2.5),radius=r*fraction,drift=Math.sin(i*2.1+age*2);
 const alpha=smooth(age/.06)*(1-smooth((r/wave.reach-.78)/.22))*smooth((age-i%7*.018)/.16)*(.7+.3*drift);
 return {x:Math.cos(angle)*radius,y:Math.sin(angle)*radius,z:8+age*(18+i%7*4)+12*drift*drift,alpha,scale:(1+i%4*.18)*rankSpread(rank)};
}
export function bardMote(song,i,time,beatAge){
 if(time<0)return {x:0,y:0,z:0,alpha:0,scale:1};
 const life=3.4+i%5*.27,u=(time/life+i*.618034+song*.13)%1;
 const angle=i*2.399963+time*(song===2?-.32:.25)+.3*Math.sin(time*.9+i);
 const radius=18+i%9*5+12*Math.sin(u*Math.PI),beat=beatAge<0?0:Math.exp(-beatAge*5);
 const alpha=smooth(u/.08)*(1-smooth((u-.72)/.28))*(.72+.28*Math.sin(time*2+i))*(.8+.2*beat);
 return {x:Math.cos(angle)*radius,y:Math.sin(angle)*radius,z:8+140*u+6*(1+Math.sin(u*4+i)),alpha,scale:.65+i%4*.18+.12*beat};
}
export function hitMote(song,i,age){
 const delay=i%5*.02,life=1.1+i%4*.09,u=(age-delay)/life;
 if(u<0||u>=1)return {x:0,y:0,z:0,alpha:0,scale:1};
 const angle=i*2.399963+(song===2?-1:1)*u*.8+.25*Math.sin(u*3+i),radius=10+i%5*5+15*u+5*Math.sin(u*Math.PI);
 const alpha=smooth(u/.09)*(1-smooth((u-.65)/.35))*(.8+.2*Math.sin(i+u*5));
 return {x:Math.cos(angle)*radius,y:Math.sin(angle)*radius,z:4+u*(60+i%6*18),alpha,scale:.75+i%3*.18};
}
export function luteVeil(time,accent){return {x:1.3*Math.sin(time*2.1),y:.8*Math.cos(time*1.7),z:.6*Math.sin(time*2.6),angle:.025*Math.sin(time*1.8),scale:1.035+.012*Math.sin(time*2.3),glow:.65+.12*Math.sin(time*2)+accent*.25,scroll:time*.065+.04*Math.sin(time*1.4)};}
export function moteColor(song,index,color){
 const type=index%4;
 if(song===3)return [[1,.82,.46],[1,.67,.20],[1,.14,.21],[1,.86,.58]][type];
 if(song===1&&type===2)return [.78,.96,1];
 if(song===0&&type===2)return [1,.78,.32];
 if(song===2&&type===2)return [.48,.32,.60];
 return index%3===0?color.map(c=>.38+.62*c):color;
}
