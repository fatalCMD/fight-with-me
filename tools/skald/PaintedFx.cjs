// Geometry and UVs for the generated atlas. The PNG alpha remains unmodified.
const {piece,vertex,disc}=require('../fx/meshkit.js');
const nif=require('../wolfhead/nifio.js');
const smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
const uv=(cell,u,v)=>[(cell%2)*.5+.003+u*.494,Math.floor(cell/2)*.5+.003+v*.494];
function seal(radius,cell,z=0){
 const p=piece();disc(p,radius,z,r=>1-smooth((r-.84)/.16),96,16);
 for(const v of p.verts)v.uv=uv(cell,.5+v.pos[0]/(radius*2),.5-v.pos[1]/(radius*2)).map(nif.toHalf);
 return p;
}
function panel(p,cell,angle,radius,width,height,z,twist=0){
 const nx=12,ny=24,base=p.verts.length;
 for(let j=0;j<=ny;j++)for(let i=0;i<=nx;i++){
  const u=i/nx,v=j/ny,a=angle+(u-.5)*width/radius+twist*(.5-v),r=radius+3*Math.sin(v*Math.PI*2);
  const alpha=smooth(Math.min(u,1-u,v,1-v)/.13);
  p.verts.push(vertex([Math.cos(a)*r,Math.sin(a)*r,z+(1-v)*height],[Math.cos(a),Math.sin(a),0],alpha,1,uv(cell,u,v)));
 }
 for(let j=0;j<ny;j++)for(let i=0;i<nx;i++){const a=base+j*(nx+1)+i,b=a+nx+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
}
function aura(song){
 const strings=piece(),runes=piece();
 for(let j=0;j<3;j++){
  panel(strings,1,j*Math.PI*2/3,45,66,142,4,song===2?-.9:song===1?.25:.65);
  panel(runes,3,j*Math.PI*2/3+.55,69,33,47,24+j*29,.15);
 }
 return [seal(99,0),strings,runes];
}
module.exports={seal,aura};
