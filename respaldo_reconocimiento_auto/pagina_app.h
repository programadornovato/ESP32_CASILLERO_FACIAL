#pragma once
#include <Arduino.h>

// Aplicación principal: estado del reconocimiento, cámara, compartimentos,
// registro de caras y ajustes. (Archivo aparte para que el preprocesador de
// Arduino no confunda el JavaScript con funciones C++.)

const char PAGINA_APP[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Casillero facial</title>
<style>
:root{--bg:#070b16;--card:#111a2e;--line:#22304f;--txt:#e8eefc;--dim:#8a9bc2;--ok:#28e07a;--warn:#ffc53d;--bad:#ff4d5e;--acc:#38bdf8;--vio:#a78bfa}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(circle at 50% -20%,#1a2b55,var(--bg) 60%);color:var(--txt);font-family:system-ui,Arial,sans-serif;min-height:100vh}
header{padding:14px 16px;display:flex;align-items:center;justify-content:space-between;gap:10px}
h1{margin:0;font-size:22px;letter-spacing:1px} h1 b{color:var(--ok)} h1 i{color:var(--vio);font-style:normal}
.sub{color:var(--dim);font-size:12px}
.con{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--dim)}
.con a{color:var(--acc);text-decoration:none;font-weight:700;margin-right:8px}
.dot{width:12px;height:12px;border-radius:50%;background:var(--bad);box-shadow:0 0 8px var(--bad)}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
main{padding:0 16px 24px;max-width:900px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:14px}
h2{font-size:14px;margin:0 0 10px;color:var(--dim);text-transform:uppercase;letter-spacing:1px}
#banner{text-align:center;padding:16px 10px;border-radius:12px;transition:.25s}
#banner .t{font-size:30px;font-weight:900;line-height:1.1}
#banner .s{font-size:15px;margin-top:6px;opacity:.9}
.b-espera{background:#10304a;color:var(--acc)} .b-cara{background:#3d3110;color:var(--warn)}
.b-ok{background:#0f3b26;color:var(--ok);animation:pulso 1s ease-in-out infinite alternate}
.b-mal{background:#3d1218;color:var(--bad)} .b-reg{background:#2a1d4a;color:var(--vio)}
@keyframes pulso{from{box-shadow:0 0 0 #28e07a00}to{box-shadow:0 0 28px #28e07a88}}
.prog{height:10px;background:#0b1324;border-radius:6px;overflow:hidden;margin-top:10px}
.prog i{display:block;height:100%;background:var(--vio);width:0;transition:width .3s}
canvas{width:100%;display:block;border-radius:10px;background:#000}
.comps{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.cp{background:#0b1324;border:2px solid var(--line);border-radius:12px;padding:12px;text-align:center;transition:.25s}
.cp.abierto{border-color:var(--ok);box-shadow:0 0 16px #28e07a55}
.cp .ic{font-size:40px}
.cp .n{font-weight:800;font-size:16px}
.cp .d{color:var(--dim);font-size:12px;min-height:16px;margin-top:4px;word-break:break-word}
.cp .r{color:var(--ok);font-weight:800;min-height:20px}
button{background:#1b2a4a;color:var(--txt);border:1px solid #2f4677;border-radius:10px;padding:12px 14px;font-size:15px;font-weight:700;cursor:pointer}
button:active{transform:scale(.97)} button:disabled{opacity:.5}
.go{background:#0f5132;border-color:var(--ok)} .vio{background:#2a1d4a;border-color:var(--vio)} .del{background:#4a1520;border-color:var(--bad)}
.mini{padding:6px 10px;font-size:13px}
input,select{padding:11px;border-radius:10px;border:1px solid #2f4677;background:#060b17;color:var(--txt);font-size:16px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:10px}
.row label{color:var(--dim);font-size:13px;display:flex;flex-direction:column;gap:4px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
.st{background:#0b1324;border:1px solid var(--line);border-radius:10px;padding:8px}
.st small{display:block;color:var(--dim);font-size:11px}
.st b{font-size:18px;font-variant-numeric:tabular-nums}
table{width:100%;border-collapse:collapse;font-size:14px}
td,th{padding:8px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--dim);font-weight:600;font-size:12px}
#ev div{padding:6px 0;border-bottom:1px solid var(--line);font-size:14px}
#ev span{color:var(--dim);font-size:12px;margin-right:8px;font-variant-numeric:tabular-nums}
#msg{min-height:20px;font-weight:700;margin-top:8px}
.oculto{display:none}
</style></head><body>
<header>
 <div><h1>🔐 <b>CASILLERO</b> <i>FACIAL</i></h1><div class="sub">Reconocimiento facial en la ESP32-CAM</div></div>
 <div class="con"><a href="/wifi">📶 Red</a><div id="dot" class="dot"></div><span id="cont">sin conexión</span></div>
</header>
<main>
 <div class="card">
  <div id="banner" class="b-espera"><div class="t" id="bt">CONECTANDO…</div><div class="s" id="bs"></div>
   <div class="prog oculto" id="prog"><i id="progi"></i></div></div>
 </div>

 <div class="card">
  <h2>Cámara</h2>
  <canvas id="cv" width="320" height="240"></canvas>
  <div class="row"><button id="bprev">⏹ OCULTAR CÁMARA</button><span class="sub">Ponte a 40–70 cm, de frente y con buena luz.</span></div>
 </div>

 <div class="card"><h2>Compartimentos</h2><div class="comps" id="comps"></div></div>

 <div class="card"><h2>Eventos</h2><div id="ev"></div></div>

 <div class="card">
  <h2>Administración</h2>
  <div id="login" class="row">
   <input type="password" id="pin" placeholder="PIN (por defecto 1234)" inputmode="numeric" style="flex:1">
   <button class="go" id="bpin">Entrar</button>
  </div>
  <div id="admin" class="oculto">
   <h2 style="margin-top:6px">Registrar una cara</h2>
   <div class="row">
    <label>Nombre <input id="rnom" maxlength="16" placeholder="Ej. Ana"></label>
    <label>Compartimento <select id="rcomp"><option value="1">1</option><option value="2">2</option><option value="3">3</option></select></label>
    <button class="vio" id="breg">📸 Registrar mi cara</button>
    <button class="mini oculto" id="bcanc">Cancelar (reinicia)</button>
   </div>
   <div class="sub" style="margin-top:6px">Mírate en la cámara: se toman 5 fotos. Mueve un poco la cabeza entre una y otra.</div>

   <h2 style="margin-top:18px">Personas registradas <span id="cuenta"></span></h2>
   <table><thead><tr><th>#</th><th>Nombre</th><th>Compartimento</th></tr></thead><tbody id="tper"></tbody></table>
   <div class="row"><button class="del" id="bborrar">🗑 Borrar todas las caras</button></div>

   <h2 style="margin-top:18px">Probar servos</h2>
   <div class="row"><button id="bp1">Abrir 1</button><button id="bp2">Abrir 2</button><button id="bp3">Abrir 3</button></div>

   <h2 style="margin-top:18px">Ajustes</h2>
   <div class="row">
    <label>Tiempo abierto (s) <input type="number" id="atiempo" min="1" max="60" style="width:110px"></label>
    <label>Ángulo cerrado <input type="number" id="aangc" min="0" max="180" style="width:110px"></label>
    <label>Ángulo abierto <input type="number" id="aanga" min="0" max="180" style="width:110px"></label>
   </div>
   <div class="row">
    <label style="flex-direction:row;align-items:center"><input type="checkbox" id="aluz" style="width:20px;height:20px"> Encender LED flash (poca luz)</label>
   </div>
   <div class="row">
    <label style="flex-direction:row;align-items:center"><input type="checkbox" id="avflip" style="width:20px;height:20px"> Voltear vertical</label>
    <label style="flex-direction:row;align-items:center"><input type="checkbox" id="aespejo" style="width:20px;height:20px"> Espejo</label>
    <span class="sub">Si te ves de cabeza, marca los dos (= girar 180°). Se aplica al instante.</span>
   </div>
   <div class="row">
    <label>Nuevo PIN (opcional) <input type="password" id="anpin" maxlength="8" placeholder="4 a 8 caracteres"></label>
    <button class="go" id="bguardar">💾 Guardar ajustes</button>
   </div>
  </div>
  <div id="msg"></div>
 </div>

 <div class="card">
  <h2>Diagnóstico</h2>
  <div class="grid">
   <div class="st"><small>Análisis/s</small><b id="fps">-</b></div>
   <div class="st"><small>Captura</small><b id="msc">-</b></div>
   <div class="st"><small>Detección</small><b id="msd">-</b></div>
   <div class="st"><small>Reconocimiento</small><b id="msr">-</b></div>
   <div class="st"><small>Caras guardadas</small><b id="nreg">-</b></div>
   <div class="st"><small>Heap / PSRAM</small><b id="mem" style="font-size:14px">-</b></div>
   <div class="st"><small>Cámara / errores</small><b id="cam">-</b></div>
   <div class="st"><small>Red</small><b id="red" style="font-size:14px">-</b></div>
  </div>
 </div>
</main>
<script>
const $=id=>document.getElementById(id);
const cv=$('cv'),cx=cv.getContext('2d');
let preview=true,ultimoOK=0,pin='',ajustesCargados=false,ultimaCuenta=-1,ultimoProgreso=-1;
try{pin=sessionStorage.getItem('pin')||'';}catch(e){}

async function pedir(url,op={},ms=3000){
  const ac=new AbortController();const t=setTimeout(()=>ac.abort(),ms);
  try{const r=await fetch(url,Object.assign({signal:ac.signal,cache:'no-store'},op));clearTimeout(t);return r;}
  catch(e){clearTimeout(t);throw e;}
}
async function post(url,datos){
  const body=new URLSearchParams(Object.assign({pin:pin},datos||{})).toString();
  try{const r=await pedir(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body});
    const d=await r.json();if(!d.ok)mensaje(d.error||'Error',true);return d;}
  catch(e){mensaje('Sin respuesta de la ESP32',true);return {ok:false};}
}
function mensaje(t,mal){const m=$('msg');m.textContent=t;m.style.color=mal?'var(--bad)':'var(--ok)';clearTimeout(m._t);m._t=setTimeout(()=>m.textContent='',5000);}

const BANNER={
 ESPERANDO:['b-espera','🔒 ESPERANDO ROSTRO','Mírate en la cámara'],
 CARA_DETECTADA:['b-cara','🙂 CARA DETECTADA','Acércate y mira de frente'],
 RECONOCIENDO:['b-cara','🔎 RECONOCIENDO…',''],
 ACCESO:['b-ok','✅ ¡HOLA!',''],
 DESCONOCIDO:['b-mal','⛔ ROSTRO NO REGISTRADO','No tienes un compartimento asignado'],
 REGISTRANDO:['b-reg','📸 REGISTRANDO','Mírate en la cámara y mueve un poco la cabeza'],
 ERROR:['b-mal','⚠ ERROR','']};

function pintar(d){
  ultimoOK=Date.now();
  const b=BANNER[d.estado]||BANNER.ESPERANDO;
  let t=b[1],s=b[2];
  if(d.estado==='ACCESO'){t='✅ ¡HOLA, '+d.nombre.toUpperCase()+'!';s=d.comp>=0?('Abriendo compartimento '+(d.comp+1)):'Sin compartimento asignado';}
  if(d.estado==='RECONOCIENDO')s='Parece '+d.nombre+'… confirmando';
  if(d.estado==='REGISTRANDO')s='Foto '+d.progreso+' de '+d.muestras+' · '+b[2];
  if(d.estado==='ERROR')s=d.error;
  $('banner').className=b[0];$('bt').textContent=t;$('bs').textContent=s;
  $('prog').classList.toggle('oculto',d.estado!=='REGISTRANDO');
  $('progi').style.width=(d.progreso/d.muestras*100)+'%';
  $('bcanc').classList.toggle('oculto',d.estado!=='REGISTRANDO');
  $('breg').disabled=d.estado==='REGISTRANDO';

  const C=$('comps');
  if(C.children.length!==d.comps.length){C.innerHTML='';d.comps.forEach(()=>{const e=document.createElement('div');e.className='cp';
    e.innerHTML='<div class="ic"></div><div class="n"></div><div class="r"></div><div class="d"></div>';C.appendChild(e);});}
  d.comps.forEach((c,i)=>{const e=C.children[i];e.classList.toggle('abierto',c.abierto);
    e.querySelector('.ic').textContent=c.abierto?'🔓':'🔒';
    e.querySelector('.n').textContent='Compartimento '+(i+1);
    e.querySelector('.r').textContent=c.abierto?('Abierto · '+c.restante+' s'):'';
    e.querySelector('.d').textContent=(c.duenos||'Sin dueño')+' · GPIO '+c.gpio;});

  const E=$('ev');E.innerHTML='';
  d.eventos.forEach(x=>{const r=document.createElement('div');const sp=document.createElement('span');
    const m=Math.floor(x.t/60),sg=x.t%60;sp.textContent=m+':'+String(sg).padStart(2,'0');
    r.appendChild(sp);r.appendChild(document.createTextNode(x.txt));E.appendChild(r);});
  if(!d.eventos.length)E.innerHTML='<div class="sub">Sin eventos</div>';

  $('fps').textContent=d.fps.toFixed(2);$('msc').textContent=d.msCaptura+' ms';
  $('msd').textContent=d.msDeteccion+' ms';$('msr').textContent=d.msReconocer+' ms';
  $('nreg').textContent=d.registradas+' / '+d.max;
  $('mem').textContent=(d.heap/1024|0)+' KB / '+(d.psram/1024|0)+' KB';
  $('cam').textContent=(d.camara?'OK':'FALLO')+' / '+d.erroresCam;$('red').textContent=d.red;
  if(!ajustesCargados){$('atiempo').value=d.tiempo;$('aangc').value=d.angC;$('aanga').value=d.angA;$('aluz').checked=d.luz;$('avflip').checked=d.vflip;$('aespejo').checked=d.espejo;ajustesCargados=true;}
  if(d.registradas!==ultimaCuenta||(ultimoProgreso>0&&d.progreso===0)){ultimaCuenta=d.registradas;cargarPersonas();}
  ultimoProgreso=d.progreso;
}

async function cargarPersonas(){
  try{const l=await (await pedir('/personas')).json();const T=$('tper');T.innerHTML='';
    l.forEach(p=>{const tr=document.createElement('tr');[String(p.id),p.nombre||('Persona '+p.id),p.comp>=0?String(p.comp+1):'-'].forEach(v=>{
      const td=document.createElement('td');td.textContent=v;tr.appendChild(td);});T.appendChild(tr);});
    if(!l.length)T.innerHTML='<tr><td colspan="3" class="sub">Nadie registrado todavía</td></tr>';
    $('cuenta').textContent='('+l.length+')';}catch(e){}
}

async function cicloEstado(){try{pintar(await (await pedir('/estado')).json());}catch(e){}setTimeout(cicloEstado,400);}
async function cicloFoto(){
  if(!preview){setTimeout(cicloFoto,400);return;}
  try{const r=await pedir('/foto',4000);
    if(r.ok){const c=r.headers.get('X-Cara');const bmp=await createImageBitmap(await r.blob());cx.drawImage(bmp,0,0,320,240);
      if(c&&c!=='none'){const [x1,y1,x2,y2,id]=c.split(',').map(Number);
        cx.strokeStyle=id>=0?'#28e07a':(id===-1?'#ff4d5e':'#ffc53d');cx.lineWidth=3;cx.strokeRect(x1,y1,x2-x1,y2-y1);}}}
  catch(e){}
  setTimeout(cicloFoto,120);
}
setInterval(()=>{const on=Date.now()-ultimoOK<3000;$('dot').className='dot'+(on?' on':'');$('cont').textContent=on?'conectado':'sin conexión';},500);

function entrar(ok){$('login').classList.toggle('oculto',ok);$('admin').classList.toggle('oculto',!ok);if(ok)cargarPersonas();}
$('bpin').onclick=async()=>{pin=$('pin').value;const d=await post('/verificar');
  if(d.ok){try{sessionStorage.setItem('pin',pin);}catch(e){}entrar(true);mensaje('Administración abierta');}};
$('breg').onclick=async()=>{const n=$('rnom').value.trim();if(!n){mensaje('Escribe un nombre',true);return;}
  const d=await post('/registrar',{nombre:n,comp:$('rcomp').value});if(d.ok)mensaje('Mírate en la cámara…');};
$('bcanc').onclick=async()=>{await post('/cancelar');mensaje('Reiniciando la ESP32…');};
$('bborrar').onclick=async()=>{if(!confirm('¿Borrar TODAS las caras registradas?'))return;
  const d=await post('/borrarTodo');if(d.ok){mensaje('Caras borradas');setTimeout(cargarPersonas,1500);}};
[1,2,3].forEach(i=>$('bp'+i).onclick=()=>post('/probar',{comp:i}));
$('bguardar').onclick=async()=>{const np=$('anpin').value;
  const d=await post('/ajustes',{tiempo:$('atiempo').value,angC:$('aangc').value,angA:$('aanga').value,luz:$('aluz').checked?1:0,nuevoPin:np});
  if(d.ok){if(np){pin=np;try{sessionStorage.setItem('pin',pin);}catch(e){}$('anpin').value='';}mensaje('Ajustes guardados');}};
['avflip','aespejo'].forEach(id=>$(id).onchange=async()=>{const d=await post('/ajustes',{vflip:$('avflip').checked?1:0,espejo:$('aespejo').checked?1:0});if(d.ok)mensaje('Orientación guardada');});
$('bprev').onclick=()=>{preview=!preview;$('bprev').textContent=preview?'⏹ OCULTAR CÁMARA':'📷 VER CÁMARA';
  if(!preview){cx.fillStyle='#000';cx.fillRect(0,0,320,240);}};

cx.fillStyle='#000';cx.fillRect(0,0,320,240);
if(pin){post('/verificar').then(d=>entrar(!!d.ok));}
cicloEstado();cicloFoto();
</script></body></html>)HTML";
