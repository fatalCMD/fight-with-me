// Original, deterministic shell density texture. No image or asset from a fur mod is used.
// Alpha is strand height: successive shells' alpha tests leave finer tips.
const fs = require('fs'), path = require('path');
const { random } = require('./meshkit');
const size=256, rand=random(74219), density=new Float32Array(size*size);
for(let strand=0;strand<5800;++strand) {
  const x=rand()*size,y=rand()*size,height=.18+.82*rand(),length=1.5+rand()*4;
  for(let dy=-5;dy<=5;++dy) for(let dx=-2;dx<=2;++dx) {
    const value=height*Math.exp(-(dx*dx/1.1+dy*dy/(length*length)));
    const i=((Math.floor(y)+dy+size)%size)*size+(Math.floor(x)+dx+size)%size;
    density[i]=Math.max(density[i],value);
  }
}
let pixels=Buffer.alloc(size*size*4);
for(let i=0;i<density.length;++i) {
  const light=Math.round(105+75*density[i]);
  pixels.set([light,light,light,Math.round(255*density[i])],i*4);
}
const levels=[pixels];let width=size;
while(width>1) {
  const nextWidth=width/2,next=Buffer.alloc(nextWidth*nextWidth*4);
  for(let y=0;y<nextWidth;++y) for(let x=0;x<nextWidth;++x) for(let c=0;c<4;++c) {
    let sum=0;for(let j=0;j<2;++j)for(let k=0;k<2;++k)sum+=pixels[((y*2+j)*width+x*2+k)*4+c];
    next[(y*nextWidth+x)*4+c]=Math.round(sum/4);
  }
  levels.push(next);pixels=next;width=nextWidth;
}
const header=Buffer.alloc(128);header.write('DDS ');
for(const [offset,value] of [[4,124],[8,0x2100f],[12,size],[16,size],[20,size*4],[28,levels.length],
  [76,32],[80,0x41],[88,32],[92,0xff0000],[96,0xff00],[100,0xff],[104,0xff000000],[108,0x401008]]) header.writeUInt32LE(value,offset);
const dest=path.resolve(__dirname,'../../assets/textures/FightWithMe/SpectralFur.dds');
fs.writeFileSync(dest,Buffer.concat([header,...levels]));
console.log(`Original spectral fur: ${fs.statSync(dest).size} bytes, ${levels.length} mip levels`);
