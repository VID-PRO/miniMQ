#pragma once
// Embedded single-page web UI, served at "/". No external assets required.

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ArtNet-USB</title>
<style>
:root{
  --bg:#0e1116; --panel:#161b22; --panel2:#1c2330; --line:#2a3342;
  --fg:#e6e8ec; --dim:#8b94a3; --acc:#3aa0ff; --ok:#34d399; --warn:#fbbf24; --bad:#f87171;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
header{display:flex;flex-wrap:wrap;gap:10px;align-items:center;padding:12px 16px;background:var(--panel);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:30}
header .title{font-weight:700;font-size:16px;color:var(--acc)}
.pill{padding:4px 10px;border:1px solid var(--line);border-radius:999px;background:var(--panel2);color:var(--dim);font-size:12px}
.pill b{color:var(--fg);font-weight:600}
.pill .dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;vertical-align:middle}
.dot.ok{background:var(--ok)} .dot.warn{background:var(--warn)} .dot.bad{background:var(--bad)}
nav{display:flex;gap:8px;padding:10px 16px;border-bottom:1px solid var(--line);background:var(--panel);position:sticky;top:48px;z-index:20;margin-top:-1px}
nav button{background:none;border:1px solid transparent;color:var(--dim);padding:7px 16px;border-radius:8px;cursor:pointer;font:inherit}
nav button.active{color:var(--fg);border-color:var(--line);background:var(--panel2)}
main{max-width:1100px;margin:0 auto;padding:16px}
.grid-tools{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-bottom:12px}
.grid-tools label{color:var(--dim)} .grid-tools input[type=text]{background:var(--panel);border:1px solid var(--line);color:var(--fg);border-radius:6px;padding:6px 10px;font:inherit;width:220px}
.switch{position:relative;display:inline-block;width:40px;height:22px;vertical-align:middle}
.switch input{display:none} .slider{position:absolute;inset:0;background:var(--panel2);border:1px solid var(--line);border-radius:999px;cursor:pointer;transition:.15s}
.slider:before{content:"";position:absolute;width:16px;height:16px;left:2px;top:2px;background:var(--dim);border-radius:50%;transition:.15s}
.switch input:checked + .slider{background:var(--acc);border-color:var(--acc)}
.switch input:checked + .slider:before{transform:translateX(18px);background:#fff}
.grid{display:grid;grid-template-columns:repeat(32,1fr);gap:4px}
.cell{position:relative;height:44px;background:var(--panel2);border:1px solid var(--line);border-radius:5px;overflow:hidden;cursor:pointer;transition:border-color .1s}
.cell .bar{position:absolute;left:0;right:0;bottom:0;background:var(--acc);opacity:.85}
.cell .num{position:absolute;top:2px;left:4px;font-size:10px;color:var(--dim)}
.cell .val{position:absolute;bottom:1px;right:4px;font-size:11px;font-weight:600}
.cell:hover{border-color:var(--acc)} .cell.active{border-color:#fff}
.legend{display:flex;gap:16px;margin-top:14px;color:var(--dim);font-size:12px;flex-wrap:wrap}
.empty{color:var(--dim);text-align:center;padding:40px}
fieldset{border:1px solid var(--line);border-radius:10px;margin:0 0 16px;padding:16px;background:var(--panel)}
legend{color:var(--acc);padding:0 6px;font-weight:600}
.row{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:12px}
.row > div{flex:1 1 200px}
label{display:block;color:var(--dim);margin-bottom:5px;font-size:12px}
input[type=text],input[type=password],input[type=number],select{width:100%;background:var(--panel2);border:1px solid var(--line);color:var(--fg);border-radius:6px;padding:8px 10px;font:inherit}
input:focus,select:focus{outline:none;border-color:var(--acc)}
#univ-rows > div{display:flex;flex-wrap:wrap;gap:10px;align-items:end;margin-bottom:10px}
#univ-rows .port{flex:0 0 150px;margin:0;line-height:38px}
#univ-rows .tri{display:flex;gap:8px;align-items:end}
#univ-rows .tri label{width:42px;margin:0}
#univ-rows .tri input{width:74px;padding:8px 8px}
#univ-rows .tri select{width:auto;padding:8px 8px}
.hint{font-size:11px;color:var(--dim)}
.btns{display:flex;gap:10px;flex-wrap:wrap;margin-top:8px}
button.btn{background:var(--acc);color:#04121f;border:none;border-radius:8px;padding:10px 18px;font:inherit;font-weight:600;cursor:pointer}
button.btn:hover{filter:brightness(1.1)}
button.sec{background:var(--panel2);color:var(--fg);border:1px solid var(--line)}
button.danger{background:var(--bad);color:#1a0507;border:none}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:var(--panel2);border:1px solid var(--line);color:var(--fg);padding:10px 18px;border-radius:8px;display:none;z-index:50}
footer{position:fixed;left:0;right:0;bottom:0;display:flex;align-items:center;justify-content:center;gap:6px;padding:8px 16px;background:var(--panel);border-top:1px solid var(--line);color:var(--dim);font-size:12px;z-index:40}
footer a{color:var(--acc);text-decoration:none}
footer a:hover{text-decoration:underline}
body{padding-bottom:42px}
</style>
</head>
<body>
<header>
  <div class="title">ARTNET-USB</div>
  <div class="pill">IP <b id="t-ip">-</b></div>
  <div class="pill">Mask <b id="t-mask">-</b></div>
  <div class="pill"><span id="s-dhcp" class="dot warn"></span>DHCP <b id="t-dhcp">-</b></div>
  <div class="pill"><span id="s-art" class="dot bad"></span>Art-Net <b id="t-fps">0</b> fps</div>
  <div class="pill" id="t-namepi" style="display:none">ID <b id="t-name">-</b></div>
  <div class="pill">Up <b id="t-up">-</b></div>
</header>
<nav>
  <button id="tab-ch" class="active" onclick="showTab('ch')">Channels</button>
  <button id="tab-st" onclick="showTab('st')">Settings</button>
</nav>
<main>
  <section id="view-ch">
    <div class="grid-tools">
      <label>View:
        <select id="port" onchange="setPort()"></select>
      </label>
      <label>Search:
        <input type="text" id="q" placeholder="channel number or value (e.g. 12 or 255)" oninput="render()">
      </label>
      <label><span style="margin-right:8px">Active only</span>
        <span class="switch"><input type="checkbox" id="activeOnly" onchange="render()"><span class="slider"></span></span>
      </label>
      <span class="hint" id="hint-count"></span>
    </div>
    <div class="grid" id="grid"></div>
    <div class="legend"><span>Cell height = intensity</span><span>Number = value 0-255</span></div>
  </section>

  <section id="view-st" style="display:none">
    <form id="cfg-form">
      <fieldset><legend>Network</legend>
        <div class="row">
          <div><label>Static IP</label><input type="text" name="ip" spellcheck="false"></div>
          <div><label>Netmask</label><input type="text" name="mask" spellcheck="false"></div>
          <div><label style="display:flex;align-items:center;gap:8px">DHCP server<span class="switch"><input type="checkbox" name="dhcp"><span class="slider"></span></span></label>
              <span class="hint">Hands the host an address 10.0.0.x / your subnet</span></div>
        </div>
        <p class="hint">The node always uses a static address on the USB link. DHCP only configures the host. Changing the subnet means you must reach the new IP afterwards.</p>
      </fieldset>
      <fieldset><legend>Node identity</legend>
        <div class="row">
          <div><label>Short name</label><input type="text" name="name" maxlength="17" spellcheck="false" placeholder="MagicQ Compact"></div>
          <div><label>Long name</label><input type="text" name="longname" maxlength="63" spellcheck="false" placeholder="MagicQ Compact Mini Connect"></div>
        </div>
        <p class="hint">Advertised in ArtPollReply; MagicQ lists this name in its DMX output node list. Visible next to the Art-Net tablet in the header.</p>
      </fieldset>
      <fieldset><legend>Art-Net ports</legend>
        <div class="row" id="univ-rows"></div>
        <p class="hint">Each port is either an <b>output</b> (ArtDmx &rarr; DMX out) or an <b>input</b> (DMX in &rarr; ArtDmx), on its own Art-Net address: <b>net</b> 0-127, <b>subnet</b> 0-15, <b>universe</b> 0-15. MagicQ discovers the mapping via ArtPollReply.</p>
      </fieldset>
      <div class="btns">
        <button class="btn" type="button" onclick="save()">Save &amp; reboot</button>
        <button class="btn sec" type="button" onclick="reboot()">Reboot only</button>
        <button class="btn danger" type="button" onclick="factory()">Factory reset</button>
      </div>
    </form>
  </section>
</main>
<footer>ArtNet-USB node &middot; <a href="https://www.chamsys.com" target="_blank" rel="noopener">MagicQ compatible</a></footer>
<div id="toast"></div>

<script>
var N=512, vals=new Array(513).fill(0);
var valsByPort=[[],[],[],[]];   // last known frame per port (for port switches)
var curPort=-1;
var port=0;
var UNIV_N=4;

function showTab(t){
  document.getElementById('tab-ch').classList.toggle('active',t==='ch');
  document.getElementById('tab-st').classList.toggle('active',t==='st');
  document.getElementById('view-ch').style.display = t==='ch'?'':'none';
  document.getElementById('view-st').style.display = t==='st'?'':'none';
  if(t==='ch') render();
}

function build(){
  var g=document.getElementById('grid'); g.innerHTML='';
  for(var i=1;i<=512;i++){
    var c=document.createElement('div'); c.className='cell'; c.id='c'+i;
    c.title='Channel '+i;
    c.innerHTML='<div class="bar" id="b'+i+'"></div><span class="num">'+i+'</span><span class="val" id="v'+i+'">0</span>';
    c.onclick=function(){ this.classList.toggle('active'); };
    g.appendChild(c);
  }
  var sel=document.getElementById('port');
  for(var p=0;p<4;p++){
    var o=document.createElement('option'); o.value=p;
    o.textContent='Port '+(p+1);
    sel.appendChild(o);
  }
}

function render(){
  var q=document.getElementById('q').value.trim().toLowerCase();
  var only=document.getElementById('activeOnly').checked;
  var shown=0;
  for(var i=1;i<=512;i++){
    var c=document.getElementById('c'+i);
    var show=true;
    if(only && vals[i]===0) show=false;
    if(q && show){
      var s=i.toString()+':'+vals[i];
      show = s.indexOf(q)>=0 || i.toString()===q;
    }
    c.style.display = (i<=N && show)?'':'none';
    if(show) shown++;
  }
  document.getElementById('hint-count').textContent = shown+' / '+N+' channels';
}

function fmtUp(ms){
  ms=Math.floor(ms/1000);
  var h=Math.floor(ms/3600), m=Math.floor(ms%3600/60), s=ms%60;
  return (h?h+':':'')+(h?(''+m).padStart(2,'0'):m)+':'+(''+s).padStart(2,'0');
}

function update(d){
  var name=d.longname||d.name||'';
  document.getElementById('t-ip').textContent=d.ip;
  document.getElementById('t-mask').textContent=d.mask;
  document.getElementById('t-dhcp').textContent=d.dhcp?'on':'off';
  document.getElementById('s-dhcp').className='dot '+(d.dhcp?'ok':'warn');
  document.getElementById('s-art').className='dot '+(d.artnet.connected?'ok':'bad');
  document.getElementById('t-fps').textContent=d.artnet.fps;
  document.getElementById('t-up').textContent=fmtUp(d.uptime_ms);
  document.getElementById('t-name').textContent=name;
  document.getElementById('t-namepi').style.display=name?'':'none';
  var sel=document.getElementById('port');
  for(var i=0;i<sel.options.length;i++){
    var a='N'+(d.net?d.net[i]:0)+'/S'+(d.subnet?d.subnet[i]:0)+'/U'+(d.universe?d.universe[i]:0);
    var dir=(d.direction&&d.direction[i])?'IN ':'OUT ';
    sel.options[i].textContent='Port '+(i+1)+' - '+dir+a;
    sel.options[i].title=dir+a;
  }
  syncForm(d);
  if(d.values) valsByPort[port]=d.values;   // keep latest frame per port
  if(port!==curPort || d.changed!==false){
    curPort=port;
    vals=valsByPort[port]||vals;
    for(var i=1;i<=512;i++) drawCell(i);
    render();
  }
}

// Keep the Settings form in sync with what the node actually has. The node
// can be reprogrammed out-of-band (MagicQ ArtAddress), so every poll refills
// the fields; the browser may simply keep this page open and watch changes.
// Fields currently being edited (focused) are left alone.
function syncForm(d){
  var e=document.getElementById('cfg-form').elements;
  if(!e['ip']) return;
  var net=d.net||[], sub=d.subnet||[], uni=d.universe||[], dir=d.direction||[];
  var f={
    ip:d.ip, mask:d.mask, dhcp:d.dhcp?true:false,
    name:d.name||'', longname:d.longname||''
  };
  for(var k in f){
    var el=e[k]; if(!el || el===document.activeElement) continue;
    if(el.type==='checkbox'){ if(el.checked!==f[k]) el.checked=f[k]; }
    else if(el.value!==f[k]) el.value=f[k];
  }
  for(var i=0;i<net.length;i++){
    var n=e['n'+i], s=e['s'+i], u=e['u'+i], dd=e['d'+i];
    if(n && n!==document.activeElement && n.value!=net[i]) n.value=net[i];
    if(s && s!==document.activeElement && s.value!=sub[i]) s.value=sub[i];
    if(u && u!==document.activeElement && u.value!=uni[i]) u.value=uni[i];
    if(dd && dd!==document.activeElement && dd.value!=dir[i]) dd.value=dir[i];
  }
}
function drawCell(i){
  var v=vals[i]||0;
  var el=document.getElementById('v'+i); if(!el) return;
  el.textContent=v;
  document.getElementById('b'+i).style.height=(v/255*100)+'%';
}

function poll(){
  fetch('api/status?port='+port).then(r=>r.json()).then(update).catch(()=>{});
}
function setPort(){ port=+document.getElementById('port').value; poll(); }
setInterval(poll, 300); poll(); build(); render();

function fill(cfg){
  document.getElementById('cfg-form').elements['ip'].value=cfg.ip;
  document.getElementById('cfg-form').elements['mask'].value=cfg.mask;
  document.getElementById('cfg-form').elements['dhcp'].checked=cfg.dhcp;
  document.getElementById('cfg-form').elements['name'].value=cfg.name||'';
  document.getElementById('cfg-form').elements['longname'].value=cfg.longname||'';
  var nets=cfg.net, subs=cfg.subnet, unis=cfg.universe;
  var dirs=cfg.direction||[0,0,0,0];
  UNIV_N=nets?nets.length:4;
  var rows=document.getElementById('univ-rows'); rows.innerHTML='';
  for(var i=0;i<(nets?nets.length:0);i++){
    var isIn=dirs[i]==1;
    var div=document.createElement('div');
    div.innerHTML='<label class="port">Port '+(i+1)+'</label>'+
      '<div class="tri"><span class="hp">dir</span><select name="d'+i+'">'+
        '<option value="0"'+(isIn?'':' selected')+'>output</option>'+
        '<option value="1"'+(isIn?' selected':'')+'>input</option></select></div>'+
      '<div class="tri"><span class="hp">net</span><input type="number" name="n'+i+'" min="0" max="127" value="'+nets[i]+'">'+
      '<span class="hp">subnet</span><input type="number" name="s'+i+'" min="0" max="15" value="'+subs[i]+'">'+
      '<span class="hp">universe</span><input type="number" name="u'+i+'" min="0" max="15" value="'+unis[i]+'"></div>';
    rows.appendChild(div);
  }
}

function toast(m){var t=document.getElementById('toast');t.textContent=m;t.style.display='block';setTimeout(function(){t.style.display='none';},2600);}

function save(){
  var e=document.getElementById('cfg-form').elements;
  var p=new URLSearchParams();
  p.set('ip',e['ip'].value.trim());
  p.set('mask',e['mask'].value.trim());
  p.set('dhcp',e['dhcp'].checked?'true':'false');
  p.set('name',e['name'].value.trim());
  p.set('longname',e['longname'].value.trim());
  for(var i=0;i<UNIV_N;i++){
    p.set('n'+i, e['n'+i].value);
    p.set('s'+i, e['s'+i].value);
    p.set('u'+i, e['u'+i].value);
    p.set('d'+i, e['d'+i].value);
  }
  fetch('api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()})
    .then(function(r){return r.json();}).then(function(j){
      toast(j.ok?'Saved. Rebooting...':'Error: '+j.error);
      if(j.ok) setTimeout(function(){location.reload(true);},2500);
    }).catch(function(){toast('Save failed');});
}
function req(p,cb){
  fetch('api/'+p,{method:'POST'}).then(function(r){return r.json();}).then(function(){toast('OK, rebooting...');cb&&cb();});
}
function reboot(){req('reboot',function(){setTimeout(function(){location.reload(true);},2500);});}
function factory(){ if(confirm('Reset all settings to defaults and reboot?')) req('factory',function(){setTimeout(function(){location.reload(true);},2500);}); }

fetch('api/config').then(r=>r.json()).then(fill).catch(function(){
  // still populate the universe fields with a sensible default
  fill({ip:'10.0.0.10',mask:'255.0.0.0',dhcp:true,net:[0,0,0,0],subnet:[0,0,0,0],universe:[0,1,2,3],direction:[0,0,0,0],name:'MagicQ Compact',longname:'MagicQ Compact Mini Connect'});
});
</script>
</body>
</html>
)HTML";