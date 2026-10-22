"use strict";
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
 const cal=await(await request('/diagnostics')).json();$('calibration-status').textContent=cal.calibration_active?('Collecting range · '+cal.calibration_samples+' filtered readings · '+(cal.calibration_range_ready?'Range detected. Turn OFF when finished.':'Raise and lower the sensor in water.')):(cal.calibration_notice||'Calibration OFF');
 const values=await(await request('/get-values')).json();$('process-values').textContent='';for(const [k,v]of Object.entries(values)){const p=document.createElement('p');p.textContent=k+': '+Math.round(v);$('process-values').appendChild(p);}
 }catch(e){$('mode-status').textContent='Connection unavailable';}setTimeout(poll,1000);}
async function init(){controls(false);try{const d=await(await request('/device-status')).json();if(d.slave){$('selector-card').hidden=true;$('submit-button').disabled=true;$('remove-slaves-button').disabled=true;}await refreshPeers();await loadBoard('master');}catch(e){status(e.message);}poll();}init();
