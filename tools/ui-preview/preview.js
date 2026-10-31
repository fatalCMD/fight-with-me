(async () => {
const stage = document.querySelector('#stage');
const query = new URLSearchParams(location.search);
let mode = query.get('view') || 'overview', role = Math.max(0,Math.min(6,Number(query.get('role')??1)||0)), chosen = Math.max(0,Math.min(14,Number(query.get('node')??3)||0)), picker = false;
const palettes = {
  vanilla: { bg:'#050505', inset:'#151515', edge:'#eeeeee', line:'#dedede58', text:'#f2f2f2', dim:'#b9b9b9', faint:'#858585', accent:'#4b4b4b' },
  oxblood: { bg:'#0f0f10', inset:'#160c0c', edge:'#d4423a', line:'#ffffff36', text:'#ece8e2', dim:'#aaa6a0', faint:'#6c6a66', accent:'#9e2422' },
  nordic: { bg:'#0f1516', inset:'#151d1e', edge:'#cfd6c3', line:'#9ba6a094', text:'#ecebdd', dim:'#b7bfb3', faint:'#808c86', accent:'#424c48' },
};
const symbols = { 0xef00:'wolf-head', 0xef01:'barbed-sun', 0xef02:'weak-will', 0xef03:'last-bastion' };
for(let code=0xef10;code<=0xef1e;code++)symbols[code]='glyphs/'+code.toString(16);
const escape = x => String(x ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const clamp = (v,a,b) => Math.max(a, Math.min(b,v));
const data = await fetch('/metadata.json').then(r => r.json());
await Promise.all(['Skyrim','Icons','MasterSilver','MasterGothic','MasterStone','ThemeSilver','ThemeGothic','ThemeStone'].map(face=>document.fonts.load('24px '+face)));
const faces={vanilla:'MasterSilver',oxblood:'MasterGothic',nordic:'MasterStone'};const bodyFaces=faces;let activeFace='MasterSilver';
function withMaster(rank,draw){const old=activeFace;activeFace=rank>=3?faces[theme]:bodyFaces[theme];draw();activeFace=old;}
const measure = document.createElement('canvas').getContext('2d');
const width = (text,size) => { measure.font=`${size*1.2}px ${activeFace}`; return measure.measureText(text).width; };
function arcText(x,y,size,radius,label,lower,color){
  const chars=Array.from(label),tracking=size*.065,total=chars.reduce((sum,c)=>sum+width(c,size),0)+Math.max(0,chars.length-1)*tracking,fit=Math.min(1,radius*2.35/Math.max(total,1));
  size*=fit;let cursor=-total*fit*.5;
  for(const c of chars){const advance=width(c,size),angle=(cursor+advance*.5)/radius;
    out+=`<g transform="translate(${x+radius*Math.sin(angle)} ${y+(lower?1:-1)*radius*Math.cos(angle)}) rotate(${(lower?-angle:angle)*180/Math.PI})">`;
    text(0,0,size,c,color,'middle');out+='</g>';cursor+=advance+tracking*fit;
  }
}
function abilityName(x,y,scale,label,color){
  let split=-1,best=Infinity;
  for(let i=label.indexOf(' ');i!==-1;i=label.indexOf(' ',i+1)){const diff=Math.abs(width(label.slice(0,i),22)-width(label.slice(i+1),22));if(diff<best){best=diff;split=i;}}
  out+=`<g class="ability-name" aria-hidden="true" data-label="${escape(label)}">`;
  arcText(x,y,22*scale,87*scale,split<0?label:label.slice(0,split),false,color);
  if(split>=0)arcText(x,y,22*scale,94*scale,label.slice(split+1),true,color);
  out+='</g>';
}
function wrap(text,size,maxWidth) {
  const lines=[]; let line='';
  for (const word of text.split(/\s+/)) { const next=line ? `${line} ${word}` : word; if(line && width(next,size)>maxWidth){lines.push(line);line=word;}else line=next; }
  if(line)lines.push(line);return lines;
}
let out='',p,theme='vanilla',previousRole=-1;
const learned=new Map(),equipped=new Map();let reveal=-1,debugMode=false,debugSaved=null;
const rankCost=(n,rank)=>debugMode?0:n.kind==='ability'?rank:1;
const requiredLevel=(n,rank)=>n.kind==='trait'?([0,0,25,50][rank]||0):([0,35,50][n.tier]||0);
const followerLevel=()=>+document.getElementById('level').value;
const poses=new Map();
function rect(x,y,w,h,fill,stroke='none',sw=1){
  if(w<12||h<8||(x===0&&y===0&&w>800)||theme==='vanilla'){
    out+=`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${x===0&&y===0?0:Math.min(10,h*.35)}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;return;
  }
  const c=Math.min(14,w*.24,h*.24),q=theme==='nordic'?c:c*.3;
  out+=`<path d="M${x+c} ${y}H${x+w-c}L${x+w} ${y+q}V${y+h-c}L${x+w-q} ${y+h}H${x+q}L${x} ${y+h-c}V${y+q}Z" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;
}
function line(x,y,x2,y2,color=p.line,sw=1){out+=`<path d="M${x} ${y}L${x2} ${y2}" stroke="${color}" stroke-width="${sw}"/>`;}
function circle(x,y,r,fill,stroke='none',sw=1){out+=`<circle cx="${x}" cy="${y}" r="${r}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;}
function diamond(x,y,r,fill,stroke=p.edge,sw=1,cls=''){out+=`<path class="${cls}" d="M${x} ${y-r}L${x+r} ${y}L${x} ${y+r}L${x-r} ${y}Z" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;}
function text(x,y,size,label,color=p.text,anchor='start',tracking=0){out+=`<text style="font-family:${activeFace}" x="${x}" y="${y}" font-size="${size*1.2}" fill="${color}" text-anchor="${anchor}" letter-spacing="${tracking}">${escape(label)}</text>`;}
function top(x,y,size,label,color=p.text,tracking=0){text(x,y+size*.6,size,label,color,'start',tracking);}
function glyph(x,y,size,code,color=p.text){if(symbols[code])out+=`<image x="${x-size*.58}" y="${y-size*.58}" width="${size*1.16}" height="${size*1.16}" href="/${symbols[code]}.svg"/>`;else out+=`<text class="icon" x="${x}" y="${y}" text-anchor="middle" font-size="${size}" fill="${color}">${String.fromCodePoint(code || 0xf111)}</text>`;}
function para(x,y,size,str,maxWidth,color=p.dim){const lines=wrap(str,size,maxWidth);lines.forEach((t,i)=>top(x,y+i*size*1.34,size,t,color));return lines.length*size*1.34;}
function emblem(x,y,r,fill,stroke=p.edge,sw=1,kind=0,backlight=false){
  if(theme==='vanilla'&&kind!==2){circle(x,y,r,fill,stroke,sw);return;}
  const n=theme==='nordic'?(kind===1?8:6):(kind===1?6:kind===2?8:4);
  const pts=Array.from({length:n},(_,i)=>{const a=-Math.PI/2+Math.PI*2*i/n;return [x+Math.cos(a)*r,y+Math.sin(a)*r].join(',');}).join(' ');
  out+=`<polygon class="${backlight?'backlight-fill':'node-rim'}" points="${pts}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;
}
function sealPoints(x,y,r){
  if(theme==='oxblood')return [[0,-1.14],[.70,-.78],[.96,-.20],[.73,.62],[0,1.16],[-.73,.62],[-.96,-.20],[-.70,-.78]].map(([a,b])=>[x+a*r,y+b*r]);
  const count=theme==='nordic'?6:48;
  return Array.from({length:count},(_,i)=>{const a=-Math.PI/2+Math.PI*2*i/count;return [x+Math.cos(a)*r,y+Math.sin(a)*r];});
}
function nodeContour(x,y,r,kind,master){
 if(master)return sealPoints(x,y,r);
 const count=theme==='vanilla'&&kind!==2?48:theme==='nordic'?(kind===1?8:6):(kind===1?6:kind===2?8:4);
 return Array.from({length:count},(_,i)=>{const a=-Math.PI/2+i*Math.PI*2/count;return [x+Math.cos(a)*r,y+Math.sin(a)*r];});
}
function poly(points,fill,stroke='none',sw=1){out+=`<polygon points="${points.map(p=>p.join(',')).join(' ')}" fill="${fill}" stroke="${stroke}" stroke-width="${sw}"/>`;}
function blend(a,b,t){const aa=a.match(/\w\w/g).map(x=>parseInt(x,16)),bb=b.match(/\w\w/g).map(x=>parseInt(x,16));return '#'+aa.map((v,i)=>Math.round(v+(bb[i]-v)*t).toString(16).padStart(2,'0')).join('');}

function mastery(x,y,r,numeral=true){
  out+='<g class="mastery" aria-label="Rank III '+theme+' seal">';
  const base=theme==='nordic'?'#303935':theme==='vanilla'?'#242424':'#420c14',dust=theme==='nordic'?'#7a8f80':theme==='vanilla'?'#dadada':'#ab2735';
  const outer=sealPoints(x,y,r*1.12),inner=sealPoints(x,y,r*.87);
  for(let i=0;i<outer.length;i++){const k=(i+1)%outer.length;poly([outer[i],outer[k],inner[k],inner[i]],base,base,.5);}
  for(let band=0;band<7;band++){const v=band/6;poly(sealPoints(x,y,r*(.9+.19*v)),'none',dust+Math.round(255*(.025+.065*Math.sin(v*Math.PI))).toString(16).padStart(2,'0'),r*.045);}
  for(let i=0;i<34;i++){
    const side=(i*13)%outer.length,u=.12+.76*((i*17)%23)/22,a=outer[side],b=outer[(side+1)%outer.length],q=.82+.14*(i%5)/4,px=x+(a[0]+(b[0]-a[0])*u-x)*q,py=y+(a[1]+(b[1]-a[1])*u-y)*q;
    if(theme==='nordic')line(px,py,px+r*(.035+.018*(i%3)),py-r*.018,(i%3?dust:'#000000')+'38',Math.max(.6,r*.016));
    else if(theme==='vanilla')circle(px,py,r*(i%9===0?.027:.011),dust+(i%9===0?'cc':'59'));
    else line(px,py,px+r*.025,py+r*(.035+.012*(i%4)),(i%3?dust:'#000000')+'33',Math.max(.6,r*.02));
  }
  if(theme==='nordic')for(let i=-1;i<=1;i++){const px=x+i*r*.19,py=y-r*.91;line(px,py-r*.085,px,py+r*.065,'#000000e6',2.5);line(px,py-r*.06,px+r*.065,py,dust+'a6');}
  else if(theme==='oxblood'){const py=y-r*1.04;poly([[x-r*.09,py+r*.06],[x,py-r*.1],[x+r*.09,py+r*.06]],'none',dust+'cc');line(x,py-r*.14,x,py+r*.12,dust+'b3');}
  if(numeral){rect(x-r*.40,y+r*.70,r*.80,r*.42,base);withMaster(3,()=>text(x,y+r*.91,r*.32,'III',p.text,'middle'));}
  out+='</g>';
}
function selection(x,y,r,n,master){
  const tone=theme==='vanilla'?'#dedede':theme==='nordic'?'#87c0a4':'#e12f42';
  out+='<g class="selection-glow">';
  for(let i=0;i<24;i++){const v=i/23,radius=r*(1.68-.72*v),fill=tone+Math.round(255*(.006+.035*v*v)).toString(16).padStart(2,'0');
    if(master)poly(sealPoints(x,y,radius),fill);
    else emblem(x,y,radius,fill,'none',0,n.kind==='branch'?1:n.kind==='trait'?2:0,true);
  }
  out+='</g>';
}
function node(x,y,r,n,rank,selected,locked,buyable,scale=1,withPips=true){
  const color=rank?p.edge:buyable?p.dim:p.line,master=n.kind==='ability'&&rank>=3;
  if(selected)selection(x,y,r,n,master);
  if(rank)circle(x,y,r*1.65,p.accent+'0a');
  if(master){poly(sealPoints(x,y,r*.95),'#131314');poly(sealPoints(x,y,r*.84),p.accent+'60');}
  else if(n.kind==='ability'){
    emblem(x,y,r+6*scale,'none',p.line);emblem(x,y,r,'#131314',color,1.4);
    if(rank)emblem(x,y,r*.84,p.accent+'4d','none');
    for(const [dx,dy]of[[0,-1],[1,0],[0,1],[-1,0]])diamond(x+dx*(r+6*scale),y+dy*(r+6*scale),2.6*scale,p.edge,'none');
  }else if(n.kind==='branch'){
    emblem(x,y,r+4*scale,'none',p.line,1,1);emblem(x,y,r,'#131314',color,1.4,1);if(rank)emblem(x,y,r*.84,p.accent+'4d','none',1,1);
  }else{
    emblem(x,y,r,rank?p.accent:'#131314',color,1,2);
  }
  glyph(x,y,r*(n.kind==='ability'?.62:.66),n.glyph,locked?p.faint:rank?p.text:p.dim);
  if(!rank){const kind=n.kind==='branch'?1:n.kind==='trait'?2:0;poly(nodeContour(x,y,r+(kind===0?6:kind===1?4:5)*scale,kind,master),locked?'#000000b8':'#0000005c');}

  if(locked){circle(x+r*.72,y+r*.72,8*scale,'#101012',p.line);glyph(x+r*.72,y+r*.72,8.5*scale,0xf023,p.dim);}
  if(master)mastery(x,y,r);
  if(withPips){const kind=n.kind==='branch'?1:n.kind==='trait'?2:0,rim=master?r*1.12:r+(kind===0?6:kind===1?4:5)*scale,contour=nodeContour(x,y,rim,kind,master),step=Math.floor(contour.length/3),corners=[contour.length-step,0,step];
    for(let i=0;i<n.most;i++){const point=contour[n.most===1?0:n.most===2?corners[i*2]:corners[i%3]],on=i<rank;out+='<g class="rank-marker" aria-label="Rank '+(i+1)+(on?' learned':' unlearned')+'">';circle(point[0],point[1],10.5*scale,'#0f0f11');diamond(point[0],point[1],9*scale,on?p.edge:'#1a1a1c',on?p.text:buyable&&i===rank?p.edge:p.line,scale);out+='</g>';}
  }
}
const demoPools=[new Set([0,1]),new Set([0,2]),new Set([0,1])];
let poolTab=0,poolOpen=true,poolCursor=1;
const poolChoices=[['Healing','Fast Healing','Lesser Ward','Oakflesh','Firebolt'],['Firebolt','Flames','Frostbite','Sparks','Healing'],['Steel Sword','Steel Greatsword','Hunting Bow','Staff of Firebolts','Steel Shield']];
const poolLabels=['LEFT SPELL POOL','RIGHT SPELL POOL','WEAPON POOL'];
function drawLoadouts(){
  stage.setAttribute('viewBox','30 145 1340 740');
  text(70,75,24,'YOUR PARTY',p.text,'start',3);text(70,122,16,'Rumarin',p.dim);
  rect(60,180,460,540,p.inset+'99',p.line);top(90,210,13,'LOADOUT',p.dim,2);
  poolLabels.forEach((label,i)=>{const y=260+i*88;out+='<g role="button" tabindex="0" aria-label="'+label+'" data-pool="'+i+'">';rect(88,y,400,74,poolOpen&&poolTab===i?p.accent:p.inset,poolOpen&&poolTab===i?p.edge:p.line);glyph(117,y+37,24,i===2?0xf6de:0xe2ca,p.edge);text(151,y+22,12,label,p.text,'start',1.2);text(151,y+48,15,demoPools[i].size?demoPools[i].size+' selected':'Automatic choice',p.dim);glyph(465,y+37,12,poolOpen&&poolTab===i?0xf078:0xf054,p.dim);out+='</g>';});
  text(90,592,13,'BEST AT',p.dim,'start',2);text(90,634,16,'Destruction',p.text);text(460,634,16,'54',p.dim,'end');text(90,673,16,'Restoration',p.text);text(460,673,16,'48',p.dim,'end');
  if(!poolOpen)return;
  const x=560,y=180,w=760;rect(x,y,w,670,p.inset,p.edge);rect(x+1,y+1,w-2,68,p.accent+'55');text(x+25,y+35,18,poolLabels[poolTab],p.text,'start',1.2);
  out+='<g role="button" tabindex="0" aria-label="Close pool" id="close-pool">';glyph(x+w-30,y+34,18,0xf00d,p.dim);out+='</g>';
  text(x+25,y+98,14,poolTab===2?'Choose one or more weapons to start the battle with':'ALL SCHOOLS',p.dim,'start',poolTab===2?0:1.6);
  const choices=['Automatic choice',...poolChoices[poolTab]];
  choices.forEach((name,i)=>{const py=y+130+i*68,selected=i===0?demoPools[poolTab].size===0:demoPools[poolTab].has(i-1);out+='<g role="checkbox" aria-checked="'+selected+'" tabindex="0" aria-label="'+name+'" data-pool-item="'+i+'">';rect(x+20,py,w-40,60,selected?p.accent+'99':p.bg,selected?p.edge:p.line);emblem(x+48,py+30,13,p.inset,selected?p.edge:p.line);if(selected)glyph(x+48,py+30,13,0xf00c,p.text);text(x+78,py+30,17,name,selected?p.text:p.dim);if(i)text(x+w-42,py+30,13,poolTab===2?'x1':i<3?'Novice':'Apprentice',p.dim,'end');out+='</g>';});
  text(x+25,y+633,14,demoPools[poolTab].size+' selected',p.dim);
  out+='<g role="button" tabindex="0" aria-label="Done" id="pool-done">';rect(x+w-110,y+610,85,40,p.accent,p.line);text(x+w-68,y+630,15,'Done',p.text,'middle');out+='</g>';
}
function bindLoadouts(){
 const activate=(el,fn)=>{el.onclick=fn;el.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();fn();}};};
 stage.querySelectorAll('[data-pool]').forEach(el=>activate(el,()=>{const n=+el.dataset.pool;poolOpen=poolTab!==n||!poolOpen;poolTab=n;render();}));
 stage.querySelectorAll('[data-pool-item]').forEach(el=>activate(el,()=>{const n=+el.dataset.poolItem-1;if(n<0)demoPools[poolTab].clear();else if(demoPools[poolTab].has(n))demoPools[poolTab].delete(n);else demoPools[poolTab].add(n);poolCursor=n+1;render();stage.querySelector('[data-pool-item="'+(n+1)+'"]')?.focus();}));
 for(const id of ['close-pool','pool-done']){const el=stage.querySelector('#'+id);if(el)activate(el,()=>{poolOpen=false;render();});}
}
function drawTreeBackdrop(canvasW,h){
  const key=['fighter','tank','healer','bruiser','archer','mage','skald'][role],side=Math.min(canvasW*.48,h*.57),left=theme==='oxblood'?22:canvasW-side-22;
  out+=`<defs><linearGradient id="art-fade" gradientUnits="userSpaceOnUse" x1="0" x2="0" y1="116" y2="${116+side}"><stop offset="0" stop-color="white" stop-opacity="0"/><stop offset=".08" stop-color="white"/><stop offset=".58" stop-color="white"/><stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient><mask id="art-mask" maskUnits="userSpaceOnUse" x="${left}" y="116" width="${side}" height="${side}"><rect x="${left}" y="116" width="${side}" height="${side}" fill="url(#art-fade)"/></mask></defs>`;
  out+=`<image class="tree-backdrop" data-art="${theme}-${key}" href="/backdrops/${theme}-${key}.png" x="${left}" y="116" width="${side}" height="${side}" opacity="${theme==='nordic'?.27:.23}" mask="url(#art-mask)" pointer-events="none"/>`;
  out+=`<foreignObject x="0" y="0" width="${canvasW}" height="${h}" pointer-events="none" aria-hidden="true"><canvas xmlns="http://www.w3.org/1999/xhtml" id="atmosphere-canvas" data-theme="${theme}" data-width="${canvasW}" data-height="${h}" style="width:100%;height:100%;pointer-events:none"></canvas></foreignObject>`;
}

function render(){
  const oldPoses=new Map(poses),roleChanged=previousRole!==role;poses.clear();previousRole=role;
  theme=document.querySelector('#theme').value;p=palettes[theme];activeFace=bodyFaces[theme];out='';
  for(const id of ['loadouts','battle','overview','details','callouts'])document.getElementById(id).classList.toggle('active',mode===id);
  const hazeColor=theme==='oxblood'?'#5c221b':theme==='nordic'?'#635d49':'#555555';
  out+='<defs><linearGradient id="night" x2=".8" y2="1"><stop stop-color="'+(theme==='vanilla'?'#030303':theme==='nordic'?'#0a0c0b':'#0c0505')+'"/><stop offset="1" stop-color="'+(theme==='vanilla'?'#080808':theme==='nordic'?'#131410':'#160908')+'"/></linearGradient><radialGradient id="haze"><stop stop-color="'+hazeColor+'" stop-opacity=".11"/><stop offset="1" stop-color="'+hazeColor+'" stop-opacity="0"/></radialGradient><linearGradient id="drawer"><stop stop-color="'+p.inset+'" stop-opacity=".08"/><stop offset=".6" stop-color="'+p.inset+'" stop-opacity=".94"/></linearGradient><linearGradient id="foot" x2="0" y2="1"><stop stop-color="'+p.inset+'" stop-opacity="0"/><stop offset="1" stop-color="'+p.inset+'" stop-opacity=".7"/></linearGradient></defs>';
  const fullTree=mode==='overview'||mode==='details',ratio=innerWidth/innerHeight,treeW=Math.max(1920,1080*ratio),treeH=Math.max(1080,1920/ratio);
  rect(0,0,fullTree?treeW:1872,fullTree?treeH:996,'url(#night)');
  
  stage.setAttribute('viewBox',fullTree?`0 0 ${treeW} ${treeH}`:'0 0 1872 996');
  if(mode==='loadouts'){drawLoadouts();stage.innerHTML=out;bindLoadouts();return;}
  if(mode==='battle'){drawBattle();stage.innerHTML=out;bindBattle();return;}
  if(mode==='callouts'){drawCallouts();stage.innerHTML=out;return;}
  const open=mode==='details',w=treeW,h=treeH,drawerX=open?w-480:w,canvasW=drawerX-24,scale=clamp(canvasW/1376,.93,1.0);
  const apexY=180+(h-732)*.18,branchY=h-298,abilityY=branchY-202*scale,families=[canvasW*.18,canvasW*.5,canvasW*.82];
  const r=data.roles[role],nodes=r.nodes,noneEligible=document.getElementById('points').value==='locked',points=noneEligible?4:+document.getElementById('points').value;
  const rankMode=document.getElementById('ranks').value;
  const ranks=nodes.map((n,i)=>n?learned.get(role+':'+i)??(noneEligible?n.most:rankMode!=='mixed'?Math.min(n.most,+rankMode):Math.min(n.most,[3,2,1,2,1,0,1,0,0,0,0,0,1,0,0][i])):0);
  const investment=(n,rank)=>n.kind==='ability'?rank*(rank+1)/2:rank;
  const spent=nodes.reduce((sum,n,i)=>sum+(n?investment(n,ranks[i]):0),0),below=tier=>nodes.reduce((sum,n,i)=>sum+(n&&n.tier<tier?investment(n,ranks[i]):0),0);
  const locked=n=>!debugMode&&(followerLevel()<requiredLevel(n,ranks[nodes.indexOf(n)]+1)||(n.capstone&&nodes.some((q,i)=>q&&!q.capstone&&ranks[i]<1)));
  const buyable=i=>{const n=nodes[i];return n&&ranks[i]<n.most&&(debugMode||(!noneEligible&&points>=rankCost(n,ranks[i]+1)&&!locked(n)&&(n.kind!=='branch'||ranks[i]+1<=ranks[i-i%3])));};
  drawTreeBackdrop(canvasW,h);
  out+='<g data-pose="title" data-x="'+canvasW*.5+'" data-y="0">';poses.set('title',[canvasW*.5,0]);
  text(canvasW*.5,33,14,`${battleMembers[battleMember].name.toUpperCase()}  /  LEVEL ${followerLevel()}`,p.dim,'middle',2.5);text(canvasW*.5,72,34,r.title.toUpperCase(),p.text,'middle',4);
  for(const [dir,label] of [[-1,'Previous companion'],[1,'Next companion']]){const x=canvasW*.5+dir*220;out+='<g data-tree-companion="'+dir+'" role="button" aria-label="'+label+'" style="cursor:pointer"><title>'+label+'</title>';rect(x-20,10,40,42,'transparent');glyph(x,33,16,dir<0?0xf053:0xf054,p.dim);out+='</g>';}
  line(canvasW*.5-155,108,canvasW*.5-12,108,p.edge+'3d');line(canvasW*.5+12,108,canvasW*.5+155,108,p.edge+'3d');diamond(canvasW*.5,108,4,'none',p.edge+'b2');out+='</g>';
  const bonusX=w-510,bonusY=43;
  if(role!==6){
  rect(bonusX,bonusY,235,32,p.inset+'55',p.line);glyph(bonusX+23,bonusY+16,16,[0xf0e7,0xf3ed,0xf21e,0xf6e3,0xf2ea,0xf0d0][role],p.edge);text(bonusX+44,bonusY+16,18,(role===1||role===5?'-':role===4?'':'+' )+spent+'%',p.text);text(bonusX+97,bonusY+16,10,['ATTACK SPEED','DAMAGE TAKEN','HEALING','DAMAGE','ARROWS KEPT','MAGICKA COST'][role],p.dim,'start',1.5);
  for(const [i,g]of[[0,0xf068],[1,0xf067]]){rect(bonusX+243+i*34,bonusY,30,32,'none',p.line);glyph(bonusX+258+i*34,bonusY+16,12,g,p.dim);}
  }
  out+='<g id="debug-toggle" role="button" tabindex="0" aria-label="Debug Mode" aria-pressed="'+debugMode+'"><title>Debug Mode: free ranks and out-of-combat ability testing</title>';
  circle(w-139,48,19,debugMode?p.accent:p.inset,p.edge);glyph(w-139,48,15,0xf188,p.text);text(w-139,82,10,debugMode?'DEBUG: ON':'DEBUG',p.dim,'middle',1);out+='</g>';
  circle(w-55,48,19,'none',p.line);text(w-55,48,18,'?',p.dim,'middle');
  const at=nodes.map((n,i)=>i<9?[families[Math.floor(i/3)]+(i%3===1?-94:i%3===2?94:0)*scale,(i%3===0?abilityY:branchY)+(Math.floor(i/3)===1?-18:12)*scale]:i===9?[canvasW*.5,apexY]:i<12?[canvasW*.5+(i===10?-220:220)*scale,apexY+30*scale]:[families[i-12],h-135]);
  for(let i=0;i<12;i+=3){if(!nodes[i])continue;const a=at[i];if(!nodes[i].capstone)for(let j=i+1;j<i+3;j++){if(!nodes[j])continue;const b=at[j],dir=b[0]<a[0]?-1:1,sx=a[0]+dir*(i===9?55:32)*scale,sy=a[1]+(i===9?0:25)*scale,ex=i===9?b[0]-dir*39*scale:b[0],ey=i===9?b[1]:b[1]-39*scale;
    const path='M'+sx+' '+sy+'C'+sx+' '+(sy+50*scale)+','+ex+' '+(ey-45*scale)+','+ex+' '+ey,lit=ranks[j]>0,focused=open&&(chosen===i||chosen===j);
    out+='<path class="branch-link" data-link="'+j+'" d="'+path+'" fill="none" stroke="'+p.edge+'" stroke-opacity="'+(lit?.065:.02)+'" stroke-width="7"/><path class="branch-link" data-link="'+j+'" d="'+path+'" fill="none" stroke="'+(focused?p.text:p.edge)+'" stroke-opacity="'+(focused?.9:lit?.6:.24)+'" stroke-width="'+(focused?1.8:1.15)+'"/>';
    if(lit)out+='<circle r="1.65" fill="'+p.text+'" opacity=".65"><animateMotion dur="4.76s" begin="-'+j*.8+'s" repeatCount="indefinite" path="'+path+'"/></circle>';
  }
    if(!nodes[i].capstone&&locked(nodes[i]))text(a[0],a[1]+115*scale+9,13,`LEVEL ${requiredLevel(nodes[i],1)}`,p.faint,'middle',1.8);
  }
  for(let i=0;i<12;i++){
    const n=nodes[i];if(!n)continue;const [x,y]=at[i],radius=(n.capstone?51:n.kind==='ability'?52:34);
    const nameHidden=!debugMode&&followerLevel()<requiredLevel(n,1);
    poses.set(String(i),[x,y]);out+=`<g class="perk" data-node="${i}" data-pose="${i}" role="button" tabindex="0" aria-label="${escape(nameHidden?'Requires level '+requiredLevel(n,1):n.name)}">`;node(x,y,radius*scale,n,ranks[i],open&&chosen===i,locked(n),buyable(i),scale);
    if(!nameHidden&&(n.kind==='ability'||n.capstone))abilityName(x,y,scale,n.name,locked(n)?p.dim:p.text);
    else if(!nameHidden){const font=19*scale,max=Math.min(166*scale,canvasW*.32-188*scale-22);text(x,y+(radius+39)*scale,font*Math.min(1,max/Math.max(width(n.name,font),1)),n.name,p.text,'middle');}
    if(n.capstone&&!debugMode){top(x+110*scale,y-35*scale,15,'CAPSTONE',p.dim,2.2);top(x+110*scale,y+16*scale,17*scale,followerLevel()<50?'Requires level 50':'Unlock every other perk',p.dim);}
    out+='</g>';
  }
  line(canvasW*.5-185,h-205,canvasW*.5-85,h-205,p.edge+'33');line(canvasW*.5+85,h-205,canvasW*.5+185,h-205,p.edge+'33');text(canvasW*.5,h-205,13,'INNATE GIFTS',p.dim,'middle',2.8);
  for(let i=12;i<15;i++){const n=nodes[i];if(!n)continue;const [x,y]=at[i];const nameHidden=!debugMode&&followerLevel()<requiredLevel(n,1);
    poses.set(String(i),[x,y]);out+=`<g class="perk" data-node="${i}" data-pose="${i}" role="button" tabindex="0" aria-label="${escape(n.name)}">`;node(x,y-5,28,n,ranks[i],open&&chosen===i,locked(n),buyable(i));text(x,y+55,19*Math.min(1,canvasW*.27/Math.max(width(n.name,19),1)),n.name,p.text,'middle');text(x,y+79,15,`Rank ${ranks[i]} / ${n.most}`,p.dim,'middle');out+='</g>';}
  if(open)drawDetails(drawerX,h,nodes[chosen],ranks[chosen],buyable(chosen),ranks,chosen);
  const px=drawerX-53,py=h-150,canSpend=nodes.some((n,i)=>buyable(i));
  poses.set('points',[px,py]);out+='<g data-pose="points">';
  if(canSpend){out+='<g class="glow">';for(let i=4;i>=1;i--)circle(px,py,52*(.55+.14*i),'#d63a3407');out+='</g>';}
  diamond(px,py,29,'#70191a',canSpend?'#d63a34':'#8e3b38',1.6);diamond(px,py,24,'none','#d63a343d');text(px,py,27,debugMode?'∞':String(points),p.text,'middle');out+='</g>';
  out+='<g id="picker" role="button" aria-label="Select role">';rect(30,24,294,72,p.inset+'33');line(87,94,319,94,p.edge+'55');emblem(61,59,28,p.inset+'99',p.edge+'80');glyph(61,59,26,r.glyph);text(185,60,23,r.name,p.text,'middle');glyph(300,60,13,picker?0xf077:0xf078,p.dim);out+='</g>';
  if(picker){out+='<g class="role-list">';rect(30,103,294,378,p.inset,p.line);data.roles.forEach((r,i)=>{const yy=110+52*i;out+=`<g data-role="${i}" role="button" aria-label="${escape(r.name)} path">`;rect(37,yy,280,52,i===role?p.accent+'55':p.inset);glyph(62,yy+26,21,r.glyph);top(88,yy+12,21,r.name,i===role?p.text:p.dim);if(i===1)diamond(298,yy+26,4,p.edge);out+='</g>';});out+='</g>';}
  const oldLinks=[...stage.querySelectorAll('.branch-link')].map(el=>el.getAttribute('d'));
  stage.innerHTML=out;
  stage.querySelectorAll('[data-pose]').forEach(el=>{
    if(query.get('render')==='1')return;
    const key=el.dataset.pose,current=poses.get(key),old=oldPoses.get(key);
    if(!current)return;
    if(old&&!roleChanged)el.animate([{transform:'translate('+(old[0]-current[0])+'px,'+(old[1]-current[1])+'px)'},{transform:'translate(0,0)'}],{duration:420,easing:'cubic-bezier(.4,0,.2,1)'});
    else if(roleChanged&&el.classList.contains('perk'))el.animate([{opacity:0,transform:'translateY(12px)'},{opacity:1,transform:'translateY(0)'}],{duration:500,delay:Number(key)<12?Math.floor(Number(key)/3)*55+(Number(key)%3)*65:220,fill:'backwards',easing:'ease-out'});
  });
  if(!roleChanged)stage.querySelectorAll('.branch-link').forEach((el,i)=>{if(oldLinks[i])el.animate([{d:'path("'+oldLinks[i]+'")'},{d:'path("'+el.getAttribute('d')+'")'}],{duration:420,easing:'cubic-bezier(.4,0,.2,1)'});});
  stage.querySelectorAll('[data-node]').forEach(el=>el.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();el.dispatchEvent(new MouseEvent('click',{bubbles:true}));}}));
  const debugToggle=stage.querySelector('#debug-toggle');
  const toggleDebug=()=>{if(!debugMode){debugSaved=new Map(learned);debugMode=true;}else{learned.clear();for(const [k,v]of debugSaved)learned.set(k,v);debugMode=false;}render();};
  debugToggle?.addEventListener('click',toggleDebug);debugToggle?.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();toggleDebug();}});
  stage.querySelector('#picker')?.addEventListener('click',()=>{picker=!picker;render();});
  stage.querySelectorAll('[data-role]').forEach(g=>g.addEventListener('click',()=>{role=+g.dataset.role;chosen=3;picker=false;mode='overview';render();}));
  stage.querySelectorAll('[data-node]').forEach(g=>g.addEventListener('click',()=>{if(picker){picker=false;render();return;}chosen=+g.dataset.node;mode='details';render();}));
  stage.querySelector('#learn-rank')?.addEventListener('click',()=>{if(buyable(chosen)){learned.set(role+':'+chosen,ranks[chosen]+1);reveal=chosen;render();}});
  stage.querySelectorAll('[data-equip-slot]').forEach(el=>el.addEventListener('click',()=>{if(nodes[chosen]?.kind!=='ability'||ranks[chosen]<1)return;const slots=equipped.get(role)||[0,3];if(slots.includes(chosen))return;slots[+el.dataset.equipSlot]=chosen;equipped.set(role,slots);render();}));
  if(reveal>=0){
    const el=stage.querySelector('[data-node="'+reveal+'"]'),xy=at[reveal],n=nodes[reveal],master=n.kind==='ability'&&ranks[reveal]>=3,duration=master?2200:1800;
    if(el&&xy){const [x,y]=xy,base=(n.capstone?51:n.kind==='ability'?52:n.kind==='branch'?34:32)*scale,kind=n.kind==='branch'?1:n.kind==='trait'?2:0,ns='http://www.w3.org/2000/svg';
      el.style.transformOrigin=x+'px '+y+'px';el.animate([{transform:'scale(1)'},{transform:'scale(1.04)',offset:.19},{transform:'scale(1)',offset:.38},{transform:'scale(1)'}],{duration,easing:'ease-in-out'});
      const waves=document.createElementNS(ns,'g');waves.setAttribute('pointer-events','none');waves.setAttribute('class','unlock-waves');el.before(waves);
      const outline=nodeContour(x,y,base,kind,master).map(q=>q.join(',')).join(' ');
      for(let wave=0;wave<(master?4:3);wave++)for(let layer=0;layer<3;layer++){
        const path=document.createElementNS(ns,'polygon');path.setAttribute('points',outline);path.setAttribute('fill','none');path.setAttribute('stroke',p.edge);path.setAttribute('stroke-width',[10,5,1.25][layer]*scale);path.style.transformOrigin=x+'px '+y+'px';waves.append(path);
        const ink=[.018,.045,.17][layer]*(master?1.12:1),start=master?1.62:1.03,end=master?1.11:1.40;
        path.animate([{opacity:0,transform:`scale(${start})`},{opacity:ink,offset:.5},{opacity:0,transform:`scale(${end})`}],{duration:duration*.62,delay:duration*wave*.12,easing:'ease-in-out',fill:'both'});
      }
      if(master){const seal=document.createElementNS(ns,'polygon');seal.setAttribute('points',outline);seal.setAttribute('fill','none');seal.setAttribute('stroke',p.edge);seal.setAttribute('stroke-width',2*scale);seal.style.transformOrigin=x+'px '+y+'px';seal.style.transform='scale(1.12)';waves.append(seal);seal.animate([{opacity:0},{opacity:.38},{opacity:0}],{duration:duration*.48,delay:duration*.52,fill:'both',easing:'ease-in-out'});}
      setTimeout(()=>waves.remove(),duration);
    }reveal=-1;
  }
  stage.querySelector('#close-details')?.addEventListener('click',()=>{mode='overview';render();});
  stage.querySelectorAll('[data-tree-companion]').forEach(el=>el.addEventListener('click',()=>switchTreeCompanion(+el.dataset.treeCompanion)));
}
function drawDetails(x,h,n,rank,buyable,ranks,index){
  const a=data.abilities.find(a=>a.id===n.ability),s=1.15,dx=x+28,topY=139,dw=424;
  rect(x-44,126,518,h-232,'url(#drawer)');line(x,151,x,h-130,p.edge+'4c',1);
  if(!debugMode&&n.kind!=='trait'&&followerLevel()<requiredLevel(n,1)){
    glyph(dx+dw*.5,topY+58,28,0xf023,p.dim);text(dx+dw*.5,topY+111,17,`Requires level ${requiredLevel(n,1)}`,p.dim,'middle');
    out+='<g id="close-details" role="button" aria-label="Close details">';rect(x+420,130,32,32,'transparent');glyph(x+438,147,15,0xf00d,p.dim);out+='</g>';return;
  }
  node(dx+36*s,topY+40*s,24*s,n,rank,false,false,buyable,s);
  top(dx+88*s,topY+8*s,10.5*s,n.kind==='ability'?'ABILITY':n.kind==='branch'?`BRANCH OF ${a?.name??''}`:'TRAIT / ALWAYS ON',p.edge,1.5);
  withMaster(n.kind==='ability'?rank:0,()=>top(dx+88*s,topY+26*s,Math.min(25*s,255/Math.max(width(n.name.toUpperCase(),25*s),1)*25*s),n.name.toUpperCase(),p.text,2));
  top(dx+88*s,topY+58*s,14*s,`Rank ${['0','I','II','III'][rank]} of ${['0','I','II','III'][n.most]}`,p.edge);
  let y=topY+96*s;line(dx,y,dx+dw,y);y+=22;
  const intro=n.description?n.description.replaceAll('{0}',n.values[Math.max(1,rank)-1]):n.kind==='ability'?(a?.own||'Use when the moment calls for it.'):n.kind==='branch'?`Strengthens ${a?.name||'its parent ability'}.`:'Always active while its requirements are met.';
  y+=para(dx,y,15.5*s,intro,dw,p.dim)+28;
  top(dx,y,12*s,'RANKS',p.dim,1.8);y+=33;
  for(let i=1;i<=n.most;i++){const held=i<=rank,next=i===rank+1;rect(dx,y,dw,55,next?p.accent+'40':'none');diamond(dx+18,y+27,12,held?p.accent:p.inset,held?p.edge:p.line);text(dx+18,y+27,10.5,['','I','II','III'][i],held?p.text:p.dim,'middle');text(dx+51,y+27,16,n.measure&&n.values?n.measure.replace('{}',n.values[i-1]):held?'Learned':debugMode?'Free':requiredLevel(n,i)>0?`Level ${requiredLevel(n,i)}`:next?'Next rank':'Not learned',held||next?p.text:p.faint);if(held)glyph(dx+dw-18,y+27,13,0xf00c,p.edge);else{diamond(dx+dw-35,y+27,4,p.dim,p.dim);text(dx+dw-18,y+27,14,String(rankCost(n,i)),p.dim,'middle');}y+=55;}
  const actionY=h-118-30*s-16*s-128*s-20*s-46*s;
    let blocked='Not enough perk points';
  if(followerLevel()<requiredLevel(n,rank+1))blocked=`Requires level ${requiredLevel(n,rank+1)}`;
  else if(n.capstone&&data.roles[role].nodes.some((q,i)=>q&&!q.capstone&&ranks[i]<1))blocked='Unlock every other perk';
  else if(n.kind==='branch'&&rank+1>ranks[index-index%3])blocked='Requires parent ability rank';
  out+='<g id="learn-rank" role="button" aria-label="Learn next rank" style="cursor:pointer">';rect(dx,actionY,dw,46*s,buyable?p.accent:p.inset,buyable?p.edge:p.line);text(dx+dw*.5,actionY+23*s,17,buyable?(rank?'RANK UP':'LEARN'):rank>=n.most?'MASTERED':blocked,p.text,'middle',1.5);out+='</g>';
  top(dx,actionY+46*s+22,12*s,'EQUIPPED ABILITIES',p.dim,1.7);
  const slots=equipped.get(role)||[0,3];
  for(let i=0;i<2;i++){const a=data.roles[role].nodes[slots[i]],held=ranks[slots[i]]>0,here=slots[i]===index&&held;out+='<g data-equip-slot="'+i+'" role="button" aria-label="Equip in slot '+(i+1)+'" style="cursor:pointer">';rect(dx+i*212+4,actionY+46*s+42,204,98,'transparent');emblem(dx+106+i*212,actionY+46*s+80,23,here?p.accent:p.inset,p.edge);if(ranks[slots[i]]>=3)mastery(dx+106+i*212,actionY+46*s+80,23);if(held)glyph(dx+106+i*212,actionY+46*s+80,22,a.glyph);else text(dx+106+i*212,actionY+46*s+80,18,String(i+1),p.dim,'middle');text(dx+106+i*212,actionY+46*s+117,13,here?'Equipped':held?a.name:'Empty slot',p.dim,'middle');out+='</g>';}
  rect(dx,h-118-30*s,dw,30*s,'none',p.line);text(dx+dw*.5,h-118-15*s,13,'START OVER',p.dim,'middle',1.7);
  out+='<g id="close-details" role="button" aria-label="Close details">';rect(x+420,130,32,32,'transparent');glyph(x+438,147,15,0xf00d,p.dim);out+='</g>';
}

const battleMembers=[{name:'Duraz',role:1},{name:'Rumarin',role:5},{name:'Elisif',role:2},{name:'Inigo',role:4}];
let battleMember=0,battleOrder=-1,battleFocus=3,battlePosition=2,battleTactics=false;
const battleAuto=new Map(),battleTimers=new Map();let battleMessage='';
function drawBattle(){
  const m=battleMembers[battleMember],r=data.roles[m.role];
  text(936,51,27,'FIGHT WITH ME',p.text,'middle',3);text(936,91,14,'TIME SLOWED  /  4 COMPANIONS  /  3 ENEMIES',p.dim,'middle',1);
  const commands=[['Attack',0xf140],['To me',0xf505],['Hold back',0xf2ea],['As you were',0xf0c0]];
  commands.forEach(([label,icon],i)=>{const x=696+i*120;out+='<g role="button" aria-label="'+label+'" data-command="'+i+'">';rect(x+4,114,112,76,i===battleOrder?p.accent:p.inset,i===battleOrder?p.edge:p.line);glyph(x+60,142,24,icon);text(x+60,170,Math.min(13,94/Math.max(width(label,13),1)*13),label,p.text,'middle');out+='</g>';});
  // Full character details remain visible; tactical controls open only on request.
  rect(1392,112,440,440,p.inset+'ed',p.line);rect(1393,113,438,66,p.accent+'90');text(1416,154,26,m.name.toUpperCase(),p.text);diamond(1778,146,27,p.inset,p.edge);text(1778,152,22,'32',p.text,'middle');
  text(1416,203,15,r.name+' / fighting Bandit Marauder',p.dim);
  [[.82,'#b22c28','328 / 400'],[.61,'#5482cc','122 / 200'],[.7,'#76ae5e','210 / 300']].forEach(([v,c,label],i)=>{const x=1416+i*129;rect(x,225,118,5,'#ffffff10');rect(x,225,118*v,5,c);text(x,250,13,label,p.dim);});
  text(1416,283,14,'ROLE',p.edge);text(1808,283,10,'SET OUT OF A FIGHT',p.faint,'end',1);
  emblem(1475,363,39,p.accent,p.edge);glyph(1475,363,29,r.glyph);text(1475,422,14,r.name,p.text,'middle');
  text(1565,329,14,'ARMOR',p.dim);text(1792,329,17,'384',p.text,'end');text(1565,360,14,'DAMAGE',p.dim);text(1792,360,17,'61',p.text,'end');
  [0xf06d,0xf2dc,0xf0e7,0xf714,0xf0d0].forEach((icon,i)=>{glyph(1575+i*49,397,16,icon,p.dim);text(1575+i*49,425,12,['25%','50%','0%','0%','15%'][i],p.dim,'middle');});
  line(1416,446,1808,446);text(1416,474,13,'TRAITS',p.dim);text(1416,506,14,'Steady hand',p.text);text(1615,506,14,'Battle hardened',p.text);
  const focusText=['Their choice','Your target','Attacking you','Attacking the party','Casters and archers','Strongest','Weakest'][battleFocus];
  const positions=['Formation','Free','Guard you','Hold back','Front line','With me'];
  out+='<g role="button" aria-label="Focus and Position controls" aria-expanded="'+battleTactics+'" id="battle-tactics">';glyph(1420,586,18,0xf1de);text(1448,591,14,focusText+' / '+positions[battlePosition],p.dim);glyph(1796,586,13,battleTactics?0xf077:0xf078);out+='</g>';
  if(battleTactics){
    rect(938,558,440,300,p.inset+'f5',p.line);text(962,594,14,'FOCUS',p.edge);
    out+='<g role="button" aria-label="Change target priority" id="battle-focus">';rect(962,613,392,52,'#ffffff06',p.line);glyph(986,644,17,0xf053);glyph(1330,644,17,0xf054);text(1158,644,16*Math.min(1,290/width(focusText,16)),focusText,p.text,'middle');out+='</g>';
    text(962,715,14,'POSITION',p.edge);positions.forEach((name,i)=>{const x=994+i*65;out+='<g role="button" aria-label="'+name+' position" data-position="'+i+'">';emblem(x,763,19,i===battlePosition?p.accent:p.inset,i===battlePosition?p.edge:p.line);glyph(x,763,16,[0xf0c0,0xf0b2,0xf505,0xf2ea,0xf124,0xf076][i]);out+='</g>';});text(1158,816,14,positions[battlePosition],p.text,'middle');
  }
  // Feathered shadow instead of an enclosing utility window.
  out+='<defs><radialGradient id="abilityVeil"><stop offset="0" stop-color="'+p.inset+'" stop-opacity=".85"/><stop offset=".65" stop-color="'+p.inset+'" stop-opacity=".6"/><stop offset="1" stop-color="'+p.inset+'" stop-opacity="0"/></radialGradient></defs><ellipse cx="1612" cy="857" rx="239" ry="160" fill="url(#abilityVeil)"/>';
  (equipped.get(m.role)||[0,3]).forEach((index,slot)=>{const n=r.nodes[index],x=1510+slot*204,y=798,key=battleMember+':'+slot,ready=debugMode?0:Math.max(0,Math.ceil(((battleTimers.get(key)||0)-Date.now())/1000)),rank=learned.get(m.role+':'+index)??(slot===0?3:2);
    out+='<g role="button" aria-label="Use '+escape(n.name)+'" data-ability="'+slot+'">';
    node(x,y,56,n,rank,!ready,false,false,1,false);if(ready){poly(sealPoints(x,y,53),'#05080ac9');text(x,y,25,String(ready),p.text,'middle');}
    rect(x-65,y+29,24,24,p.inset,p.line);text(x-53,y+41,13,String(slot+1),p.text,'middle');
    withMaster(rank,()=>text(x,y+78,17*Math.min(1,190/Math.max(width(n.name,17),1)),n.name,p.text,'middle'));out+='</g>';
    out+='<g role="button" aria-label="Toggle '+escape(n.name)+' automatic use" data-auto="'+slot+'">';const on=battleAuto.get(key)!==false;rect(x-52,y+95,104,26,on?p.accent:p.inset,p.line);text(x,y+108,11.5,on?'AUTO':'MANUAL',p.text,'middle');out+='</g>';
  });
  out+='<g role="button" tabindex="0" aria-label="Skill tree" id="battle-tree"><title>Skill tree</title>';
  emblem(1612,960,26,p.inset,p.edge);line(1612,973,1601,958,p.text,1.5);line(1612,973,1623,958,p.text,1.5);line(1601,958,1612,946,p.text,1.4);line(1623,958,1612,946,p.text,1.4);[[1612,973],[1601,958],[1623,958],[1612,946]].forEach(([x,y])=>diamond(x,y,3.5,p.text));diamond(1643,980,12,'#9e2422',p.edge);text(1643,980,11,'4',p.text,'middle');out+='<g class="tree-label">';text(1612,915,14,'Skill tree',p.text,'middle');out+='</g></g>';
  if(battleMessage)text(1612,995,12,battleMessage,p.dim,'middle');
  battleMembers.forEach((m,i)=>{const x=723+i*142;out+='<g role="button" aria-label="Select '+m.name+'" data-member="'+i+'">';if(i===battleMember)selection(x,905,30,{kind:'ability'},false);emblem(x,905,30,i===battleMember?p.accent:p.inset,p.edge);glyph(x,905,24,data.roles[m.role].glyph);text(x,953,14,m.name,i===battleMember?p.text:p.dim,'middle');out+='</g>';});
  text(936,985,12,'1 / 2  USE ABILITY     |     CLICK ICON  USE     |     AUTO / MANUAL  TOGGLE',p.dim,'middle',.4);
}
function bindBattle(){
  stage.querySelectorAll('[data-command]').forEach(el=>el.onclick=()=>{battleOrder=+el.dataset.command;render();});
  stage.querySelectorAll('[data-member]').forEach(el=>el.onclick=()=>{battleMember=+el.dataset.member;battleMessage='';battleTactics=false;render();});
  stage.querySelectorAll('[data-ability]').forEach(el=>el.onclick=()=>usePreviewAbility(+el.dataset.ability));
  stage.querySelectorAll('[data-auto]').forEach(el=>el.onclick=()=>{const key=battleMember+':'+el.dataset.auto;battleAuto.set(key,battleAuto.get(key)===false);render();});
  stage.querySelectorAll('[data-position]').forEach(el=>el.onclick=()=>{battlePosition=+el.dataset.position;render();});
  stage.querySelector('#battle-tactics').onclick=()=>{battleTactics=!battleTactics;render();};
  const focus=stage.querySelector('#battle-focus');if(focus)focus.onclick=()=>{battleFocus=(battleFocus+1)%7;render();};
  const tree=stage.querySelector('#battle-tree');
  tree.onclick=()=>{role=battleMembers[battleMember].role;enterTree();};
  tree.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();role=battleMembers[battleMember].role;enterTree();}};
}
let entranceTimers=[],entranceMotion=null,entranceActive=false;
function cancelEntrance(){entranceTimers.forEach(clearTimeout);entranceTimers=[];entranceMotion?.cancel();entranceMotion=null;entranceActive=false;document.querySelector('#tree-blackout')?.remove();stage.style.pointerEvents='';}
function enterTree(){
 if(entranceActive)return;
 entranceActive=true;stage.style.pointerEvents='none';
 const veil=document.createElement('div');veil.id='tree-blackout';veil.setAttribute('aria-hidden','true');document.body.append(veil);
 entranceMotion=stage.animate([{transform:'translateY(0)',opacity:1},{transform:'translateY(35px)',opacity:.65},{transform:'translateY(90px)',opacity:0}],{duration:780,easing:'cubic-bezier(.4,0,.2,1)',fill:'forwards'});
 veil.animate([{opacity:0,offset:0},{opacity:0,offset:.096},{opacity:1,offset:.52},{opacity:1,offset:.624},{opacity:0,offset:1}],{duration:1250,fill:'forwards',easing:'linear'});
 entranceTimers.push(setTimeout(()=>{entranceMotion.cancel();role=battleMembers[battleMember].role;mode='overview';picker=false;render();},780));
 entranceTimers.push(setTimeout(cancelEntrance,1250));
}
function leaveTree(){
 if(entranceActive)return;
 entranceActive=true;stage.style.pointerEvents='none';
 const veil=document.createElement('div');veil.id='tree-blackout';veil.setAttribute('aria-hidden','true');document.body.append(veil);
 veil.animate([{opacity:0},{opacity:1,offset:320/1150},{opacity:1,offset:650/1150},{opacity:0}],{duration:1150,fill:'forwards',easing:'ease-in-out'});
 entranceTimers.push(setTimeout(()=>{mode='battle';picker=false;render();},320));
 entranceTimers.push(setTimeout(cancelEntrance,1150));
}
function switchTreeCompanion(direction){
 if(entranceActive)return;
 entranceActive=true;stage.style.pointerEvents='none';
 const veil=document.createElement('div');veil.id='tree-blackout';veil.setAttribute('aria-hidden','true');
 veil.style.inset='0 auto 0 0';veil.style.width='120vw';veil.style.background='linear-gradient(90deg,transparent,#000 8.333%,#000 91.667%,transparent)';document.body.append(veil);
 const left=direction>0?'-120vw':'100vw',middle='-10vw',end=direction>0?'100vw':'-120vw';
 veil.animate([{transform:`translateX(${left})`},{transform:`translateX(${middle})`,offset:420/1020},{transform:`translateX(${middle})`,offset:540/1020},{transform:`translateX(${end})`}],{duration:1020,fill:'forwards',easing:'ease-in-out'});
 entranceTimers.push(setTimeout(()=>{battleMember=(battleMember+direction+battleMembers.length)%battleMembers.length;role=battleMembers[battleMember].role;mode='overview';picker=false;render();},420));
 entranceTimers.push(setTimeout(cancelEntrance,1020));
}
function usePreviewAbility(slot){const key=battleMember+':'+slot;if(!debugMode&&(battleTimers.get(key)||0)>Date.now())return;if(!debugMode)battleTimers.set(key,Date.now()+20000);const path=battleMembers[battleMember].role,index=(equipped.get(path)||[0,3])[slot];battleMessage='Preview cast: '+data.roles[path].nodes[index].name;render();}
document.addEventListener('keydown',e=>{if(mode==='battle'&&!['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName)&&['1','2'].includes(e.key)){e.preventDefault();usePreviewAbility(+e.key-1);}});
setInterval(()=>{if(mode==='battle'&&[...battleTimers.values()].some(t=>t>Date.now()-1000))render();},1000);
function drawCallouts(){
  text(936,92,32,'BATTLE CALLOUTS',p.text,'middle',4);
  const samples=[['kSpectralWolf',"Duraz calls Kynareth's Maw: 3 enemies caught in the bite.",p.edge],['kHeal','Elisif raises a healing circle under Duraz.','#84c88c'],['kBodyguard','Duraz: "Your strength fails you!" They and their attackers deal 40% less damage.',p.edge],['kWarCry',"Inigo's war cry staggers 3 enemies.",p.edge],['kStandFast',"Duraz: \"Behind me!\" Absorbing 80% of nearby allies' damage.",p.edge],['kChaosDown','Duraz uses Chaos Down: 3 enemies slowed by 55% for 8 s.',p.edge],['kVolley','Volley! Inigo looses 3 arrows at the Draugr Death Overlord.',p.edge],['kDragonBreath','Rumarin unleashes a stream of lightning.',p.edge]];
  const ys=[200,200];
  samples.forEach(([id,body,accent],i)=>{const a=data.abilities.find(a=>a.id===id);if(!a)return;
    const textWidth=clamp(Math.max(width(body,14),width(a.name,18))+2,160,360),titles=wrap(a.name,18,textWidth),lines=wrap(body,14,textWidth),titleH=titles.length*18*1.2+7,bodyH=lines.length*14*1.2,h=Math.max(78,bodyH+titleH+32),col=i%2,x=440+col*500,y=ys[col],ty=y+(h-bodyH-titleH)/2;
    rect(x,y,textWidth+92,h,p.inset,p.line);line(x,y+10,x,y+h-10,accent,2);emblem(x+32,y+h*.5,23,p.inset,accent,1);glyph(x+32,y+h*.5,25,a.glyph);titles.forEach((t,k)=>top(x+66,ty+k*18*1.2,18,t));lines.forEach((t,k)=>top(x+66,ty+titleH+k*14*1.2,14,t,p.dim));ys[col]+=h+20;
  });
}
for(const id of ['loadouts','battle','overview','details','callouts'])document.getElementById(id).addEventListener('click',()=>{if(id==='battle'&&(mode==='overview'||mode==='details')){leaveTree();return;}mode=id;picker=false;render();});
document.querySelector('#replay-entrance').addEventListener('click',()=>{document.body.classList.remove('preview-controls');document.querySelector('#preview-settings').setAttribute('aria-expanded','false');mode='battle';render();enterTree();});
window.addEventListener('resize',()=>render());
document.getElementById('ranks').addEventListener('change',()=>{learned.clear();render();});
document.getElementById('level').addEventListener('change',render);document.getElementById('theme').addEventListener('change',render);document.getElementById('points').addEventListener('change',render);
document.addEventListener('keydown',e=>{if(entranceActive)return;if(e.key==='Escape'){if(picker)picker=false;else if(mode==='details')mode='overview';else if(mode==='overview'){leaveTree();return;}render();}if(e.key==='Tab'&&(mode==='overview'||mode==='details')&&!document.body.classList.contains('preview-controls')){e.preventDefault();switchTreeCompanion(e.shiftKey?-1:1);}if(e.key==='q'||e.key==='e'){role=(role+(e.key==='e'?1:6))%7;mode='overview';picker=false;render();}});
if(['vanilla','oxblood','nordic'].includes(query.get('theme')))document.getElementById('theme').value=query.get('theme');
for(const key of ['level','ranks','points'])if(query.has(key)&&[...document.getElementById(key).options].some(o=>o.value===query.get(key)))document.getElementById(key).value=query.get(key);
if(query.get('render')==='1'){
 document.getElementById('preview-settings').style.display='none';
 const still=document.createElement('style');still.textContent='*{animation:none!important;transition:none!important}';document.head.append(still);
}
render();

})().catch(error => { document.querySelector('.footnote').textContent = String(error); throw error; });

