// Mirrors DrawPathAtmosphere: decorative, clipped behind every interactive node.
(() => {
  const reduce=matchMedia('(prefers-reduced-motion: reduce)');
  let last=0;
  const mist=document.createElement('canvas');mist.width=193;mist.height=81;
  const mc=mist.getContext('2d'),pixels=mc.createImageData(mist.width,mist.height);
  const smooth=v=>{v=Math.max(0,Math.min(1,v));return v*v*(3-2*v);};
  function frame(now){
    requestAnimationFrame(frame);
    if(document.hidden||now-last<32)return;
    last=now;
    const canvas=document.getElementById('atmosphere-canvas');if(!canvas)return;
    const w=+canvas.dataset.width,h=+canvas.dataset.height,theme=canvas.dataset.theme;
    const bounds=canvas.getBoundingClientRect(),density=Math.min(devicePixelRatio||1,2);
    const pw=Math.max(1,Math.ceil(bounds.width*density)),ph=Math.max(1,Math.ceil(bounds.height*density));
    if(canvas.width!==pw||canvas.height!==ph){canvas.width=pw;canvas.height=ph;}
    const c=canvas.getContext('2d');c.setTransform(pw/w,0,0,ph/h,0,0);c.clearRect(0,0,w,h);
    const t=reduce.matches?12:now/1000,ember=theme==='oxblood',snow=theme==='nordic';
    c.save();c.beginPath();c.rect(18,119,w-36,h-302);c.clip();
    const disk=(x,y,r,color,a)=>{c.globalAlpha=a;c.fillStyle=color;c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.fill();};
    const line=(x,y,xx,yy,color,a,width=.85)=>{c.globalAlpha=a;c.strokeStyle=color;c.lineWidth=width;c.beginPath();c.moveTo(x,y);c.lineTo(xx,yy);c.stroke();};
    for(let i=0;i<180;i++){
      const x=24+(i*173.13)%(w-48),y=124+(i*97.71)%(h-310),weight=.25+.75*Math.abs(2*x/w-1),len=ember?12+i%23:3+i%9;
      line(x,y,x+len,y+(ember?-len*.48:snow?1.3:-.7),ember?'#9b4e3b':snow?'#a2977d':'#afafaf',weight*(ember?.07:.035),i%3===0?1.3:.65);
    }
    // A continuous soft density field replaces broad stacked strokes.
    const mh=h-302,color=ember?[126,46,36]:snow?[111,134,121]:[172,172,172];
    for(let iy=0;iy<mist.height;iy++)for(let ix=0;ix<mist.width;ix++){
      const x=w*ix/(mist.width-1),y=119+mh*iy/(mist.height-1),u=x/w;let value=0;
      for(let ribbon=0;ribbon<(snow?3:2);ribbon++){
        let distance,spread,along;
        if(ember){const v=(y-142)/(h-330);distance=x-(w*(ribbon===0?.08:.90)+42*Math.sin(v*7+t*.13+ribbon));spread=55;along=Math.sin(Math.max(0,Math.min(1,v))*Math.PI)**2;}
        else{const centre=snow?180+ribbon*62+42*Math.sin(u*6+t*.11+ribbon*1.4)+12*Math.sin(u*12-t*.15):310+ribbon*220+58*Math.sin(u*4+t*.07+ribbon*2);distance=y-centre;spread=snow?36+12*Math.sin(u*5+t*.09+ribbon):72+20*Math.sin(u*4+t*.08+ribbon);along=Math.sin(u*Math.PI)**2;}
        value+=Math.exp(-.5*distance*distance/(spread*spread))*along*(snow?.065:ember?.075:.055);
      }
      const k=(iy*mist.width+ix)*4;pixels.data[k]=color[0];pixels.data[k+1]=color[1];pixels.data[k+2]=color[2];pixels.data[k+3]=255*value*smooth((y-119)/48)*smooth((119+mh-y)/64);
    }
    mc.putImageData(pixels,0,0);c.globalAlpha=1;c.imageSmoothingEnabled=true;c.imageSmoothingQuality='high';c.drawImage(mist,0,119,w,mh);
    const count=snow?100:ember?108:64,travel=Math.max(h-280,1);
    for(let i=0;i<count;i++){
      let hash=(Math.imul(i+1,747796405)+2891336453)>>>0;hash=Math.imul((hash>>>((hash>>>28)+4))^hash,277803737)>>>0;hash=((hash>>>22)^hash)>>>0;
      const depth=(i%5)/4,period=(ember?9:snow?24:30)+(i%9)*(snow?1.2:1.7),u=(t/period+(hash&65535)/65536)%1;
      const anchor=(hash>>>16)/65536*Math.max(w-50,1),drift=snow?65*u+26*Math.sin(t*.25+i):(ember?34:24)*Math.sin(t*(ember?.65:.18)+i);
      const x=!snow&&!ember?25+u*(w-50):25+(anchor+drift+w)%(w-50),y=!snow&&!ember?130+(hash>>>16)/65536*(travel-20)+14*Math.sin(t*.15+i):120+travel*(ember?1-u:u),life=Math.sin(u*Math.PI),flicker=ember?.72+.28*Math.sin(t*(3+i%4)+i):1;
      const a=life*flicker*(.32+.42*depth),size=snow?1.1+1.9*depth:ember?.9+1.8*depth:.55+.6*depth,ash=ember&&i%4===0;
      const color=snow?'#d5caad':ember?(ash?'#988079':i%3?'#cf472b':'#efae6e'):'#e6e6e6';
      if(!snow&&!ember){disk(x,y,size*3.5,color,a*.04);disk(x,y,size,color,a*.50);if(i%6===0)line(x-7-depth*5,y,x,y,color,a*.12,.65);continue;}
      if(ember&&!ash)for(let halo=3;halo>0;halo--)disk(x,y,size*(1+halo*1.3),color,a*.025);
      const turn=t*(snow?.22:.1)+i,dx=Math.cos(turn)*size,dy=Math.sin(turn)*size;
      c.globalAlpha=a*(snow?.65:ash?.5:.85);c.fillStyle=color;c.beginPath();c.moveTo(x-dx,y-dy);c.lineTo(x+dy*.7,y-dx*.7);c.lineTo(x+dx*1.8,y+dy*1.8);c.lineTo(x-dy*.5,y+dx*.5);c.closePath();c.fill();
      if(ember&&!ash)line(x,y,x-3,y+8+depth*7,color,a*.28,size*.6);
      else if(!snow&&!ember&&i%9===0)disk(x,y,size*2.8,color,a*.055);
    }
    c.restore();canvas.dataset.particles=String(count);canvas.dataset.frame=String(reduce.matches?0:Math.floor(now));
  }
  requestAnimationFrame(frame);
})();
