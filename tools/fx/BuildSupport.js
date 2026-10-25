// Original supporting art. No external effect meshes are read or copied.
// Skyrim supplies particle textures; Fight With Me supplies every vertex and animation.
const path = require('path');
const { piece, vertex, ribbon, band, sprite, meshWriter, FX, random } = require('./meshkit');
const out = path.resolve(__dirname, '../../assets/meshes/FightWithMe/Support');
const families = ['Guard', 'Vitality', 'Focus', 'Mark', 'Binding', 'Surge', 'Rally', 'Wind', 'Impact', 'LevelUp'];
function strand(p, points, width, strength = .7) {
  ribbon(p, points, width, f => strength * Math.pow(Math.sin(Math.PI * f), .8));
}
function arc(p, radius, z, start, span, width, strength = .7) {
  strand(p, Array.from({length: 65}, (_, i) => {
    const a = start + span * i / 64;
    return [Math.cos(a) * radius, Math.sin(a) * radius, z];
  }), width, strength);
}
for (const family of families) {
  const m = meshWriter(), core = piece(), wisps = piece(), motes = [];
  const rand = random(301 + families.indexOf(family));
  if (family === 'Guard') {
    // Open protective ribs, leaving the character readable between them.
    for (let k = 0; k < 5; ++k) {
      const a = k * Math.PI * 2 / 5;
      strand(core, Array.from({length: 49}, (_, i) => {
        const f = i / 48, r = 30 + 9 * Math.sin(f * Math.PI);
        return [Math.cos(a) * r, Math.sin(a) * r, 30 + f * 103];
      }), 5, .6);
    }
    arc(wisps, 35, 77, .2, Math.PI * 1.5, 9, .25);
  } else if (family === 'Focus' || family === 'Mark') {
    for (let k = 0; k < 4; ++k) arc(core, family === 'Mark' ? 35 : 28, family === 'Mark' ? 100 : 88,
      k * Math.PI / 2 + .18, .55, 4, .65);
    arc(wisps, 31, 90, .6, Math.PI * 1.2, 7, .18);
  } else if (family === 'Binding') {
    // Two restrained, broken coils around the legs rather than another floating rune.
    for (let k = 0; k < 2; ++k) strand(core, Array.from({length: 97}, (_, i) => {
      const f = i / 96, a = f * Math.PI * 3 + k * Math.PI;
      return [Math.cos(a) * 29, Math.sin(a) * 29, 10 + f * 46];
    }), 3, .65);
    arc(wisps, 33, 12, .1, Math.PI * 1.8, 10, .25);
  } else if (family === 'Wind') {
    band(core, 38, 58, 5, [0, .25, .65, .25, 0]);
    for (let k = 0; k < 3; ++k) arc(wisps, 50 + k * 9, 10 + k * 7, k * 2, 2.6, 13, .4);
  } else if (family === 'Impact') {
    for (let k = 0; k < 12; ++k) {
      const a = k * Math.PI / 6, z = 75 + 20 * Math.sin(a * 3);
      strand(core, [[Math.cos(a)*12,Math.sin(a)*12,z], [Math.cos(a)*40,Math.sin(a)*40,z+12]], 3);
    }
    arc(wisps, 32, 85, 0, Math.PI * 1.9, 12, .3);
  } else {
    const surge = family === 'Surge', count = surge ? 5 : 3;
    for (let k = 0; k < count; ++k) strand(core, Array.from({length: 65}, (_, i) => {
      const f = i / 64, a = k * Math.PI * 2 / count + f * (surge ? 4.2 : 1.8);
      const r = 24 + 8 * Math.sin(f * Math.PI);
      return [Math.cos(a)*r, Math.sin(a)*r, 17 + f * 120];
    }), surge ? 6 : 8, .4);
    for (let k = 0; k < 3; ++k) arc(wisps, 29+k*3, 35+k*30, k*1.9, 2.5, 12, .22);
  }
  const coreTexture = family === 'Surge' ? FX.swirlStrip : FX.glowSoft;
  const nodes = [m.node('NiNode', 'FWM_Core', [m.shape('Contour', core, {scale: 1.1, texture: coreTexture, soft: 12})]),
    m.node('NiNode', 'FWM_Wisps', [m.shape('Wisps', wisps, {scale: .85, texture: FX.frostCloud, soft: 14})])];
  for (let k = 0; k < (family === 'LevelUp' ? 26 : 12); ++k) {
    const p = piece(); sprite(p, 1.8 + rand()*2.3, {alpha: .6});
    const a = rand()*Math.PI*2, r = 25+rand()*12;
    const b = m.billboard('MoteFacing'+k,[m.shape('Mote'+k,p,{scale: .8,texture: FX.glowSpot,soft: 6})]);
    motes.push(m.addBlock({type:'NiNode',name:m.str('MoteAt'+k),extra:[],controller:-1,flags:0x8000e,
      translate:[Math.cos(a)*r,Math.sin(a)*r,family==='Binding'?15+rand()*35:22+rand()*110],
      rotate:[1,0,0,0,1,0,0,0,1],scale:1,collision:-1,children:[b],effects:[]}));
  }
  nodes.push(m.node('NiNode','FWM_Motes',motes));
  m.write(path.join(out,family+'.nif'),m.node('BSFadeNode','FWM_Support',nodes));
}
// Weapon-local components follow the blade. Physical strikes use steel glints;
// the wolf uses spirit vapour, while Berserk's established fire theme uses actual flame sheets.
const weapons = {Sword:[72,5],Dagger:[35,3],WarAxe:[55,9],Mace:[59,9],Greatsword:[108,7],Battleaxe:[94,13],Warhammer:[94,13]};
for (const element of ['Fire','Steel','Spirit']) {
  for (const [shape,[length,width]] of Object.entries(weapons)) {
    const m=meshWriter(), p=piece(), haze=piece(), particles=[];
    if (element==='Fire') {
      // Broad overlapping flame sheets with the full flame texture across each face.
      // Rise away from the blade, instead of tapering into radial wire-like spikes.
      for(let k=0;k<14;++k) {
        const along=10+(k/14)*length, height=18+(k%4)*5, span=15+width;
        const base=p.verts.length, rows=12, cols=8, angle=(k%3)*Math.PI/3;
        for(let j=0;j<=rows;++j)for(let i=0;i<=cols;++i){
          const f=j/rows,u=i/cols,side=(u-.5)*span*(1-.25*f);
          const bend=Math.sin(f*4+k*1.7)*f*4;
          const v=vertex([Math.sin(angle)*side+bend,along+Math.cos(angle)*side,1+f*height],
            [Math.cos(angle),-Math.sin(angle),0],Math.pow(Math.sin(Math.PI*u),.6)*Math.min(1,f*6)*Math.pow(1-f,.7)*.85,1,[u,1-f]);
          v.c[1]=Math.round(255*(1-.6*f));v.c[2]=Math.round(255*(1-.88*f));p.verts.push(v);
        }
        for(let j=0;j<rows;++j)for(let i=0;i<cols;++i){const a=base+j*(cols+1)+i,b=a+cols+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
      }
      // The ember cloud is discrete and has individual motion/fades in SupportArt::Particle.
      for(let k=0;k<24;++k) {
        const puff=k>=12, spark=piece();sprite(spark,puff?18+width:1.4+(k%3)*.4,{alpha:puff?.65:.8,cols:puff?2:1,rows:puff?2:1,cell:k%4});
        if(puff)for(const v of spark.verts){v.c[1]=190;v.c[2]=80;}
        const b=m.billboard('MoteFacing'+k,[m.shape((puff?'FlamePuff':'Ember')+k,spark,{scale:puff?1.8:2.5,texture:puff?FX.fireball:FX.spark,soft:3})]);
        particles.push(m.addBlock({type:'NiNode',name:m.str('MoteAt'+k),extra:[],controller:-1,flags:0x8000e,
          translate:[Math.sin(k*2.4)*width*.4,12+((k%12)/12)*length,puff?4:2],rotate:[1,0,0,0,1,0,0,0,1],scale:1,collision:-1,children:[b],effects:[]}));
      }
    } else if (element==='Steel') {
      for(let k=0;k<3;++k)strand(p,[[width*(k-1)*.5,12,0],[width*(k-1)*.5,length*.55,0],[width*(k-1)*.3,length+8,0]],1.6,.8);
      for(let k=0;k<6;++k) {
        const along=15+k*length/7,sign=k%2?1:-1;
        strand(haze,[[0,along,0],[sign*(width+3),along+6,0]],1.8,.6);
      }
    } else {
      for(let k=0;k<3;++k)strand(p,Array.from({length:33},(_,i)=>{
        const f=i/32,a=k*Math.PI*2/3+f*2;
        return [Math.cos(a)*width*.6,9+f*length,Math.sin(a)*width*.6];
      }),width*1.4,.5);
    }
    if(element!=='Steel')strand(haze,[[0,12,0],[0,length*.5,0],[0,length+8,0]],width*2.4,.3);
    const texture=element==='Fire'?FX.fireWave:(element==='Steel'?FX.spark:FX.swirlStrip);
    const nodes=[m.node('NiNode','FWM_Core',[m.shape(element==='Fire'?'FlameTongues':element==='Steel'?'SteelEdges':'SpiritTrails',p,{scale:element==='Fire'?2.2:1.3,texture,soft:5})]),
      m.node('NiNode','FWM_Wisps',[m.shape(element==='Fire'?'FlameBed':'WeaponWisps',haze,{scale:element==='Fire'?1.5:.85,texture:element==='Fire'?FX.fireTile:element==='Steel'?FX.spark:FX.cloud,soft:5})])];
    if(particles.length)nodes.push(m.node('NiNode','FWM_Motes',particles));
    m.write(path.join(out,element+shape+'.nif'),m.node('BSFadeNode','FWM_Weapon',nodes));
  }
}
