#pragma once
#include <Arduino.h>
static const char kControllerIndex[] PROGMEM = R"R5ASSET(<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta http-equiv="X-UA-Compatible" content="IE=edge" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    
    <link rel="stylesheet" type="text/css" href="style.css" />
    <title>Hydrofoil Control R5.1</title>
  </head>
  <body>
    <div class="topnav">
      <h1>Hydrofoil Control R5.1</h1>
    </div>
    <div class="content">
<div class="card"><p id="mode-status">Connecting…</p><p id="save-status" role="status"></p>
<form action="/controller/pause" method="POST"><button>Pause control and open testing / recorder</button></form>
<form action="/controller/resume" method="POST"><button>Resume normal controller</button></form>
<p><a href="/setup">Device role and connection</a> · <a href="/diagnostics">Diagnostics</a></p></div>
      <div class="card" id="selector-card">
        <p class="card-title">Board Selector</p>
        <select name="board-selector" id="board-selector">
          <option value="master">Master</option>
        </select>
      </div>
      <div class="card" id="inputs">
        <p class="card-title">Inputs</p>

        <label for="slider-p">P value</label>
        <input
          class="slider"
          type="range"
          id="slider-p"
          name="slider-p"
          min="0"
          max="4"
          value="0"
          step="0.1"
        />
        <span id="slider-p-value" class="value">0</span><br />

        <label for="slider-i">I value</label>
        <input
          type="range"
          id="slider-i"
          name="slider-i"
          min="0"
          max="2"
          value="0"
          step="0.01"
        />
        <span id="slider-i-value" class="value">0</span><br />

        <label for="slider-d">D value</label>
        <input
          type="range"
          id="slider-d"
          name="slider-d"
          min="0"
          max="2"
          value="0"
          step="0.01"
        />
        <span id="slider-d-value" class="value">0</span><br />

        <label for="slider-setpoint">Setpoint value (us)</label>
        <input
          type="range"
          id="slider-setpoint"
          name="slider-setpoint"
          min="1000"
          max="2000"
          value="1500"
        />
        <span id="slider-setpoint-value" class="value">0</span><br />

        <label for="slider-factor">PWM input effect (%)</label>
        <input
          type="range"
          id="slider-factor"
          name="slider-factor"
          min="0"
          max="100"
          value="40"
        />
        <span id="slider-factor-value" class="value">0</span><br />

        <label for="slider-enable">Enable PID control</label>
        <input
          type="range"
          id="slider-enable"
          name="slider-enable"
          min="0"
          max="1"
          value="0"
          style="width: 25px"
        />
        <span id="slider-enable-value" class="value">0</span><br />

        <label for="slider-calibration">Enable Calibration</label>
        <input
          type="range"
          id="slider-calibration"
          name="slider-calibration"
          min="0"
          max="1"
          value="0"
          style="width: 25px"
        />
        <span id="slider-calibration-value" class="value">0</span><br />

        <label for="slider-servo-min">Servo min position (us)</label>
        <input
          type="range"
          id="slider-servo-min"
          name="slider-servo-min"
          min="1000"
          max="2000"
          value="0"
        />
        <span id="slider-servo-min-value" class="value">0</span><br />

        <label for="slider-servo-max">Servo max position (us)</label>
        <input
          type="range"
          id="slider-servo-max"
          name="slider-servo-max"
          min="1000"
          max="2000"
          value="2000"
        />
        <span id="slider-servo-max-value" class="value">0</span><br />
      </div>

      <div class="card">
        <p class="card-title">Add New Connection</p>
        <label for="mac-input">MAC address</label>
        <input
          type="text"
          id="mac-input"
          name="mac-input"
          class=""
          placeholder="FF:FF:FF:FF:FF:FF"
        />
        <br />
        <input type="button" value="Submit" id="submit-button" />
      </div>

      <div class="card">
        <p class="card-title">Slave Devices</p>
        <label for="slave-device-list">Max 2 slaves</label>
        <div id="slave-device-list" class="value"></div>
      </div>

      <div class="card" id="remove-slaves">
        <p class="card-title">Remove All Slave Devices</p>
        <input type="button" value="Remove" id="remove-slaves-button" />
      </div>

      <div class="card" id="wifi-off-card">
        <p class="card-title">Hide Hotspot Until Restart</p>
        <input type="button" value="Hide hotspot" id="wifi-off-button" />
      </div>

      <div class="card">
        <p class="card-title">Reset Info</p>
        <div>
          <p>Restart the board to restore its hotspot. A brief Reset-jumper connection also restores a hidden hotspot.</p>
          <p>Saved settings are retained when restarting.</p>
          <p>Application updates are available through this hotspot without a separate upload mode.</p>
        </div>
      </div>

      <div class="card">
        <p class="card-title">Master Device Process Values</p>
        <div id="process-values"></div>
      </div>
    </div>
    <script src="script.js"></script>
  </body>
