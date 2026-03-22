#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

extern AsyncWebServer server;
extern Config config;
extern AccessCode accessCodes[];
extern int accessCodeCount;

void setupWebServer();
void loopWebSocket();

// Page HTML principale
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Volet Roulant ESP32</title>
  <style>
    :root {
      --primary: #1a237e;
      --accent: #1976d2;
      --success: #2e7d32;
      --danger: #c62828;
      --warn: #e65100;
      --bg: #f0f4f8;
      --card: #ffffff;
      --border: #dde3ea;
      --text: #1c2a3e;
      --muted: #607d8b;
    }
    * { margin:0; padding:0; box-sizing:border-box; }
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background:var(--bg); color:var(--text); min-height:100vh; }
    .wrap { max-width:1100px; margin:20px auto; background:var(--card); border-radius:16px; box-shadow: 0 8px 32px rgba(0,0,0,.12); overflow:hidden; }
    header { background:linear-gradient(135deg, #1a237e, #1976d2); color:#fff; padding:28px 32px; }
    header h1 { font-size:1.7em; font-weight:700; }
    header p { opacity:.8; margin-top:6px; font-size:.95em; }
    nav { display:flex; background:#f8fafc; border-bottom:2px solid var(--border); overflow-x:auto; }
    .tb { padding:13px 18px; border:none; background:none; cursor:pointer; font-size:13.5px; font-weight:600; color:var(--muted); border-bottom:3px solid transparent; transition:all .2s; white-space:nowrap; }
    .tb:hover { background:#eef2f7; color:var(--accent); }
    .tb.on { color:var(--accent); border-bottom-color:var(--accent); background:#fff; }
    .pg { padding:28px 32px; }
    .sec { display:none; }
    .sec.on { display:block; animation: fi .25s; }
    @keyframes fi { from{opacity:0;transform:translateY(6px)} to{opacity:1;transform:none} }
    h2 { font-size:1.1em; font-weight:700; color:var(--primary); margin-bottom:18px; padding-bottom:10px; border-bottom:2px solid var(--bg); display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:8px; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:14px; margin-bottom:24px; }
    .card { border:1.5px solid var(--border); border-radius:10px; padding:18px; background:var(--card); }
    .card h3 { font-size:11px; text-transform:uppercase; letter-spacing:.8px; color:var(--muted); margin-bottom:10px; }
    .cval { font-size:1.4em; font-weight:700; display:flex; align-items:center; gap:8px; }
    .dot { width:10px; height:10px; border-radius:50%; display:inline-block; flex-shrink:0; }
    .dg { background:#43a047; box-shadow:0 0 6px #43a04780; }
    .dr { background:#e53935; }
    .do { background:#fb8c00; box-shadow:0 0 6px #fb8c0080; }
    .rbx { display:grid; grid-template-columns:repeat(3,1fr); gap:12px; max-width:400px; margin-bottom:24px; }
    .rb { padding:16px 10px; border:none; border-radius:10px; cursor:pointer; font-size:15px; font-weight:700; color:#fff; transition:all .2s; }
    .rb:hover { transform:translateY(-2px); box-shadow:0 6px 18px rgba(0,0,0,.2); }
    .rb-open { background:linear-gradient(135deg,#2e7d32,#43a047); }
    .rb-stop { background:linear-gradient(135deg,#546e7a,#78909c); }
    .rb-close { background:linear-gradient(135deg,#c62828,#e53935); }
    .btn { padding:9px 16px; border:none; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; transition:all .2s; }
    .btn:hover { filter:brightness(1.08); transform:translateY(-1px); }
    .btn-p { background:var(--accent); color:#fff; }
    .btn-s { background:var(--success); color:#fff; }
    .btn-d { background:var(--danger); color:#fff; }
    .btn-o { background:#fff; color:var(--accent); border:1.5px solid var(--accent); }
    .btn-sm { padding:5px 11px; font-size:12px; }
    .bgrp { display:flex; flex-wrap:wrap; gap:10px; margin-top:8px; }
    .learn-bar { display:none; background:#fff8e1; border:1.5px solid #ffca28; border-radius:8px; padding:12px 16px; margin:14px 0; align-items:center; gap:10px; }
    .learn-bar.on { display:flex; }
    .pulse { width:12px; height:12px; border-radius:50%; background:#fb8c00; animation:pu 1s infinite; flex-shrink:0; }
    @keyframes pu { 0%,100%{transform:scale(1);opacity:1} 50%{transform:scale(1.5);opacity:.5} }
    .fg { margin-bottom:14px; }
    label { display:block; font-size:12.5px; font-weight:600; color:#455a64; margin-bottom:5px; }
    input[type=text], input[type=password], input[type=number], select { width:100%; padding:9px 12px; border:1.5px solid var(--border); border-radius:8px; font-size:13.5px; background:#fff; color:var(--text); transition:border .2s; }
    input:focus, select:focus { outline:none; border-color:var(--accent); }
    input[type=checkbox] { width:auto; margin-right:8px; }
    .tbl { width:100%; border-collapse:collapse; font-size:13.5px; }
    .tbl thead th { background:var(--bg); padding:9px 14px; text-align:left; font-size:11px; text-transform:uppercase; letter-spacing:.5px; color:var(--muted); }
    .tbl tbody td { padding:11px 14px; border-bottom:1px solid var(--border); }
    .tbl tbody tr:hover { background:#fafbfc; }
    .b { display:inline-block; padding:3px 10px; border-radius:12px; font-size:11.5px; font-weight:700; }
    .b-g { background:#e8f5e9; color:#2e7d32; }
    .b-r { background:#ffebee; color:#c62828; }
    .b-b { background:#e3f2fd; color:#1565c0; }
    .b-o { background:#fff3e0; color:#e65100; }
    .term { background:#0d1117; border-radius:10px; padding:2px; border:1px solid #30363d; }
    .term-out { height:440px; overflow-y:auto; padding:12px 16px; font-family:'Consolas','Monaco','Fira Mono',monospace; font-size:12.5px; line-height:1.7; }
    .ll { padding:1px 0; word-break:break-word; }
    .ts { color:#484f58; margin-right:8px; user-select:none; }
    .lm { color:#c9d1d9; }
    .lm-ok { color:#3fb950; }
    .lm-err { color:#ff7b72; }
    .lm-warn { color:#d29922; }
    .lm-info { color:#58a6ff; }
    .term-out::-webkit-scrollbar { width:6px; }
    .term-out::-webkit-scrollbar-track { background:#161b22; }
    .term-out::-webkit-scrollbar-thumb { background:#30363d; border-radius:3px; }
    #toast { display:none; position:fixed; bottom:22px; right:22px; padding:12px 20px; border-radius:10px; font-size:13.5px; color:#fff; box-shadow:0 6px 20px rgba(0,0,0,.25); z-index:9999; animation:fi .3s; }
    @media(max-width:600px) { .pg { padding:16px; } .rbx { max-width:none; } header { padding:20px; } }
  </style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>&#127968; Volet Roulant ESP32</h1>
    <p>Contr&#244;leur intelligent &#8212; Wiegand / RFID / Empreinte digitale</p>
  </header>

  <nav>
    <button class="tb on" onclick="go(event,'s1')">&#128202; Statut</button>
    <button class="tb" onclick="go(event,'s2')">&#128273; Codes d'Acc&#232;s</button>
    <button class="tb" onclick="go(event,'s3')">&#128203; Historique</button>
    <button class="tb" onclick="go(event,'s4')">&#9881;&#65039; Configuration</button>
    <button class="tb" onclick="go(event,'s5')">&#128421;&#65039; Terminal</button>
    <button class="tb" onclick="go(event,'s6')">&#128260; Mise &#224; Jour</button>
    <button class="tb" onclick="go(event,'s7')">&#128204; Broches GPIO</button>
  </nav>

  <div class="pg">

    <!-- STATUS -->
    <div id="s1" class="sec on">
      <h2>&#201;tat du Syst&#232;me</h2>
      <div class="grid">
        <div class="card">
          <h3>WiFi</h3>
          <div class="cval" id="cv-wifi"><span class="dot dr"></span>&#8212;</div>
          <div id="cv-ip" style="font-size:12px;color:var(--muted);margin-top:5px;"></div>
        </div>
        <div class="card">
          <h3>MQTT</h3>
          <div class="cval" id="cv-mqtt"><span class="dot dr"></span>&#8212;</div>
        </div>
        <div class="card">
          <h3>Relais</h3>
          <div class="cval" id="cv-relay"><span class="dot dr"></span>Inactif</div>
        </div>
        <div class="card">
          <h3>Barri&#232;re Photo.</h3>
          <div class="cval" id="cv-barrier"><span class="dot dg"></span>OK</div>
        </div>
        <div class="card">
          <h3>Mode Auth.</h3>
          <div class="cval" id="cv-auth" style="font-size:1em;">&#8212;</div>
        </div>
      </div>

      <h2>Contr&#244;le Manuel</h2>
      <div class="rbx">
        <button class="rb rb-open" onclick="relay('open')">&#9650; Ouvrir</button>
        <button class="rb rb-stop" onclick="relay('stop')">&#9209; Stop</button>
        <button class="rb rb-close" onclick="relay('close')">&#9660; Fermer</button>
      </div>

      <h2>Mode Apprentissage</h2>
      <div class="learn-bar" id="lbar">
        <div class="pulse"></div>
        <span id="lbar-txt">Mode apprentissage actif...</span>
        <button class="btn btn-d btn-sm" onclick="stopLearn()" style="margin-left:auto;">Arr&#234;ter</button>
      </div>
      <div class="fg" style="max-width:280px;">
        <label>Nom &#224; enregistrer</label>
        <input type="text" id="lname" placeholder="Ex: Utilisateur 1" maxlength="31">
      </div>
      <div class="bgrp">
        <button class="btn btn-o" onclick="startLearn(0)">&#9000; Clavier</button>
        <button class="btn btn-o" onclick="startLearn(1)">&#128278; RFID</button>
        <button class="btn btn-o" onclick="startLearn(2)">&#128400; Empreinte</button>
      </div>
    </div>

    <!-- CODES -->
    <div id="s2" class="sec">
      <h2>Codes d'Acc&#232;s <span id="nbc" class="b b-b">0</span></h2>
      <table class="tbl">
        <thead><tr><th>#</th><th>Nom</th><th>Code</th><th>Type</th><th>Actif</th><th></th></tr></thead>
        <tbody id="ctb"></tbody>
      </table>

      <h2 style="margin-top:26px;">Ajouter un code</h2>
      <div class="grid" style="max-width:640px;">
        <div class="fg"><label>Nom</label><input type="text" id="an" placeholder="Ex: Alice" maxlength="31"></div>
        <div class="fg"><label>Code</label><input type="number" id="ac" placeholder="Ex: 1234" min="1"></div>
        <div class="fg">
          <label>Type</label>
          <select id="at">
            <option value="0">Clavier (0)</option>
            <option value="1">RFID (1)</option>
            <option value="2">Empreinte (2)</option>
          </select>
        </div>
      </div>
      <button class="btn btn-s" onclick="addCode()">Ajouter</button>
    </div>

    <!-- LOGS -->
    <div id="s3" class="sec">
      <h2>Historique des Acc&#232;s <button class="btn btn-o btn-sm" onclick="loadLogs()">Actualiser</button></h2>
      <table class="tbl">
        <thead><tr><th>Horodatage (ms)</th><th>Code</th><th>Type</th><th>R&#233;sultat</th></tr></thead>
        <tbody id="ltb"></tbody>
      </table>
    </div>

    <!-- CONFIG -->
    <div id="s4" class="sec">
      <h2>R&#233;seau WiFi</h2>
      <div class="grid" style="max-width:600px;">
        <div class="fg">
          <label>SSID actuel</label>
          <div id="cv-ssid-cfg" style="padding:9px 12px;border:1.5px solid var(--border);border-radius:8px;font-size:13.5px;background:#f8fafc;color:var(--muted);">&#8212;</div>
        </div>
        <div class="fg" style="display:flex;align-items:center;gap:8px;padding-top:22px;">
          <input type="checkbox" id="sip" style="width:18px;height:18px;" onchange="toggleStaticIP()">
          <label for="sip" style="margin:0;">IP fixe (d&#233;sactiver DHCP)</label>
        </div>
      </div>
      <div id="static-fields" style="display:none;">
        <div class="grid" style="max-width:600px;">
          <div class="fg"><label>Adresse IP</label><input type="text" id="wip" placeholder="192.168.1.100"></div>
          <div class="fg"><label>Passerelle</label><input type="text" id="wgw" placeholder="192.168.1.1"></div>
          <div class="fg"><label>Masque de sous-r&#233;seau</label><input type="text" id="wsn" placeholder="255.255.255.0"></div>
        </div>
      </div>
      <div class="bgrp" style="margin-bottom:24px;">
        <button class="btn btn-d" onclick="resetWifi()">&#9888; R&#233;initialiser WiFi (passe en mode AP)</button>
      </div>
      <h2>MQTT</h2>
      <div class="grid" style="max-width:700px;">
        <div class="fg"><label>Serveur</label><input type="text" id="ms" placeholder="192.168.1.x"></div>
        <div class="fg"><label>Port</label><input type="number" id="mp" value="1883"></div>
        <div class="fg"><label>Utilisateur</label><input type="text" id="mu"></div>
        <div class="fg"><label>Mot de passe</label><input type="password" id="mw"></div>
        <div class="fg"><label>Topic de base</label><input type="text" id="mt" value="roller"></div>
      </div>
      <h2>Relais &amp; S&#233;curit&#233;</h2>
      <div class="grid" style="max-width:500px;">
        <div class="fg"><label>Dur&#233;e d'activation (ms)</label><input type="number" id="rd" value="5000"></div>
        <div class="fg" style="display:flex;align-items:center;gap:8px;padding-top:22px;">
          <input type="checkbox" id="pb" style="width:18px;height:18px;">
          <label for="pb" style="margin:0;">Barri&#232;re photo activ&#233;e</label>
        </div>
        <div class="fg"><label>Mot de passe admin</label><input type="password" id="ap"></div>
      </div>
      <h2>Mode d'Identification</h2>
      <div class="grid" style="max-width:480px;">
        <div class="fg">
          <label>Mode requis pour l'acc&#232;s</label>
          <select id="am" onchange="updateAuthDesc()">
            <option value="0">&#128290; Code PIN long seul (clavier)</option>
            <option value="1">&#128278; Badge RFID seul</option>
            <option value="2">&#128274; Badge RFID + Code PIN (2FA)</option>
          </select>
          <p id="am-desc" style="font-size:11.5px;color:var(--muted);margin-top:7px;"></p>
        </div>
      </div>
      <button class="btn btn-p" onclick="saveCfg()">Enregistrer</button>
      <p style="font-size:11.5px;color:var(--muted);margin-top:8px;">&#8505; Un red&#233;marrage est n&#233;cessaire pour appliquer les changements d'IP.</p>
    </div>

    <!-- TERMINAL -->
    <div id="s5" class="sec">
      <h2>Terminal de D&#233;bogage
        <span style="display:flex;align-items:center;gap:8px;">
          <span id="ws-b" class="b b-r">&#9899; D&#233;co.</span>
          <button class="btn btn-d btn-sm" onclick="clearTerm()">Vider</button>
        </span>
      </h2>
      <div class="term">
        <div class="term-out" id="tout"></div>
      </div>
    </div>

    <!-- OTA -->
    <div id="s6" class="sec">
      <h2>Mise &#224; Jour Firmware</h2>
      <p style="color:var(--muted);margin-bottom:20px;">Utilisez ElegantOTA pour flasher un nouveau firmware via le navigateur.</p>
      <a href="/update" target="_blank"><button class="btn btn-p">Ouvrir ElegantOTA</button></a>
    </div>

    <!-- GPIO PINS -->
    <div id="s7" class="sec">
      <h2>Broches GPIO</h2>
      <p style="font-size:13px;color:var(--muted);margin-bottom:18px;background:#fff8e1;border:1.5px solid #ffca28;border-radius:8px;padding:10px 14px;">&#9888; Un red&#233;marrage est n&#233;cessaire pour appliquer les changements de broches.</p>
      <div class="grid" style="max-width:700px;">
        <div class="fg"><label>Wiegand D0</label><input type="number" id="pd0" min="0" max="39"></div>
        <div class="fg"><label>Wiegand D1</label><input type="number" id="pd1" min="0" max="39"></div>
        <div class="fg"><label>Relais Ouverture</label><input type="number" id="prlop" min="0" max="39"></div>
        <div class="fg"><label>Relais Fermeture</label><input type="number" id="prlcl" min="0" max="39"></div>
        <div class="fg"><label>Barri&#232;re photo-&#233;lectrique</label><input type="number" id="ppbar" min="0" max="39"></div>
        <div class="fg"><label>LED Statut syst&#232;me</label><input type="number" id="pstld" min="0" max="39"></div>
        <div class="fg"><label>LED Lecteur Rouge</label><input type="number" id="prlr" min="0" max="39"></div>
        <div class="fg"><label>LED Lecteur Verte</label><input type="number" id="prlg" min="0" max="39"></div>
        <div class="fg"><label>Interrupteur Monter</label><input type="number" id="pupsw" min="0" max="39"></div>
        <div class="fg"><label>Interrupteur Descendre</label><input type="number" id="pdwsw" min="0" max="39"></div>
      </div>
      <div class="bgrp">
        <button class="btn btn-p" onclick="savePins()">Enregistrer les broches</button>
        <button class="btn btn-o" onclick="restartESP()">Red&#233;marrer l'ESP32</button>
      </div>
    </div>

  </div>
</div>

<div id="toast"></div>

<script>
function go(e,id){
  document.querySelectorAll('.tb').forEach(function(b){b.classList.remove('on');});
  document.querySelectorAll('.sec').forEach(function(s){s.classList.remove('on');});
  e.currentTarget.classList.add('on');
  document.getElementById(id).classList.add('on');
  if(id==='s1')loadSt();
  if(id==='s2')loadCodes();
  if(id==='s3')loadLogs();
  if(id==='s4')loadCfg();
  if(id==='s7')loadPins();
}

function toast(msg,err){
  var t=document.getElementById('toast');
  t.textContent=msg;
  t.style.display='block';
  t.style.background=err?'#c62828':'#2e7d32';
  clearTimeout(t._t);
  t._t=setTimeout(function(){t.style.display='none';},3000);
}

function set(id,html){var el=document.getElementById(id);if(el)el.innerHTML=html;}

function loadSt(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    set('cv-wifi',d.wifi?'<span class="dot dg"></span>Connect&#233;':'<span class="dot dr"></span>D&#233;connect&#233;');
    document.getElementById('cv-ip').textContent=(d.ssid?d.ssid+' \u2014 ':'')+d.ip;
    set('cv-mqtt',d.mqtt?'<span class="dot dg"></span>Connect&#233;':'<span class="dot dr"></span>D&#233;connect&#233;');
    set('cv-relay',d.relayActive?'<span class="dot do"></span>Actif':'<span class="dot dr"></span>Inactif');
    set('cv-barrier',(d.barrier!==false)?'<span class="dot dg"></span>OK':'<span class="dot dr"></span>Coup&#233;e');
    var amn=['PIN seul','RFID seul','PIN+RFID (2FA)'];
    set('cv-auth',amn[d.authMode]||'&#8212;');
    var lb=document.getElementById('lbar');
    if(d.learning){
      lb.classList.add('on');
      var tn=['Clavier','RFID','Empreinte'];
      document.getElementById('lbar-txt').textContent='Mode apprentissage: '+(tn[d.learningType]||'?');
    }else{
      lb.classList.remove('on');
    }
  }).catch(function(){});
}

function relay(a){
  fetch('/api/relay',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:a})})
  .then(function(r){return r.json();}).then(function(d){toast(d.message||'OK');setTimeout(loadSt,400);})
  .catch(function(){toast('Erreur',1);});
}

function startLearn(t){
  var n=document.getElementById('lname').value.trim();
  if(!n){toast('Entrez un nom',1);return;}
  fetch('/api/learn',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:t,name:n})})
  .then(function(r){return r.json();}).then(function(d){toast(d.message||'Apprentissage d&#233;marr&#233;');loadSt();})
  .catch(function(){toast('Erreur',1);});
}

function stopLearn(){
  fetch('/api/learn/stop',{method:'POST'}).then(function(){loadSt();}).catch(function(){});
}

function loadCodes(){
  fetch('/api/codes').then(function(r){return r.json();}).then(function(d){
    var codes=d.codes||[];
    document.getElementById('nbc').textContent=codes.length;
    var tn=['Clavier','RFID','Empreinte'];
    document.getElementById('ctb').innerHTML=codes.map(function(c,i){
      return '<tr><td>'+i+'</td><td>'+escH(c.name)+'</td><td><code>'+c.code+'</code></td><td>'+
        (tn[c.type]||c.type)+'</td><td>'+(c.active?'<span class="b b-g">Actif</span>':'<span class="b b-r">Inactif</span>')+
        '</td><td><button class="btn btn-d btn-sm" onclick="delCode('+i+')">Suppr.</button></td></tr>';
    }).join('');
  });
}

function addCode(){
  var n=document.getElementById('an').value.trim(),
      c=parseInt(document.getElementById('ac').value),
      t=parseInt(document.getElementById('at').value);
  if(!n||isNaN(c)||c<=0){toast('Nom et code valide requis',1);return;}
  fetch('/api/codes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:n,code:c,type:t})})
  .then(function(r){return r.json();}).then(function(d){
    toast(d.message||d.error,!!d.error);
    if(!d.error){document.getElementById('an').value='';document.getElementById('ac').value='';loadCodes();}
  });
}

function delCode(i){
  if(!confirm('Supprimer ce code d\'acc\u00e8s ?')) return;
  fetch('/api/codes?index='+i,{method:'DELETE'})
    .then(function(r){return r.json();})
    .then(function(d){toast(d.message||d.error,!!d.error);loadCodes();})
    .catch(function(){toast('Erreur r\u00e9seau lors de la suppression',1);});
}

function loadLogs(){
  fetch('/api/logs').then(function(r){return r.json();}).then(function(d){
    var tn=['Clavier','RFID','Empreinte'];
    document.getElementById('ltb').innerHTML=(d.logs||[]).filter(function(l){return l.timestamp>0;}).reverse().map(function(l){
      return '<tr><td>'+l.timestamp+'</td><td><code>'+l.code+'</code></td><td>'+(tn[l.type]||l.type)+'</td><td>'+
        (l.granted?'<span class="b b-g">Autoris&#233;</span>':'<span class="b b-r">Refus&#233;</span>')+'</td></tr>';
    }).join('');
  });
}

function loadCfg(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    var el=document.getElementById('cv-ssid-cfg');
    if(el) el.textContent=(d.ssid&&d.ssid.length?d.ssid:'Non connect\u00e9')+' \u2014 '+(d.ip||'\u2014');
  }).catch(function(){});
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.getElementById('ms').value=d.mqttServer||'';
    document.getElementById('mp').value=d.mqttPort||1883;
    document.getElementById('mu').value=d.mqttUser||'';
    document.getElementById('mt').value=d.mqttTopic||'roller';
    document.getElementById('rd').value=d.relayDuration||5000;
    document.getElementById('pb').checked=!!d.photoEnabled;
    document.getElementById('am').value=d.authMode!==undefined?d.authMode:1;
    updateAuthDesc();
    document.getElementById('sip').checked=!!d.useStaticIP;
    document.getElementById('wip').value=d.staticIP||'';
    document.getElementById('wgw').value=d.staticGateway||'';
    document.getElementById('wsn').value=d.staticSubnet||'255.255.255.0';
    toggleStaticIP();
  });
}

function saveCfg(){
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    mqttServer:document.getElementById('ms').value,
    mqttPort:+document.getElementById('mp').value,
    mqttUser:document.getElementById('mu').value,
    mqttPassword:document.getElementById('mw').value,
    mqttTopic:document.getElementById('mt').value,
    relayDuration:+document.getElementById('rd').value,
    photoEnabled:document.getElementById('pb').checked,
    adminPassword:document.getElementById('ap').value,
    authMode:+document.getElementById('am').value,
    useStaticIP:document.getElementById('sip').checked,
    staticIP:document.getElementById('wip').value,
    staticGateway:document.getElementById('wgw').value,
    staticSubnet:document.getElementById('wsn').value
  })}).then(function(r){return r.json();}).then(function(d){toast(d.message||'Enregistr&#233;',!!d.error);});
}

function updateAuthDesc(){
  var v=+document.getElementById('am').value;
  var desc=[
    'Seul un code PIN long (clavier) est requis. Le badge RFID est ignoré.',
    'Seul le badge RFID est requis. Le clavier est ignoré.',
    'Le badge RFID ET le code PIN sont tous deux requis (2FA). L\'un peut être présenté avant l\'autre (fenêtre 30s).'
  ];
  var el=document.getElementById('am-desc');
  if(el) el.textContent=desc[v]||'';
}

var _ws;
function initWS(){
  var url=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws';
  _ws=new WebSocket(url);
  _ws.onopen=function(){
    var b=document.getElementById('ws-b');
    if(b){b.textContent='&#128994; Connect&#233;';b.className='b b-g';}
  };
  _ws.onmessage=function(ev){
    try{var d=JSON.parse(ev.data);if(d.log!==undefined)addLog(d.log);}catch(e){}
  };
  _ws.onclose=function(){
    var b=document.getElementById('ws-b');
    if(b){b.textContent='&#128308; D&#233;co.';b.className='b b-r';}
    setTimeout(initWS,3000);
  };
  _ws.onerror=function(){_ws.close();};
}

function addLog(msg){
  var el=document.getElementById('tout');
  if(!el)return;
  var now=new Date().toLocaleTimeString('fr-FR',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
  var cls='lm';
  var m=msg.toUpperCase();
  if(m.indexOf('GRANTED')>=0||m.indexOf('CONNECTED')>=0||m.indexOf('CONNECT&#201;')>=0) cls='lm-ok';
  else if(m.indexOf('DENIED')>=0||m.indexOf('ERROR')>=0||m.indexOf('ERREUR')>=0||m.indexOf('FAILED')>=0) cls='lm-err';
  else if(m.indexOf('WARNING')>=0||m.indexOf('TIMEOUT')>=0||m.indexOf('DISCONNECT')>=0) cls='lm-warn';
  else if(m.indexOf('MQTT')>=0||m.indexOf('WIFI')>=0||m.indexOf('[WIEGAND]')>=0||m.indexOf('MODE')>=0) cls='lm-info';
  var line=document.createElement('div');
  line.className='ll';
  line.innerHTML='<span class="ts">'+now+'</span><span class="'+cls+'">'+escH(msg)+'</span>';
  el.appendChild(line);
  el.scrollTop=el.scrollHeight;
  while(el.children.length>600)el.removeChild(el.firstChild);
}

function clearTerm(){var el=document.getElementById('tout');if(el)el.innerHTML='';}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#x27;');}
function escH(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}

function toggleStaticIP(){
  var el=document.getElementById('static-fields');
  if(el) el.style.display=document.getElementById('sip').checked?'block':'none';
}
function resetWifi(){
  if(!confirm('R\u00e9initialiser les identifiants WiFi ? L\'ESP32 passera en mode point d\'acc\u00e8s.')) return;
  fetch('/api/wifi/reset',{method:'POST'}).then(function(r){return r.json();}).then(function(d){toast(d.message||'R\u00e9initialisation...');}).catch(function(){});
}
function restartESP(){
  if(!confirm('Red\u00e9marrer l\'ESP32 ?')) return;
  fetch('/api/restart',{method:'POST'}).then(function(){toast('Red\u00e9marrage en cours...');}).catch(function(){});
}
function loadPins(){
  fetch('/api/pins').then(function(r){return r.json();}).then(function(d){
    document.getElementById('pd0').value=d.wiegandD0;
    document.getElementById('pd1').value=d.wiegandD1;
    document.getElementById('prlop').value=d.relayOpen;
    document.getElementById('prlcl').value=d.relayClose;
    document.getElementById('ppbar').value=d.photoBarrier;
    document.getElementById('pstld').value=d.statusLed;
    document.getElementById('prlr').value=d.readerLedRed;
    document.getElementById('prlg').value=d.readerLedGreen;
    document.getElementById('pupsw').value=d.pinUpSwitch;
    document.getElementById('pdwsw').value=d.pinDownSwitch;
  });
}
function savePins(){
  var fields=['pd0','pd1','prlop','prlcl','ppbar','pstld','prlr','prlg','pupsw','pdwsw'];
  var vals=fields.map(function(id){return parseInt(document.getElementById(id).value);});
  if(vals.some(function(v){return isNaN(v)||v<0||v>39;})){toast('Valeur de broche invalide (0-39)',1);return;}
  fetch('/api/pins',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    wiegandD0:vals[0],wiegandD1:vals[1],relayOpen:vals[2],relayClose:vals[3],
    photoBarrier:vals[4],statusLed:vals[5],readerLedRed:vals[6],readerLedGreen:vals[7],
    pinUpSwitch:vals[8],pinDownSwitch:vals[9]
  })}).then(function(r){return r.json();}).then(function(d){
    toast(d.message||'Enregistr\u00e9',!!d.error);
    if(!d.error && confirm('Red\u00e9marrer maintenant pour appliquer ?')) restartESP();
  });
}

window.addEventListener('load',function(){
  loadSt();
  setInterval(loadSt,5000);
  initWS();
});
</script>
</body>
</html>
)rawliteral";

#endif
