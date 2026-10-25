// Stock Skyrim bandit geometry, baked into a spectral archer; original wind ribbons and motes.
const path=require('path');
const {piece,band,ribbon,sprite,meshWriter,FX}=require('../fx/meshkit');
const {archer}=require('./StockArcher');
for(const female of [false,true]){
const m=meshWriter(),children=[];
function hidden(i,parts){return m.addBlock({type:'NiNode',name:m.str('Part'+String(i).padStart(2,'0')),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children:parts,effects:[]});}
const stock=archer(female);
for(let i=0;i<3;i++){
 const shapes=stock.map((s,k)=>m.shape('Echo'+i+'_'+k+'_'+s.label,s.p,{texture:s.texture,scale:2.8,soft:6,doubleSided:s.label==='NockedArrow',rim:[1,0,.6,1]}));
 children.push(hidden(i,shapes));
}
for(let i=3;i<15;i++){
 const p=piece(),pts=Array.from({length:97},(_,j)=>{const t=j/96,a=t*Math.PI*1.6;return[Math.cos(a)*32,Math.sin(a)*32,12*t+4*Math.sin(t*6)];});
 ribbon(p,pts,7,.6,{vScale:120});children.push(hidden(i,[m.shape('AirSweep'+i,p,{texture:FX.cloud,scale:1.7,soft:12})]));
}
for(let i=15;i<51;i++){const p=piece();sprite(p,4.5);children.push(hidden(i,[m.billboard('MoteFacing'+i,[m.shape('Mote'+i,p,{texture:FX.glowSpot,scale:1.2,soft:4})])]));}
const gust=piece();band(gust,22,52,0,[0,.18,.65,.3,0],192);children.push(hidden(51,[m.shape('ScatteringGust',gust,{texture:FX.cloud,scale:2,soft:18})]));
const root=m.node('BSFadeNode','Windstep',children);
for(let i=0;i<8;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/Windstep'+(female?'Female':'')+(i?i+1:'')+'.nif'),root);
}