</html>
)R5ASSET";
static const char kControllerScript[] PROGMEM = R"R5ASSET("use strict";
const $=id=>document.getElementById(id);
const fields=['p','i','d','setpoint','factor','enable','calibration','servo-min','servo-max'].map(x=>'slider-'+x);
let ready=false,activeBoard='master',busy=false;
const status=t=>$('save-status').textContent=t;
function controls(enabled){for(const id of fields)$(id).disabled=!enabled;$('board-selector').disabled=!enabled;}
async function request(url,body){const opt={cache:'no-store'};if(body){opt.method='POST';opt.headers={'Content-Type':'application/x-www-form-urlencoded'};opt.body=new URLSearchParams(body);}
 const r=await fetch(url,opt);if(!r.ok)throw Error(await r.text());return r;}
async function loadBoard(board){controls(false);ready=false;try{
 const d=await(await request('/get-settings?board='+encodeURIComponent(board))).json();
 for(const id of fields){$(id).value=d[id];$(id+'-value').textContent=d[id];}
 activeBoard=board;$('board-selector').value=board;ready=true;status('Settings loaded');
 }catch(e){status(e.message);}finally{controls(ready);}}
async function save(){if(!ready||busy)return;busy=true;controls(false);const body={'board-selector':activeBoard};for(const id of fields)body[id]=$(id).value;
 try{await request('/set-sliders',body);status('Settings saved');}catch(e){const error=e.message;await loadBoard(activeBoard);status('Not saved: '+error);}finally{busy=false;controls(ready);}}
$('inputs').addEventListener('input',e=>{if(fields.includes(e.target.id))$(e.target.id+'-value').textContent=e.target.value;});
$('inputs').addEventListener('change',save);
$('board-selector').addEventListener('change',()=>loadBoard($('board-selector').value));
async function refreshPeers(){const d=await(await request('/get-addresses')).json();$('slave-device-list').textContent=Object.values(d).filter(Boolean).join(' · ')||'No slaves configured';
 const selector=$('board-selector');for(const v of ['slave-1','slave-2']){if(!Array.from(selector.options).some(o=>o.value===v)){const o=document.createElement('option');o.value=v;o.textContent=v==='slave-1'?'Slave 1':'Slave 2';selector.appendChild(o);}}}
$('submit-button').onclick=async()=>{try{await request('/add-mac',{mac:$('mac-input').value});$('mac-input').value='';await refreshPeers();status('Peer saved');}catch(e){status(e.message);}};
$('remove-slaves-button').onclick=async()=>{try{await request('/remove-slaves',{'remove-slaves':'true'});await refreshPeers();status('Peers removed');}catch(e){status(e.message);}};
$('wifi-off-button').onclick=async()=>{try{await request('/wifi-off',{'wifi-off':'true'});status('Hotspot will hide. Restart the board to reconnect.');}catch(e){status(e.message);}};
async function poll(){try{const d=await(await request('/device-status')).json();$('mode-status').textContent=(d.controller_mode==='bench'?'Bench mode: servo output off':'Normal controller active')+' · '+(d.slave?'Slave':'Master')+(d.error?' · '+d.error:'');
 const values=await(await request('/get-values')).json();$('process-values').textContent='';for(const [k,v]of Object.entries(values)){const p=document.createElement('p');p.textContent=k+': '+Math.round(v);$('process-values').appendChild(p);}
 }catch(e){$('mode-status').textContent='Connection unavailable';}setTimeout(poll,1000);}
