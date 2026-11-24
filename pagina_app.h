#pragma once
#include <Arduino.h>

// Aplicación principal: casilleros libres/ocupados, registro y verificación
// facial, cámara y administración. (Archivo aparte para que el preprocesador
// de Arduino no confunda el JavaScript con funciones C++.)

const char PAGINA_APP[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Casilleros faciales</title>
<style>
:root{--bg:#070b16;--card:#111a2e;--line:#22304f;--txt:#e8eefc;--dim:#8a9bc2;--ok:#28e07a;--warn:#ffc53d;--bad:#ff4d5e;--acc:#38bdf8;--or:#ff8a1f;--vio:#a78bfa}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(circle at 50% -20%,#1a2b55,var(--bg) 60%);color:var(--txt);font-family:system-ui,Arial,sans-serif;min-height:100vh}
header{padding:14px 16px;display:flex;align-items:center;justify-content:space-between;gap:10px}
h1{margin:0;font-size:22px;letter-spacing:1px} h1 b{color:var(--ok)} h1 i{color:var(--vio);font-style:normal}
.sub{color:var(--dim);font-size:12px}
.con{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--dim)}
.con a{color:var(--acc);text-decoration:none;font-weight:700;margin-right:8px}
.dot{width:12px;height:12px;border-radius:50%;background:var(--bad);box-shadow:0 0 8px var(--bad)}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.dot.lenta{background:var(--warn);box-shadow:0 0 8px var(--warn)}
main{padding:0 16px 24px;max-width:1100px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:14px}
h2{font-size:14px;margin:0 0 10px;color:var(--dim);text-transform:uppercase;letter-spacing:1px}
#banner{text-align:center;padding:16px 10px;border-radius:12px;transition:.25s}
#banner .t{font-size:26px;font-weight:900;line-height:1.15}
#banner .s{font-size:15px;margin-top:6px;opacity:.9}
.b-espera{background:#10304a;color:var(--acc)} .b-reg{background:#2a1d4a;color:var(--vio)} .b-ver{background:#3d3110;color:var(--warn)}
.b-ok{background:#0f3b26;color:var(--ok);animation:pulso 1s ease-in-out infinite alternate} .b-mal{background:#3d1218;color:var(--bad)}
@keyframes pulso{from{box-shadow:0 0 0 #28e07a00}to{box-shadow:0 0 28px #28e07a88}}
.prog{height:12px;background:#0b1324;border-radius:6px;overflow:hidden;margin-top:10px;position:relative}
.prog i{display:block;height:100%;width:0;transition:width .3s}
.prog .u{position:absolute;top:-2px;bottom:-2px;width:3px;background:#fff}
#bcanc{margin-top:10px}
.cas{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.cs{border:3px solid var(--line);border-radius:16px;padding:16px 10px;text-align:center;cursor:pointer;transition:.2s;user-select:none;background:#0b1324}
.cs:active{transform:scale(.97)}
.cs .ic{font-size:46px;line-height:1}
.cs .n{font-weight:900;font-size:20px;margin-top:6px}
.cs .e{font-weight:800;font-size:15px;margin-top:4px}
.cs .h{color:var(--dim);font-size:12px;margin-top:4px;min-height:15px}
.cs.libre{border-color:var(--ok)} .cs.libre .e{color:var(--ok)}
.cs.ocupado{border-color:var(--or)} .cs.ocupado .e{color:var(--or)}
.cs.abierto{border-color:var(--acc);box-shadow:0 0 18px #38bdf866} .cs.abierto .e{color:var(--acc)}
.cs.activo{outline:3px dashed var(--warn);outline-offset:3px}
.cs.bloq{opacity:.45;cursor:not-allowed}
.lib{margin-top:8px;width:100%;background:#4a1520;border-color:var(--bad);font-size:14px;padding:9px}
canvas{width:100%;display:block;border-radius:10px;background:#000}
button{background:#1b2a4a;color:var(--txt);border:1px solid #2f4677;border-radius:10px;padding:12px 14px;font-size:15px;font-weight:700;cursor:pointer}
button:active{transform:scale(.97)} button:disabled{opacity:.5}
.go{background:#0f5132;border-color:var(--ok)} .del{background:#4a1520;border-color:var(--bad)}
input,select{padding:11px;border-radius:10px;border:1px solid #2f4677;background:#060b17;color:var(--txt);font-size:16px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:10px}
.row label{color:var(--dim);font-size:13px;display:flex;flex-direction:column;gap:4px}
.row label.ck{flex-direction:row;align-items:center}
.ck input{width:20px;height:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
.st{background:#0b1324;border:1px solid var(--line);border-radius:10px;padding:8px}
.st small{display:block;color:var(--dim);font-size:11px}
.st b{font-size:18px;font-variant-numeric:tabular-nums}
#ev{max-height:220px;overflow-y:auto;overscroll-behavior:contain;padding-right:4px}
#ev div{padding:6px 0;border-bottom:1px solid var(--line);font-size:14px}
#ev span{color:var(--dim);font-size:12px;margin-right:8px;font-variant-numeric:tabular-nums}
#msg{min-height:20px;font-weight:700;margin-top:8px}
.zona{display:grid;grid-template-columns:1fr 1fr;gap:12px;align-items:start}
.izq{position:sticky;top:8px;z-index:2}
.zona .card{margin-bottom:12px}
@media (max-width:640px){
 main{padding:0 8px 24px}
 .zona{gap:8px}
 .zona .card{padding:10px}
 #banner{padding:10px 6px}
 #banner .t{font-size:16px}
 #banner .s{font-size:12px}
 .cas{grid-template-columns:1fr;gap:8px}
 .cs{padding:8px 6px;border-width:2px}
 .cs .ic{font-size:26px}
 .cs .n{font-size:15px;margin-top:2px}
 .cs .e{font-size:13px}
 .cs .h{font-size:11px}
 .izq .row .sub{display:none}
 .izq button{width:100%;padding:8px;font-size:13px}
}
.oculto{display:none}
</style></head><body>
<header>
 <div><h1>🔐 <b>CASILLEROS</b> <i>FACIALES</i></h1><div class="sub">Reconocimiento facial en la ESP32-CAM</div></div>
 <div class="con"><a href="/wifi">📶 Red</a><div id="dot" class="dot"></div><span id="cont">sin conexión</span></div>
</header>
<main>
 <div class="zona">
  <!-- Izquierda: cámara (fija al desplazarse) -->
  <div class="card izq">
   <h2>Cámara</h2>
   <canvas id="cv" width="320" height="240"></canvas>
   <div class="sub" style="margin-top:8px">La cámara solo se enciende al tocar un casillero. Ponte a 40–70 cm, de frente y con buena luz.</div>
  </div>

  <!-- Derecha: instrucciones + casilleros -->
  <div>
   <div class="card">
    <div id="banner" class="b-espera"><div class="t" id="bt">CONECTANDO…</div><div class="s" id="bs"></div>
     <div class="prog oculto" id="prog"><i id="progi"></i><div class="u oculto" id="progu"></div></div>
     <button class="oculto" id="bcanc">✖ Cancelar</button>
    </div>
   </div>
   <div class="card">
    <h2>Casilleros</h2>
    <div class="cas" id="cas"></div>
    <div class="sub" style="margin-top:10px">🟢 Libre: tócalo para quedártelo. 🟠 Ocupado: tócalo y mira a la cámara para abrirlo (al abrirse queda libre).</div>
   </div>
  </div>
 </div>

 <div class="card">
  <h2>Administración</h2>
  <div id="login" class="row">
   <input type="password" id="pin" placeholder="PIN (por defecto 1234)" inputmode="numeric" style="flex:1">
   <button class="go" id="bpin">Entrar</button>
  </div>
  <div id="admin" class="oculto">
   <h2 style="margin-top:6px">Casilleros</h2>
   <div class="row" id="adminCas"></div>
   <div class="sub" style="margin-top:6px">"Abrir" mueve el servo sin pedir cara (para probar). "Liberar" borra al dueño del casillero.</div>

   <h2 style="margin-top:18px">Ajustes</h2>
   <div class="row">
    <label>Tiempo abierto (s) <input type="number" id="atiempo" min="1" max="60" style="width:110px"></label>
    <label>Ángulo cerrado <input type="number" id="aangc" min="0" max="180" style="width:110px"></label>
    <label>Ángulo abierto <input type="number" id="aanga" min="0" max="180" style="width:110px"></label>
    <label>Umbral de parecido <input type="number" id="aumbral" min="0.40" max="0.90" step="0.01" style="width:110px"></label>
   </div>
   <div class="row">
    <label class="ck"><input type="checkbox" id="avflip"> Voltear vertical</label>
    <label class="ck"><input type="checkbox" id="aespejo"> Espejo</label>
   </div>
   <div class="row">
    <label>Nuevo PIN (opcional) <input type="password" id="anpin" maxlength="8" placeholder="4 a 8 caracteres"></label>
    <button class="go" id="bguardar">💾 Guardar ajustes</button>
   </div>
   <div class="sub" style="margin-top:6px">Umbral más alto = más estricto (menos errores con otras personas, pero puede no reconocerte). Por defecto 0.55.</div>
  </div>
  <div id="msg"></div>
 </div>

 <div class="card">
  <h2>Diagnóstico</h2>
  <div class="grid">
   <div class="st"><small>Análisis/s</small><b id="fps">-</b></div>
   <div class="st"><small>Captura</small><b id="msc">-</b></div>
   <div class="st"><small>Detección</small><b id="msd">-</b></div>
   <div class="st"><small>Huella facial</small><b id="msr">-</b></div>
   <div class="st"><small>Heap / PSRAM</small><b id="mem" style="font-size:14px">-</b></div>
   <div class="st"><small>Cámara / errores</small><b id="cam">-</b></div>
   <div class="st"><small>Red</small><b id="red" style="font-size:14px">-</b></div>
   <div class="st"><small>Encendida hace</small><b id="upt">-</b></div>
   <div class="st"><small>Último reinicio</small><b id="rein" style="font-size:13px">-</b></div>
   <div class="st"><small>Heap mínimo / bloque</small><b id="minh" style="font-size:14px">-</b></div>
   <div class="st"><small>Desconexiones red casa / AP</small><b id="desc" style="font-size:14px">-</b></div>
   <div class="st"><small>Pila libre (visión)</small><b id="pila" style="font-size:14px">-</b></div>
  </div>
  <div class="sub" style="margin-top:8px">Si "Encendida hace" vuelve a 0 sola, la ESP32 se reinició: mira "Último reinicio".</div>
 </div>

 <div class="card">
  <h2>Prueba de servos</h2>
  <div id="servoLock" class="sub">🔒 Entra con el PIN en "Administración" para usar estos botones.</div>
  <div class="grid oculto" id="servos"></div>
  <div class="sub" style="margin-top:8px">"Abrir" deja el servo abierto hasta que pulses "Cerrar" (se cierra solo a los 60 s). Usa los ángulos de Ajustes.</div>
 </div>

 <div class="card"><h2>Eventos</h2><div id="ev"></div></div>
</main>
<script>
const $=id=>document.getElementById(id);
const cv=$('cv'),cx=cv.getContext('2d');
let camDibujadaApagada=false,ultimoOK=0,pin='',ajustesCargados=false,est=null,ultUptime=-1,perdida=0;
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
function mensaje(t,mal){const m=$('msg');m.textContent=t;m.style.color=mal?'var(--bad)':'var(--ok)';clearTimeout(m._t);m._t=setTimeout(()=>m.textContent='',5000);
  if(mal){errBanner=Date.now()+4000;const b=$('banner');b.className='b-mal';$('bt').textContent='⚠ '+t;$('bs').textContent='';$('prog').classList.add('oculto');}}
let errBanner=0;

// ---- Banner de estado ----
function pintarBanner(d){
  if(Date.now()<errBanner&&d.op==='ninguna')return;   // dejar visible el último error
  const b=$('banner'),pr=$('prog'),pi=$('progi'),pu=$('progu');
  let cls='b-espera',t='👆 TOCA UN CASILLERO',s='Libre para quedártelo · Ocupado para abrirlo';
  pr.classList.add('oculto');pu.classList.add('oculto');
  if(d.op==='registro'){
    cls='b-reg';t='📸 REGISTRANDO · CASILLERO '+(d.opCas+1);
    s=(d.cara?(d.alineada?'¡Bien! Mueve un poco la cabeza':'Acércate y mira de frente'):'No veo tu cara: ponte frente a la cámara')+
      ' · Foto '+d.progreso+' de '+d.muestras+' · '+d.segundos+' s';
    pr.classList.remove('oculto');pi.style.background='var(--vio)';pi.style.width=(d.progreso/d.muestras*100)+'%';
  }else if(d.op==='verificar'){
    cls='b-ver';t='🔎 VERIFICANDO · CASILLERO '+(d.opCas+1);
    const p=d.sim>=0?Math.max(0,Math.round(d.sim*100)):null;
    s=(d.cara?(p!==null?('Parecido con el dueño: '+p+'% (necesita '+Math.round(d.umbral*100)+'%)'):'Analizando…'):'Mira a la cámara')+' · '+d.segundos+' s';
    pr.classList.remove('oculto');pu.classList.remove('oculto');pu.style.left=(d.umbral*100)+'%';
    pi.style.width=(p!==null?p:0)+'%';pi.style.background=(d.sim>=d.umbral)?'var(--ok)':'var(--warn)';
  }else if(d.resultado){
    cls=d.resultadoTipo===1?'b-ok':'b-mal';t=(d.resultadoTipo===1?'✅ ':'⛔ ')+d.resultado;s='';
  }
  b.className=cls;$('bt').textContent=t;$('bs').textContent=s;
  $('bcanc').classList.toggle('oculto',d.op==='ninguna');
}

// ---- Tarjetas de casilleros ----
function pintarCasilleros(d){
  const C=$('cas');
  if(C.children.length!==d.casilleros.length){C.innerHTML='';
    d.casilleros.forEach((c,i)=>{const e=document.createElement('div');e.className='cs';
      e.innerHTML='<div class="ic"></div><div class="n">Casillero '+(i+1)+'</div><div class="e"></div><div class="h"></div>';
      e.onclick=()=>tocar(i);C.appendChild(e);});}
  const ocupadaCam=d.op!=='ninguna';
  d.casilleros.forEach((c,i)=>{const e=C.children[i];
    e.className='cs '+(c.abierto?'abierto':(c.ocupado?'ocupado':'libre'))+(ocupadaCam&&d.opCas===i?' activo':'')+(ocupadaCam&&d.opCas!==i?' bloq':'');
    e.querySelector('.ic').textContent=c.abierto?'🔓':(c.ocupado?'🔒':'🟢');
    e.querySelector('.e').textContent=c.abierto?'ABIERTO':(c.ocupado?'OCUPADO':'LIBRE');
    e.querySelector('.h').textContent=c.abierto?'':(c.ocupado?'Toca para abrir con tu cara':'Toca para quedártelo');
    });
}

async function tocar(i){
  if(!est)return;const c=est.casilleros[i];
  if(est.op!=='ninguna'){mensaje('Espera a que termine la operación actual',true);return;}
  if(c.abierto){mensaje('Ese casillero ya está abierto',true);return;}
  const d=await post('/casillero',{cas:i+1});
  if(d.ok)window.scrollTo({top:0,behavior:'smooth'});
}

function pintar(d){
  if(ultUptime>=0&&d.uptime+5<ultUptime){mensaje('La ESP32 se reinició sola. Motivo: '+d.reinicio,true);}
  else if(perdida&&Date.now()-perdida>OFFLINE_MS){mensaje('Conexión recuperada (estuvo cortada '+Math.round((Date.now()-perdida)/1000)+' s)',false);}
  ultUptime=d.uptime;perdida=0;
  est=d;ultimoOK=Date.now();
  pintarBanner(d);pintarCasilleros(d);

  const E=$('ev'),firma=JSON.stringify(d.eventos);
  if(E._firma!==firma){E._firma=firma;const sc=E.scrollTop;E.innerHTML='';
  d.eventos.forEach(x=>{const r=document.createElement('div');const sp=document.createElement('span');
    const m=Math.floor(x.t/60),sg=x.t%60;sp.textContent=m+':'+String(sg).padStart(2,'0');
    r.appendChild(sp);r.appendChild(document.createTextNode(x.txt));E.appendChild(r);});
  if(!d.eventos.length)E.innerHTML='<div class="sub">Sin eventos</div>';
  E.scrollTop=sc;}

  const A=$('adminCas');
  if(A.children.length!==d.casilleros.length){A.innerHTML='';
    d.casilleros.forEach((c,i)=>{const w=document.createElement('div');w.style.cssText='display:flex;gap:6px;align-items:center';
      const t=document.createElement('b');t.textContent='#'+(i+1);
      const a=document.createElement('button');a.textContent='Abrir';a.onclick=()=>post('/probar',{cas:i+1});
      const l=document.createElement('button');l.className='del';l.textContent='Liberar';
      l.onclick=async()=>{if(!confirm('¿Liberar el casillero '+(i+1)+' (borrar a su dueño)?'))return;const r=await post('/forzarLibre',{cas:i+1});if(r.ok)mensaje('Casillero '+(i+1)+' liberado');};
      w.appendChild(t);w.appendChild(a);w.appendChild(l);A.appendChild(w);});}

  const S=$('servos');
  if(S.children.length!==d.casilleros.length){S.innerHTML='';
    d.casilleros.forEach((c,i)=>{const w=document.createElement('div');w.className='st';
      w.innerHTML='<small>Servo '+(i+1)+' · GPIO '+c.gpio+'</small><b class="se">-</b>'+
        '<div style="display:flex;gap:6px;margin-top:6px"><button class="go" style="flex:1;padding:8px">🔓 Abrir</button><button class="del" style="flex:1;padding:8px">🔒 Cerrar</button></div>';
      const [ba,bc]=w.querySelectorAll('button');
      ba.onclick=async()=>{const r=await post('/servo',{cas:i+1,accion:'abrir'});if(r.ok)mensaje('Servo '+(i+1)+' abierto');};
      bc.onclick=async()=>{const r=await post('/servo',{cas:i+1,accion:'cerrar'});if(r.ok)mensaje('Servo '+(i+1)+' cerrado');};
      S.appendChild(w);});}
  d.casilleros.forEach((c,i)=>{const b=S.children[i].querySelector('.se');
    b.textContent=c.abierto?('ABIERTO ('+c.restante+' s)'):'CERRADO';b.style.color=c.abierto?'var(--acc)':'var(--dim)';});

  $('fps').textContent=d.fps.toFixed(2);$('msc').textContent=d.msCaptura+' ms';
  $('msd').textContent=d.msDeteccion+' ms';$('msr').textContent=d.msReconocer+' ms';
  $('mem').textContent=(d.heap/1024|0)+' KB / '+(d.psram/1024|0)+' KB';
  $('cam').textContent=(d.camara?'OK':'FALLO')+' / '+d.erroresCam;$('red').textContent=d.red;
  $('upt').textContent=Math.floor(d.uptime/60)+' min '+(d.uptime%60)+' s';
  $('rein').textContent=d.reinicio;$('rein').style.color=/FALLA|ERROR|Watchdog/.test(d.reinicio)?'var(--bad)':'';
  $('minh').textContent=(d.minHeap/1024|0)+' KB / '+(d.maxBloque/1024|0)+' KB';
  $('pila').textContent=d.pilaLibre+' B';$('pila').style.color=d.pilaLibre<2048?'var(--bad)':'';
  if(d.heapDanado)mensaje('Memoria dañada después de: '+d.heapDanado,true);
  $('desc').textContent=d.descSta+' / '+d.descAp+(d.descSta?(' (razón '+d.razonSta+')'):'');
  if(!d.particion)mensaje('Falta la partición "casilleros": revisa partitions.csv',true);
  if(!ajustesCargados){$('atiempo').value=d.tiempo;$('aangc').value=d.angC;$('aanga').value=d.angA;$('aumbral').value=d.umbral;
    $('avflip').checked=d.vflip;$('aespejo').checked=d.espejo;ajustesCargados=true;}
}

// Conexión: "lenta" si no contesta en LENTA_MS, "sin conexión" solo tras OFFLINE_MS
// (el servidor de la ESP32 atiende de a uno y a veces se traba unos segundos).
const LENTA_MS=2500,OFFLINE_MS=9000;let latencia=0;
async function cicloEstado(){const t0=Date.now();
  try{const d=await (await pedir('/estado',{},7000)).json();latencia=Date.now()-t0;pintar(d);}catch(e){}
  setTimeout(cicloEstado,500);}
function camaraApagada(){
  cx.fillStyle='#050912';cx.fillRect(0,0,320,240);
  cx.fillStyle='#8a9bc2';cx.textAlign='center';
  cx.font='48px sans-serif';cx.fillText('📷',160,105);
  cx.font='bold 16px sans-serif';cx.fillText('Cámara apagada',160,145);
  cx.font='13px sans-serif';cx.fillText('Toca un casillero para encenderla',160,168);
  cx.textAlign='left';
}
async function cicloFoto(){
  if(!est||!est.camActiva){if(!camDibujadaApagada){camaraApagada();camDibujadaApagada=true;}setTimeout(cicloFoto,300);return;}
  camDibujadaApagada=false;
  try{const r=await pedir('/foto',{},4000);
    if(r.ok){const c=r.headers.get('X-Cara');const bmp=await createImageBitmap(await r.blob());cx.drawImage(bmp,0,0,320,240);
      if(c&&c!=='none'){const [x1,y1,x2,y2]=c.split(',').map(Number);
        let col='#38bdf8';
        if(est&&est.op==='verificar'&&est.sim>=0)col=est.sim>=est.umbral?'#28e07a':'#ff4d5e';
        else if(est&&est.op==='registro')col='#a78bfa';
        cx.strokeStyle=col;cx.lineWidth=3;cx.strokeRect(x1,y1,x2-x1,y2-y1);}}}
  catch(e){}
  setTimeout(cicloFoto,120);
}
setInterval(()=>{const dt=Date.now()-ultimoOK,on=ultimoOK&&dt<OFFLINE_MS,lenta=on&&dt>=LENTA_MS;
  $('dot').className='dot'+(on?(lenta?' lenta':' on'):'');
  $('cont').textContent=!ultimoOK?'conectando…':(!on?'sin conexión':(lenta?'lenta… '+Math.round(dt/1000)+' s':'conectado · '+latencia+' ms'));
  if(!on&&ultimoOK){if(!perdida)perdida=ultimoOK;const s=Math.round((Date.now()-perdida)/1000);
    $('banner').className='b-mal';$('bt').textContent='📡 SIN CONEXIÓN CON LA ESP32';
    $('bs').textContent='Reconectando… '+s+' s · Revisa que el celular siga en la misma red Wi-Fi';$('prog').classList.add('oculto');}},500);

function entrar(ok){$('login').classList.toggle('oculto',ok);$('admin').classList.toggle('oculto',!ok);
  $('servos').classList.toggle('oculto',!ok);$('servoLock').classList.toggle('oculto',ok);}
$('bpin').onclick=async()=>{pin=$('pin').value;const d=await post('/verificar');
  if(d.ok){try{sessionStorage.setItem('pin',pin);}catch(e){}entrar(true);mensaje('Administración abierta');}};
$('bcanc').onclick=()=>post('/cancelar');
$('bguardar').onclick=async()=>{const np=$('anpin').value;
  const d=await post('/ajustes',{tiempo:$('atiempo').value,angC:$('aangc').value,angA:$('aanga').value,umbral:$('aumbral').value,
    vflip:$('avflip').checked?1:0,espejo:$('aespejo').checked?1:0,nuevoPin:np});
  if(d.ok){if(np){pin=np;try{sessionStorage.setItem('pin',pin);}catch(e){}$('anpin').value='';}mensaje('Ajustes guardados');}};
['avflip','aespejo'].forEach(id=>$(id).onchange=async()=>{const d=await post('/ajustes',{vflip:$('avflip').checked?1:0,espejo:$('aespejo').checked?1:0});if(d.ok)mensaje('Orientación guardada');});

camaraApagada();
if(pin){(async()=>{const body=new URLSearchParams({pin:pin}).toString();
  try{const r=await pedir('/verificar',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body});entrar((await r.json()).ok);}catch(e){}})();}
cicloEstado();cicloFoto();
</script></body></html>)HTML";
