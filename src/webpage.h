#pragma once
#include <Arduino.h>

// Web-Frontend (Single Page, Vanilla JS). Spricht die JSON-API unter /api/* an.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DB-Tafel</title>
<style>
:root{--bg:#f2f3f5;--card:#fff;--fg:#1d1f23;--mut:#6b7078;--bd:#d9dce1;--acc:#ec0016;--ok:#0a7d32;--in:#fff}
@media(prefers-color-scheme:dark){:root{--bg:#131518;--card:#1d2024;--fg:#e8e9eb;--mut:#9aa0a8;--bd:#34383e;--in:#25292e}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header{background:var(--acc);color:#fff;padding:12px 16px;display:flex;align-items:center;gap:10px}
header h1{font-size:18px;margin:0;flex:1}main{max-width:900px;margin:0 auto;padding:12px 16px 90px}
section{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px 16px;margin:12px 0}
h2{font-size:16px;margin:0 0 10px}h3{font-size:14px;margin:0}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:10px 14px}
label{display:flex;flex-direction:column;font-size:12px;color:var(--mut);gap:3px}
label.ck{flex-direction:row;align-items:center;gap:6px;font-size:14px;color:var(--fg)}
input,select{font:inherit;padding:7px 8px;border:1px solid var(--bd);border-radius:6px;background:var(--in);color:var(--fg);width:100%}
input[type=checkbox]{width:auto}
button{font:inherit;padding:7px 14px;border-radius:6px;border:1px solid var(--bd);background:var(--in);color:var(--fg);cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button.sm{padding:4px 10px;font-size:13px}
.prof{border:1px solid var(--bd);border-radius:8px;padding:12px;margin:10px 0}
.prof .hd{display:flex;align-items:center;gap:10px;margin-bottom:10px}.prof .hd h3{flex:1}
.row{display:flex;gap:8px;align-items:flex-end;flex-wrap:wrap}.row>*{flex:1;min-width:140px}
.chips{display:flex;flex-wrap:wrap;gap:4px 12px;margin-top:4px}
.mut{color:var(--mut);font-size:13px}.err{color:var(--acc)}.okc{color:var(--ok)}
table{border-collapse:collapse;width:100%;font-size:13px}td,th{padding:3px 6px;border-bottom:1px solid var(--bd);text-align:left}
.bar{position:fixed;left:0;right:0;bottom:0;background:var(--card);border-top:1px solid var(--bd);padding:10px 16px;display:flex;gap:10px;justify-content:center;align-items:center}
#msg{font-size:14px}.res{max-height:180px;overflow:auto;border:1px solid var(--bd);border-radius:6px;margin-top:6px}
.res div{padding:5px 8px;cursor:pointer;border-bottom:1px solid var(--bd)}.res div:hover{background:var(--bg)}
.tgts{max-height:220px;overflow:auto;border:1px solid var(--bd);border-radius:6px;padding:6px 8px;margin-top:6px}
.badge{display:inline-block;padding:0 5px;border-radius:3px;background:#666;color:#fff;font-weight:600}
</style></head><body>
<header><h1>DB-Tafel</h1><span id="ver" style="opacity:.8;font-size:13px"></span></header>
<main>
<section><h2>Status</h2><div id="status" class="mut">lade ...</div></section>

<section><h2>Haltestellen &amp; Zeitplan</h2>
<p class="mut">Ein Profil wird angezeigt, wenn Wochentag und Uhrzeit passen. Sind mehrere Profile gleichzeitig aktiv, wechselt die Anzeige zwischen ihnen. Beispiel: Abfahrten <i>06:00&ndash;12:00</i>, Ankünfte <i>12:00&ndash;20:00</i>. Gleiche Von/Bis-Zeit = ganztägig.</p>
<div id="profiles"></div>
<button id="addProf">+ Profil hinzufügen</button>
</section>

<section><h2>Anzeige</h2><div class="grid">
<label>Layout<select data-k="display.layout" data-n><option value="0">Kompakt (1 Zeile je Fahrt)</option><option value="1">Ausführlich (2 Zeilen, Gleis, Verspätung)</option></select></label>
<label>Schrift<select data-k="display.font" data-n><option value="0">Klein 3x5 (mehr Text)</option><option value="1">Normal 5x7</option></select></label>
<label>Zeitangabe<select data-k="display.timeMode" data-n><option value="0">Uhrzeit (Plan)</option><option value="1">Minuten bis Abfahrt</option></select></label>
<label>Abfrageintervall (s)<input type="number" min="30" max="3600" data-k="display.refreshSec"></label>
<label>Seitenwechsel (s)<input type="number" min="2" max="120" data-k="display.pageSec"></label>
<label>Helligkeit (0&ndash;255)<input type="number" min="0" max="255" data-k="display.brightness"></label>
<label class="ck"><input type="checkbox" data-k="display.scroll">Lange Texte scrollen</label>
<label class="ck"><input type="checkbox" data-k="display.hideCancelled">Ausfälle ausblenden</label>
</div>
<h3 style="margin-top:14px">Nachtmodus</h3><div class="grid" style="margin-top:6px">
<label class="ck"><input type="checkbox" data-k="display.nightEnabled">aktiv</label>
<label>von<input type="time" data-k="display.nightFrom"></label>
<label>bis<input type="time" data-k="display.nightTo"></label>
<label>Helligkeit nachts (0 = aus)<input type="number" min="0" max="255" data-k="display.nightBrightness"></label>
</div>
<h3 style="margin-top:14px">Lichtsensor (LDR)</h3>
<p class="mut">Die Helligkeit folgt dem Umgebungslicht: bei Dunkelheit „Min. Helligkeit“, bei heller Umgebung die Helligkeit von oben. Der Nachtmodus hat Vorrang. Zum Kalibrieren den Raum abdunkeln bzw. hell machen, ein paar Sekunden warten und den Messwert übernehmen.</p>
<div class="grid" style="margin-top:6px">
<label class="ck"><input type="checkbox" data-k="display.ldrEnabled">aktiv</label>
<label>GPIO (ADC1: 32&ndash;39)<input type="number" min="32" max="39" data-k="display.ldrPin"></label>
<label class="ck"><input type="checkbox" data-k="display.ldrInvert">invertiert (hell = kleiner Wert)</label>
<label>Min. Helligkeit (dunkel)<input type="number" min="0" max="255" data-k="display.ldrMinBright"></label>
<label>Messwert dunkel<input type="number" min="0" max="4095" data-k="display.ldrDark"></label>
<label>Messwert hell<input type="number" min="0" max="4095" data-k="display.ldrBright"></label>
</div>
<div class="row" style="margin-top:8px;align-items:center">
<div class="mut" id="ldrNow" style="flex:2">Aktueller Messwert: &ndash;</div>
<div style="flex:0"><button type="button" class="sm" id="ldrSetDark">als „dunkel“</button></div>
<div style="flex:0"><button type="button" class="sm" id="ldrSetBright">als „hell“</button></div>
</div></section>

<section><h2>WLAN</h2><div class="grid">
<label>SSID<input data-k="wifi.ssid" list="ssids" autocomplete="off"><datalist id="ssids"></datalist></label>
<label>Passwort<input type="password" data-k="wifi.pass" data-secret autocomplete="new-password"></label>
<label>Hostname (mDNS: name.local)<input data-k="wifi.hostname"></label>
<label class="ck"><input type="checkbox" data-k="wifi.ipv6">IPv6 verwenden</label>
<label>&nbsp;<button id="scan" type="button">Netzwerke suchen</button></label>
</div><p class="mut">Ohne Verbindung startet nach 60 s der Setup-Access-Point <b id="apname"></b> (IP 192.168.4.1).</p></section>

<section><h2>Zeit / NTP</h2><div class="grid">
<label>NTP-Server 1<input data-k="time.ntp1"></label>
<label>NTP-Server 2<input data-k="time.ntp2"></label>
<label>Zeitzone (POSIX)<input data-k="time.tz" list="tzs"><datalist id="tzs">
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Mitteleuropa (Berlin, Wien, Zürich)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">London</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Osteuropa</option>
<option value="UTC0">UTC</option></datalist></label>
</div></section>

<section><h2>HUB75-Panel</h2><p class="mut">Änderungen hier führen zu einem Neustart. Standardwerte = ESP32 Trinity mit 64x64-Panel (E an GPIO18).</p>
<div class="grid">
<label>Breite je Panel (px)<input type="number" data-k="panel.width"></label>
<label>Höhe (px)<input type="number" data-k="panel.height"></label>
<label>Anzahl Panels (Kette)<input type="number" min="1" max="8" data-k="panel.chain"></label>
<label>Treiber-IC<select data-k="panel.driver" data-n><option value="0">Standard (SHIFTREG)</option><option value="1">FM6124</option><option value="2">FM6126A</option><option value="3">ICN2038S</option><option value="4">MBI5124</option><option value="5">DP3246</option></select></label>
<label>Zeilen-Decoder<select data-k="panel.lineDriver" data-n><option value="0">Standard (74HC138)</option><option value="1">74HC595 / SM5368</option><option value="2">Direkt</option><option value="3">SM5266P</option></select></label>
<label>I2S-Takt<select data-k="panel.i2sMHz" data-n><option value="8">8 MHz</option><option value="10">10 MHz</option><option value="15">15 MHz</option><option value="20">20 MHz</option></select></label>
<label>Latch-Blanking (1&ndash;4)<input type="number" min="1" max="4" data-k="panel.latchBlanking"></label>
<label>Min. Bildwiederholrate (Hz)<input type="number" min="30" max="240" data-k="panel.minRefresh"></label>
<label class="ck"><input type="checkbox" data-k="panel.clkPhase">Clock-Phase</label>
<label class="ck"><input type="checkbox" data-k="panel.doubleBuffer">Double-Buffer</label>
</div>
<h3 style="margin-top:14px">Pinbelegung (GPIO, -1 = nicht verwendet)</h3>
<div class="grid" id="pins" style="margin-top:6px;grid-template-columns:repeat(auto-fill,minmax(80px,1fr))"></div>
<button type="button" class="sm" id="pinDef" style="margin-top:8px">Trinity-Standard</button>
</section>

<section><h2>System</h2><div class="grid">
<label>Web-Passwort (Benutzer admin, leer = aus)<input type="password" data-k="web.pass" data-secret autocomplete="new-password"></label>
<label>Passwort Setup-AP (min. 8 Zeichen)<input type="password" data-k="wifi.apPass" data-secret autocomplete="new-password"></label>
</div>
<div class="row" style="margin-top:12px">
<label>Firmware-Update (firmware.bin)<input type="file" id="fw" accept=".bin"></label>
<div style="flex:0"><button type="button" id="fwUp">Hochladen</button></div>
<div style="flex:0"><button type="button" id="reboot">Neustart</button></div>
</div><div id="fwMsg" class="mut"></div></section>
</main>
<div class="bar"><span id="msg"></span><button class="pri" id="save">Speichern</button></div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const PROD=[["ICE",512],["IC/EC",256],["IR/D",128],["RE/RB",64],["S-Bahn",32],["Bus",16],["Schiff",8],["U-Bahn",4],["Tram",2],["Ruf/AST",1]];
const DAYS=["Mo","Di","Mi","Do","Fr","Sa","So"];
const PINS=["r1","g1","b1","r2","g2","b2","a","b","c","d","e","lat","oe","clk"];
const TRINITY=[25,26,27,14,12,13,23,19,5,17,18,4,15,16];
let C=null,P=[],LDR=-1;
const esc=s=>String(s??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const get=(o,p)=>p.split(".").reduce((a,k)=>a?.[k],o);
const set=(o,p,v)=>{const k=p.split(".");let a=o;k.slice(0,-1).forEach(x=>a=a[x]??=( {}));a[k.at(-1)]=v};
function msg(t,cls){const m=$("#msg");m.textContent=t;m.className=cls||""}
async function api(u,o){const r=await fetch(u,o);const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||r.status);return j}

function newProfile(){return{enabled:true,label:"",stationName:"",stationId:"",type:"dep",days:127,from:"00:00",to:"00:00",products:1023,leadMin:0,maxCount:10,hours:1,dirs:""}}

function renderProfiles(){
  const box=$("#profiles");box.innerHTML="";
  P.forEach((p,i)=>{
    const d=document.createElement("div");d.className="prof";
    d.innerHTML=`<div class="hd"><label class="ck"><input type="checkbox" data-f="enabled" ${p.enabled?"checked":""}></label>
<h3>Profil ${i+1}${p.stationName?": "+esc(p.stationName):""}</h3>
<button class="sm" data-a="up" ${i==0?"disabled":""}>&uarr;</button><button class="sm" data-a="del">Löschen</button></div>
<div class="row"><label>Haltestelle suchen<input data-a="q" placeholder="z.B. Hannover Hbf" value=""></label>
<div style="flex:0"><button class="sm" data-a="search">Suchen</button></div></div>
<div class="res" hidden></div>
<div class="grid" style="margin-top:8px">
<label>Haltestelle<input data-f="stationName" value="${esc(p.stationName)}"></label>
<label>Anzeigename (optional)<input data-f="label" maxlength="23" value="${esc(p.label)}" placeholder="z.B. Abf. Hbf"></label>
<label>Art<select data-f="type"><option value="dep">Abfahrten</option><option value="arr">Ankünfte</option></select></label>
<label>von<input type="time" data-f="from" value="${p.from}"></label>
<label>bis<input type="time" data-f="to" value="${p.to}"></label>
<label>Fußweg/Vorlauf (min)<input type="number" min="0" max="120" data-f="leadMin" value="${p.leadMin}"></label>
<label>Max. Fahrten<input type="number" min="1" max="${C.info.maxEntries}" data-f="maxCount" value="${p.maxCount}"></label>
<label>Zeitraum (h)<select data-f="hours"><option>1</option><option>2</option><option>3</option></select></label>
</div>
<div class="row" style="margin-top:8px"><label style="flex:3"><span data-dirlbl></span><input data-f="dirs" maxlength="119" value="${esc(p.dirs)}" placeholder="leer = alle, z.B. Hamburg; Altona"></label>
<div style="flex:0"><button class="sm" data-a="tgt">Ziele abrufen</button></div></div>
<div class="tgts chips" hidden></div>
<div class="mut" style="margin-top:8px">Wochentage</div><div class="chips">${DAYS.map((n,b)=>`<label class="ck"><input type="checkbox" data-day="${b}" ${p.days&(1<<b)?"checked":""}>${n}</label>`).join("")}</div>
<div class="mut" style="margin-top:8px">Verkehrsmittel</div><div class="chips">${PROD.map(([n,v])=>`<label class="ck"><input type="checkbox" data-prod="${v}" ${p.products&v?"checked":""}>${n}</label>`).join("")}</div>
<details style="margin-top:8px"><summary class="mut">Stations-ID</summary><input data-f="stationId" value="${esc(p.stationId)}" style="font-size:12px"></details>`;
    d.querySelector('[data-f=type]').value=p.type;
    d.querySelector('[data-f=hours]').value=String(p.hours);
    const tp=d.querySelector('[data-f=type]'),dl=d.querySelector('[data-dirlbl]');
    const dirLbl=()=>dl.textContent=(tp.value=="arr"?"Nur Herkunft":"Nur Richtung / Ziel")+" (Teilbegriffe, mehrere mit ; trennen)";
    dirLbl();tp.addEventListener("change",dirLbl);
    const dirs=d.querySelector('[data-f=dirs]'),tg=d.querySelector('.tgts');
    const toks=()=>dirs.value.split(";").map(s=>s.trim()).filter(Boolean);
    d.querySelector('[data-a=tgt]').onclick=async()=>{
      const id=d.querySelector('[data-f=stationId]').value.trim();tg.hidden=false;
      if(!id){tg.innerHTML='<span class="err">Zuerst eine Haltestelle wählen</span>';return}
      let prod=0;d.querySelectorAll("[data-prod]").forEach(e=>{if(e.checked)prod|=Number(e.dataset.prod)});
      const u='/api/targets?id='+encodeURIComponent(id)+'&type='+tp.value+'&products='+(prod||1023);
      tg.innerHTML='<span class="mut">rufe Fahrten der nächsten 2 h ab ...</span>';
      try{let j=await api(u);
        for(let n=0;j.pending&&n<45;n++){await new Promise(r=>setTimeout(r,2000));j=await api(u)}
        if(j.pending)throw new Error("Zeitüberschreitung");
        if(!j.targets.length){tg.innerHTML='<span class="mut">Keine Fahrten gefunden'+(j.error?" ("+esc(j.error)+")":"")+'</span>';return}
        const cur=toks().map(s=>s.toLowerCase());
        j.targets.sort((a,b)=>a.name.localeCompare(b.name));
        tg.innerHTML=j.targets.map(t=>`<label class="ck"><input type="checkbox" data-t="${esc(t.name)}" ${cur.includes(t.name.toLowerCase())?"checked":""}>${esc(t.name)} <span class="mut">${esc(t.lines)}</span></label>`).join("");
        tg.querySelectorAll("[data-t]").forEach(c=>c.onchange=()=>{const t=c.dataset.t,l=toks().filter(s=>s.toLowerCase()!=t.toLowerCase());if(c.checked)l.push(t);dirs.value=l.join("; ")});
      }catch(e){tg.innerHTML='<span class="err">Fehler: '+esc(e.message)+'</span>'}};
    d.querySelector('[data-a=del]').onclick=()=>{readProfiles();P.splice(i,1);renderProfiles()};
    d.querySelector('[data-a=up]').onclick=()=>{readProfiles();[P[i-1],P[i]]=[P[i],P[i-1]];renderProfiles()};
    const q=d.querySelector('[data-a=q]'),res=d.querySelector('.res');
    const search=async()=>{
      if(q.value.trim().length<2)return;res.hidden=false;res.innerHTML='<div class="mut">suche ...</div>';
      try{const u='/api/stations?q='+encodeURIComponent(q.value.trim());let j=await api(u);
        for(let n=0;j.pending&&n<45;n++){await new Promise(r=>setTimeout(r,2000));j=await api(u)}
        if(j.pending)throw new Error("Zeitüberschreitung");
        if(!j.stations.length){res.innerHTML='<div class="mut">Nichts gefunden'+(j.error?" ("+esc(j.error)+")":"")+'</div>';return}
        res.innerHTML="";j.stations.forEach(s=>{const e=document.createElement("div");e.textContent=s.name;
          e.onclick=()=>{d.querySelector('[data-f=stationName]').value=s.name;d.querySelector('[data-f=stationId]').value=s.id;res.hidden=true;d.querySelector('h3').textContent=`Profil ${i+1}: ${s.name}`};res.appendChild(e)})
      }catch(e){res.innerHTML='<div class="err">Fehler: '+esc(e.message)+'</div>'}};
    d.querySelector('[data-a=search]').onclick=search;q.onkeydown=e=>{if(e.key=="Enter"){e.preventDefault();search()}};
    box.appendChild(d);
  });
  $("#addProf").disabled=P.length>=C.info.maxProfiles;
}
function readProfiles(){
  $$("#profiles .prof").forEach((d,i)=>{const p=P[i];
    d.querySelectorAll("[data-f]").forEach(e=>{const f=e.dataset.f;p[f]=e.type=="checkbox"?e.checked:(e.type=="number"||f=="hours")?Number(e.value):e.value});
    p.days=0;d.querySelectorAll("[data-day]").forEach(e=>{if(e.checked)p.days|=1<<e.dataset.day});
    p.products=0;d.querySelectorAll("[data-prod]").forEach(e=>{if(e.checked)p.products|=Number(e.dataset.prod)});
  });
}
function render(){
  $("#pins").innerHTML=PINS.map(n=>`<label>${n.toUpperCase()}<input type="number" min="-1" max="39" data-k="panel.pins.${n}"></label>`).join("");
  $$("[data-k]").forEach(e=>{const v=get(C,e.dataset.k);
    if(e.dataset.secret!==undefined){e.value="";e.dataset.dirty="";const s=get(C,e.dataset.k+"Set");e.placeholder=s?"(unverändert)":"(leer)";e.oninput=()=>e.dataset.dirty=1;return}
    if(e.type=="checkbox")e.checked=!!v;else e.value=v??""});
  P=C.profiles.map(p=>({...newProfile(),...p}));renderProfiles();
  $("#ver").textContent="v"+C.info.version;
}
function collect(){
  const o={};
  $$("[data-k]").forEach(e=>{const k=e.dataset.k;
    if(e.dataset.secret!==undefined){if(e.dataset.dirty)set(o,k,e.value);return}
    set(o,k,e.type=="checkbox"?e.checked:(e.type=="number"||e.dataset.n!==undefined)?Number(e.value):e.value)});
  readProfiles();o.profiles=P;return o;
}
async function load(){try{C=await api('/api/config');render()}catch(e){msg("Laden fehlgeschlagen: "+e.message,"err")}}
async function save(){
  msg("speichere ...");
  try{const j=await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())});
    if(j.reboot){msg("Gespeichert – Gerät startet neu ...","okc");setTimeout(()=>location.reload(),9000)}
    else{msg("Gespeichert","okc");await load();setTimeout(status,3000)}
  }catch(e){msg("Fehler: "+e.message,"err")}
}
async function status(){
  try{const s=await api('/api/status');const w=s.wifi;$("#apname").textContent=w.apSsid;
    let h=`<div>${s.timeValid?esc(s.time):'<span class="err">Zeit nicht synchronisiert</span>'} &middot; `+
      (w.connected?`<span class="okc">WLAN ${esc(w.ssid)}</span> ${esc(w.ip)} (${w.rssi} dBm) &middot; ${esc(w.hostname)}.local`:'<span class="err">WLAN nicht verbunden</span>')+
      (w.ap?` &middot; Setup-AP <b>${esc(w.apSsid)}</b> aktiv`:"")+` &middot; Heap ${Math.round(s.heap/1024)} kB &middot; Laufzeit ${Math.floor(s.uptime/3600)} h ${Math.floor(s.uptime/60)%60} min</div>`;
    LDR=s.ldr;$("#ldrNow").textContent=s.ldr<0?"Lichtsensor aus (aktivieren + speichern, dann erscheint der Messwert)":`Aktueller Messwert: ${s.ldr} · Panel-Helligkeit: ${s.brightness}`;
    h+=s.ldr>=0?`<div class="mut">Lichtsensor ${s.ldr} &rarr; Helligkeit ${s.brightness}</div>`:"";
    const act=s.boards.filter(b=>b.active);
    if(!act.length)h+='<p>Kein Profil aktiv &ndash; die Tafel zeigt die Uhr.</p>';
    act.forEach(b=>{const p=C?.profiles[b.idx];
      h+=`<p style="margin:10px 0 4px"><b>${esc(p?.label||p?.stationName||"Profil "+(b.idx+1))}</b> (${p?.type=="arr"?"Ankünfte":"Abfahrten"}${p?.dirs?(p.type=="arr"?" aus ":" nach ")+esc(p.dirs):""}) <span class="mut">`+
        (b.age<0?"noch nicht abgerufen":"vor "+b.age+" s abgerufen")+`</span> ${b.error?'<span class="err">'+esc(b.error)+'</span>':""}</p>`;
      if(b.entries.length)h+='<table><tr><th>Zeit</th><th>Linie</th><th>'+(p?.type=="arr"?"Von":"Nach")+'</th><th>Gleis</th></tr>'+b.entries.map(e=>
        `<tr><td>${e.time}${e.cancelled?' <span class="err">fällt aus</span>':e.delay>0?' <span class="err">+'+e.delay+'</span>':''}</td><td><span class="badge">${esc(e.line)}</span></td><td>${esc(e.target)}</td><td>${esc(e.platform)}</td></tr>`).join("")+'</table>';
    });
    $("#status").innerHTML=h;
  }catch(e){$("#status").innerHTML='<span class="err">Gerät nicht erreichbar</span>'}
}
$("#addProf").onclick=()=>{readProfiles();P.push(newProfile());renderProfiles()};
$("#save").onclick=save;
const ldrTake=k=>{if(LDR<0){msg("Kein Messwert – Lichtsensor aktivieren und speichern","err");return}$(`[data-k="display.${k}"]`).value=LDR;msg("Übernommen – Speichern nicht vergessen")};
$("#ldrSetDark").onclick=()=>ldrTake("ldrDark");$("#ldrSetBright").onclick=()=>ldrTake("ldrBright");
$("#pinDef").onclick=()=>PINS.forEach((n,i)=>$(`[data-k="panel.pins.${n}"]`).value=TRINITY[i]);
$("#scan").onclick=async()=>{const b=$("#scan");b.disabled=true;b.textContent="suche ...";
  try{const j=await api('/api/scan');$("#ssids").innerHTML=j.networks.map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm${n.open?" offen":""}</option>`).join("");
    b.textContent=j.networks.length+" gefunden – SSID-Feld anklicken"}catch(e){b.textContent="Fehler"}b.disabled=false};
$("#reboot").onclick=async()=>{if(!confirm("Gerät neu starten?"))return;await api('/api/reboot',{method:'POST'}).catch(()=>{});msg("Neustart ...");setTimeout(()=>location.reload(),9000)};
$("#fwUp").onclick=()=>{const f=$("#fw").files[0];if(!f)return;const fd=new FormData();fd.append("firmware",f,f.name);
  const x=new XMLHttpRequest();x.open("POST","/update");x.upload.onprogress=e=>$("#fwMsg").textContent="Upload "+Math.round(e.loaded*100/e.total)+" %";
  x.onload=()=>{$("#fwMsg").textContent=x.status==200?"Update erfolgreich, Neustart ...":"Update fehlgeschlagen";if(x.status==200)setTimeout(()=>location.reload(),12000)};x.send(fd)};
load().then(status);setInterval(status,10000);
</script></body></html>)rawliteral";