async function init(){controls(false);try{const d=await(await request('/device-status')).json();if(d.slave){$('selector-card').hidden=true;$('submit-button').disabled=true;$('remove-slaves-button').disabled=true;}await refreshPeers();await loadBoard('master');}catch(e){status(e.message);}poll();}init();
)R5ASSET";
static const char kControllerStyle[] PROGMEM = R"R5ASSET(html {
  font-family: Arial, Helvetica, sans-serif;
  display: block;
  text-align: center;
  background-color: #f4f7f6;
  color: #333;
}

body {
  margin: 0;
  padding: 0;
}

h1 {
  font-size: 2rem;
  color: white;
  margin: 0;
  padding: 15px 0;
}

.topnav {
  overflow: hidden;
  background-color: #2d42b8;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.content {
  padding: 20px;
  max-width: 800px;
  margin: 0 auto;
}

.card {
  background-color: white;
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
  padding: 20px;
  margin-top: 20px;
  border-radius: 8px;
  text-align: left;
}

.card-title {
  font-size: 1.4rem;
  font-weight: bold;
  color: #034078;
  margin-bottom: 15px;
  border-bottom: 2px solid #f4f7f6;
  padding-bottom: 10px;
}

label {
  display: block;
  font-size: 1rem;
  color: #555;
  margin-bottom: 5px;
}

input[type="checkbox"] {
  margin-top: 10px;
}

input[type="range"] {
  width: 100%;
  margin-bottom: 10px;
}
input[type="text"] {
  display: block;
  width: 100%;
  padding: 12px;
  margin: 10px 0;
  font-size: 1.1rem;
  box-sizing: border-box;
  border: 1px solid #ddd;
  border-radius: 4px;
}

.value {
  font-weight: bold;
  font-size: 1.1rem;
  color: #034078;
  margin-bottom: 10px;
  display: inline-block;
}

/* Responsive Design */
@media (max-width: 600px) {
  h1 {
    font-size: 1.8rem;
  }

  .content {
    padding: 10px;
  }

  .card {
    padding: 15px;
    margin-top: 15px;
  }

  .card-title {
    font-size: 1.4rem;
  }

  label,
  .value {
    font-size: 1.1rem;
  }

  input[type="text"],
  input[type="submit"] {
    padding: 12px;
    font-size: 1.1rem;
  }

  input[type="checkbox"] {
    margin-top: 15px;
  }
}
)R5ASSET";
static const char kControllerSetup[] PROGMEM = R"R5ASSET(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Hydrofoil device setup</title><link rel="stylesheet" href="/style.css"></head><body><div class="content"><h1>Device setup</h1><div class="card"><p>Station MAC for pairing: <strong id="mac">…</strong></p><form action="/setup" method="POST"><label>Role <select name="role" id="role" disabled><option value="master">Master</option><option value="slave">Slave</option></select></label><p>Saving restarts this board. Its HF-Bench hotspot remains available in either role.</p><button id="save-role" disabled>Save role and restart</button><p id="setup-error" role="status"></p></form><p>Updates use the board hotspot directly. Existing router credentials remain stored.</p><a href="/">Return to controller</a></div></div><script>const role=document.getElementById('role'),save=document.getElementById('save-role'),error=document.getElementById('setup-error');async function loadSetup(){try{const [m,d]=await Promise.all([fetch('/get-mac'),fetch('/device-status')]);if(!m.ok||!d.ok)throw Error('Could not load device settings');document.getElementById('mac').textContent=await m.text();const state=await d.json();role.value=state.slave?'slave':'master';role.disabled=false;save.disabled=false;}catch(e){error.textContent='Settings unavailable. Reload before saving.';}}loadSetup();</script></body></html>)R5ASSET";
