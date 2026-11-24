#pragma once
#include <Arduino.h>

// Pantalla inicial: elegir red Wi-Fi o trabajar directo en 192.168.4.1
// (Archivo aparte: el preprocesador de Arduino no confunde el JavaScript con funciones C++)

const char PAGINA_WIFI[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Casillero facial · Red</title>
<style>
:root{--bg:#070b16;--card:#111a2e;--line:#22304f;--txt:#e8eefc;--dim:#8a9bc2;--ok:#28e07a;--warn:#ffc53d;--bad:#ff4d5e;--acc:#38bdf8;--or:#ff8a1f}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(circle at 50% -20%,#1a2b55,var(--bg) 60%);color:var(--txt);font-family:system-ui,Arial,sans-serif;min-height:100vh}
main{padding:16px;max-width:560px;margin:auto}
h1{margin:6px 0 2px;font-size:26px;text-align:center;letter-spacing:1px} h1 b{color:var(--ok)} h1 i{color:var(--or);font-style:normal}
.sub{color:var(--dim);font-size:13px;text-align:center;margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:14px}
h2{font-size:14px;margin:0 0 10px;color:var(--dim);text-transform:uppercase;letter-spacing:1px;display:flex;justify-content:space-between;align-items:center}
button{background:#1b2a4a;color:var(--txt);border:1px solid #2f4677;border-radius:10px;padding:12px 14px;font-size:15px;font-weight:700;cursor:pointer}
button:active{transform:scale(.97)} button:disabled{opacity:.5}
.big{width:100%;padding:18px;font-size:17px}
.ap{background:#10304a;border-color:var(--acc)}
.go{background:#0f5132;border-color:var(--ok)}
.mini{padding:6px 10px;font-size:13px}
.net{border:1px solid var(--line);border-radius:10px;margin-bottom:8px;background:#0b1324;overflow:hidden}
.nh{display:flex;align-items:center;gap:10px;padding:12px;cursor:pointer}
.nh:active{background:#15213b}
.nn{flex:1;font-weight:700;word-break:break-all}
.bars{display:flex;gap:2px;align-items:flex-end;height:16px}
.bars i{width:4px;background:#2f4677;border-radius:1px}
.bars i.on{background:var(--ok)}
.form{padding:0 12px 12px;display:none}
.net.abierta .form{display:block}
input[type=password],input[type=text]{width:100%;padding:12px;border-radius:10px;border:1px solid #2f4677;background:#060b17;color:var(--txt);font-size:16px;margin:6px 0}
.chk{color:var(--dim);font-size:13px;display:flex;gap:6px;align-items:center;margin-bottom:8px}
#panel{display:none;text-align:center}
#panel .t{font-size:20px;font-weight:800;margin-bottom:6px}
.ip{font-size:28px;font-weight:900;color:var(--ok);margin:8px 0;word-break:break-all}
.err{color:var(--bad);font-weight:700}
.spin{display:inline-block;width:18px;height:18px;border:3px solid #2f4677;border-top-color:var(--acc);border-radius:50%;animation:g 1s linear infinite;vertical-align:middle}
@keyframes g{to{transform:rotate(360deg)}}
.saved{color:var(--dim);font-size:13px;margin-top:8px;display:flex;justify-content:space-between;align-items:center;gap:8px}
.col{display:flex;flex-direction:column;gap:10px;margin-top:12px}
a.btn{display:block;text-decoration:none;text-align:center}
</style></head><body>
<main>
 <h1>🔐 <b>CASILLERO</b> <i>FACIAL</i></h1>
 <div class="sub">¿Cómo quieres conectarte a la ESP32-CAM?</div>

 <div class="card" id="panel"></div>

 <div class="card">
  <h2>Opción 1 · Directo</h2>
  <button class="big ap" id="bap">📡 Trabajar directo en 192.168.4.1</button>
  <div class="sub" style="margin:8px 0 0">Sin router. El celular queda conectado a la red <b>ESP32CAM-CASILLERO</b>.</div>
 </div>

 <div class="card">
  <h2>Opción 2 · Mi red Wi-Fi <button class="mini" id="bbuscar">🔄 Buscar</button></h2>
  <div id="lista"><span class="spin"></span> Buscando redes…</div>
  <div class="net" id="otra">
   <div class="nh" id="otraH"><span>➕</span><span class="nn">Otra red (oculta)</span></div>
   <div class="form">
    <input type="text" id="ssidOtra" placeholder="Nombre de la red (SSID)" autocomplete="off">
    <input type="password" id="passOtra" placeholder="Contraseña (vacía si es abierta)">
    <button class="go" style="width:100%" id="bOtra">Conectar</button>
   </div>
  </div>
  <div class="sub" style="margin:8px 0 0">Solo redes de 2.4 GHz (el ESP32 no ve redes de 5 GHz).</div>
  <div class="saved" id="saved" style="display:none"><span id="savedTxt"></span><button class="mini" id="bolvidar">Olvidar</button></div>
 </div>
</main>
<script>
const $=id=>document.getElementById(id);
let redes=[],sondeo=null;
const esc=s=>String(s).replace(/[&<>\x22\x27]/g,c=>'&#'+c.charCodeAt(0)+';');

async function pedir(url,op={},ms=4000){
  const ac=new AbortController();const t=setTimeout(()=>ac.abort(),ms);
  try{const r=await fetch(url,Object.assign({signal:ac.signal,cache:'no-store'},op));clearTimeout(t);return r;}
  catch(e){clearTimeout(t);throw e;}
}
function post(url,datos){return pedir(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(datos||{}).toString()});}

function barras(rssi){const n=rssi>-55?4:rssi>-67?3:rssi>-78?2:1;let h='<span class="bars">';
  for(let i=1;i<=4;i++)h+='<i class="'+(i<=n?'on':'')+'" style="height:'+(i*4)+'px"></i>';return h+'</span>';}

function abrir(n){document.querySelectorAll('.net').forEach(x=>{if(x!==n)x.classList.remove('abierta');});
  n.classList.toggle('abierta');const inp=n.querySelector('input');if(inp&&n.classList.contains('abierta'))setTimeout(()=>inp.focus(),50);}

// La lista se construye con nodos DOM (los nombres de red nunca se interpretan como HTML)
function pintarLista(){
  const L=$('lista');L.innerHTML='';
  if(!redes.length){L.innerHTML='<div class="sub">No se encontraron redes. Pulsa 🔄 Buscar.</div>';return;}
  redes.forEach(r=>{
    const net=document.createElement('div');net.className='net';
    const h=document.createElement('div');h.className='nh';
    h.innerHTML=barras(r.rssi);
    const nn=document.createElement('span');nn.className='nn';nn.textContent=r.ssid;h.appendChild(nn);
    const lk=document.createElement('span');lk.textContent=r.abierta?'🔓':'🔒';h.appendChild(lk);
    net.appendChild(h);
    if(r.abierta){h.onclick=()=>conectar(r.ssid,'');}
    else{
      const f=document.createElement('div');f.className='form';
      const p=document.createElement('input');p.type='password';p.placeholder='Contraseña de '+r.ssid;
      p.onkeydown=e=>{if(e.key==='Enter')conectar(r.ssid,p.value);};
      const lb=document.createElement('label');lb.className='chk';
      const ck=document.createElement('input');ck.type='checkbox';ck.onchange=()=>{p.type=ck.checked?'text':'password';};
      lb.appendChild(ck);lb.appendChild(document.createTextNode(' Mostrar contraseña'));
      const b=document.createElement('button');b.className='go';b.style.width='100%';b.textContent='Conectar';
      b.onclick=()=>conectar(r.ssid,p.value);
      f.appendChild(p);f.appendChild(lb);f.appendChild(b);net.appendChild(f);
      h.onclick=()=>abrir(net);
    }
    L.appendChild(net);
  });
}

async function buscar(nuevo){
  $('lista').innerHTML='<span class="spin"></span> Buscando redes…';$('bbuscar').disabled=true;
  for(let k=0;k<25;k++){
    try{const r=await pedir('/redes'+(nuevo&&k===0?'?nuevo=1':''));const d=await r.json();
      if(!d.escaneando){redes=d.redes;break;}}catch(e){}
    await new Promise(r=>setTimeout(r,800));
  }
  $('bbuscar').disabled=false;pintarLista();
}

function panel(html){$('panel').style.display='block';$('panel').innerHTML=html;$('panel').scrollIntoView({behavior:'smooth'});}
function alertar(m){panel('<div class="err">⚠ '+esc(m||'Error')+'</div>');}

async function conectar(ssid,pass){
  ssid=(ssid||'').trim();
  if(!ssid){alertar('Escribe el nombre de la red');return;}
  if(pass.length>0&&pass.length<8){alertar('La contraseña Wi-Fi debe tener al menos 8 caracteres');return;}
  panel('<div class="t"><span class="spin"></span> Conectando a '+esc(ssid)+'…</div><div class="sub">Puede tardar hasta 15 s. Si la página se corta un momento, es normal.</div>');
  try{const r=await post('/conectar',{ssid:ssid,pass:pass});const d=await r.json();if(!d.ok){alertar(d.error);return;}}
  catch(e){}
  vigilar();
}

function vigilar(){clearInterval(sondeo);sondeo=setInterval(revisar,1000);revisar();}
async function revisar(){
  let d;try{d=await (await pedir('/estadoRed',{},2500)).json();}catch(e){return;}
  mostrarGuardada(d);
  if(d.sta==='conectando'){panel('<div class="t"><span class="spin"></span> Conectando a '+esc(d.ssid)+'… '+d.segundos+' s</div><div class="sub">Si la página se corta un momento, es normal.</div>');return;}
  clearInterval(sondeo);
  if(d.sta==='conectado'&&d.conectado){
    const url='http://'+d.ip+'/';const aqui=location.hostname===d.ip;
    const urlMdns=d.mdns?('http://'+d.mdns+'/'):'';
    panel('<div class="t">✅ ¡Conectado a '+esc(d.ssid)+'!</div><div class="sub">La ESP32-CAM ya tiene esta dirección en tu red:</div>'+
      '<div class="ip">'+esc(d.ip)+'</div>'+
      (d.mdns?'<div class="sub" style="margin:0">también se puede acceder desde</div><div class="ip" style="font-size:24px">'+esc(urlMdns)+'</div>'+
        '<div class="sub">(funciona en PC e iPhone; en algunos Android solo funciona la IP)</div>':'')+
      '<div class="sub">Conecta tu celular a <b>'+esc(d.ssid)+'</b> y abre cualquiera de las dos direcciones. '+
      'La próxima vez se conectará sola. El modo directo 192.168.4.1 sigue disponible como respaldo.</div>'+
      '<div class="col"><a class="btn" href="'+url+'"><button class="big go" style="width:100%">🔐 Abrir '+esc(d.ip)+'</button></a>'+
      (d.mdns?'<a class="btn" href="'+esc(urlMdns)+'"><button class="big go" style="width:100%">🔐 Abrir '+esc(urlMdns)+'</button></a>':'')+
      (aqui?'':'<a class="btn" href="/app"><button class="big" style="width:100%">Seguir aquí en 192.168.4.1</button></a>')+'</div>');
  }else if(d.sta==='error'){
    alertar(d.error+' ('+d.ssid+')');
  }
}
function mostrarGuardada(d){
  if(d.guardada&&d.modo==='local'){$('saved').style.display='flex';$('savedTxt').textContent='Red guardada: '+d.guardada;}
  else $('saved').style.display='none';
}

$('otraH').onclick=()=>abrir($('otra'));
$('bOtra').onclick=()=>conectar($('ssidOtra').value,$('passOtra').value);
$('bap').onclick=async()=>{try{await post('/modoAP');}catch(e){}location.href='/app';};
$('bbuscar').onclick=()=>buscar(true);
$('bolvidar').onclick=async()=>{try{await post('/olvidar');}catch(e){}$('saved').style.display='none';$('panel').style.display='none';};

(async()=>{
  try{const d=await (await pedir('/estadoRed')).json();mostrarGuardada(d);
    if(d.sta==='conectando'||d.sta==='error'||(d.sta==='conectado'&&d.conectado))vigilar();}catch(e){}
  buscar(true);
})();
</script></body></html>)HTML";
