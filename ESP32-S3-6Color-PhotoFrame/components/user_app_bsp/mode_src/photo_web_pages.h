#pragma once

// C++ raw string literal
// Dashboard with epdoptimize: calibrated Spectra 6 palette + professional dithering
static const char *WEB_INDEX_HTML = R"====(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PhotoPainter</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0d1117;--card:#161b22;--cell:#0d1117;--border:#21262d;--muted:#8b949e}
body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:#c9d1d9;margin:0 auto;padding:12px;width:100%;display:flex;justify-content:center}
.dash{width:100%}
.dash{display:grid;gap:10px}
.topbar{display:flex;justify-content:space-between;align-items:center;padding:10px 14px;background:var(--card);border-radius:10px}
.topbar h1{font-size:clamp(14px,2.5vw,20px);font-weight:600}
.wifi-tag{font-size:clamp(10px,1.5vw,13px);padding:3px 8px;border-radius:5px;white-space:nowrap}
.wifi-on{background:#238636;color:#fff}
.wifi-off{background:#30363d;color:var(--muted)}
.card{background:var(--card);border-radius:10px;padding:clamp(10px,1.5vw,18px)}
.card h2{font-size:clamp(12px,1.8vw,15px);color:var(--muted);margin-bottom:10px;font-weight:500}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:6px}
.cell{background:var(--cell);border-radius:8px;padding:clamp(6px,1vw,12px);text-align:center}
.cell .val{font-size:clamp(20px,4vw,36px);font-weight:bold;line-height:1.2}
.cell .unit{font-size:clamp(11px,1.5vw,14px);color:var(--muted)}
.cell .lbl{font-size:clamp(12px,1.5vw,15px);color:var(--muted);margin-bottom:3px;font-weight:500}
.val-green{color:#3fb950}
.val-blue{color:#58a6ff}
.val-red{color:#f85149}
.compare{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0}
.compare img{width:100%;height:220px;object-fit:contain;border-radius:6px;border:1px solid var(--border)}
.compare .label{font-size:11px;color:var(--muted);text-align:center;margin-top:2px}
.info-row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;font-size:clamp(12px,1.3vw,14px);border-bottom:1px solid var(--border)}
.info-row:last-child{border-bottom:none}
.info-row .key{color:var(--muted)}
.btn-row{display:flex;gap:6px;flex-wrap:wrap}
input{width:100%;padding:8px 10px;border-radius:6px;border:1px solid #30363d;background:var(--bg);color:#c9d1d9;font-size:clamp(12px,1.3vw,14px);margin-bottom:6px;outline:none}
input:focus{border-color:#58a6ff}
input[type=time]{padding:4px 8px;width:auto;margin:0}
.btn{padding:clamp(6px,1vw,10px) clamp(10px,1.5vw,18px);border:none;border-radius:6px;cursor:pointer;font-size:clamp(12px,1.3vw,14px);font-weight:600;white-space:nowrap}
.btn-blue{background:#1f6feb;color:#fff}.btn-blue:hover{background:#388bfd}
.btn-red{background:#da3633;color:#fff}.btn-red:hover{background:#f85149}
.btn-green{background:#238636;color:#fff}
.btn-purple{background:#8250df;color:#fff}
.btn-sm{padding:2px 8px;font-size:11px}
.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;font-size:14px}
.toggle-row .key{color:var(--muted)}
.toggle-sw{width:44px;height:24px;background:#30363d;border-radius:12px;position:relative;cursor:pointer;transition:background .2s;flex-shrink:0}
.toggle-sw.on{background:#238636}
.toggle-sw::after{content:'';width:18px;height:18px;background:#c9d1d9;border-radius:50%;position:absolute;top:3px;left:3px;transition:left .2s}
.toggle-sw.on::after{left:23px}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:20px;background:transparent;outline:none;margin:0;cursor:pointer;touch-action:none}
input[type=range]::-webkit-slider-runnable-track{height:8px;background:#30363d;border-radius:4px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:22px;height:22px;background:#58a6ff;border-radius:50%;margin-top:-7px;cursor:pointer;border:none}
input[type=range]::-moz-range-track{height:8px;background:#30363d;border-radius:4px;border:none}
input[type=range]::-moz-range-thumb{width:22px;height:22px;background:#58a6ff;border-radius:50%;border:none;cursor:pointer}
select{padding:6px 8px;border-radius:6px;border:1px solid #30363d;background:var(--bg);color:#c9d1d9;font-size:13px;outline:none}
.adjust-val{width:42px;text-align:right;font-size:12px;color:var(--muted);flex-shrink:0}
.ssid-item{padding:8px 10px;background:var(--cell);border-radius:6px;margin-bottom:4px;cursor:pointer;font-size:clamp(11px,1.2vw,14px)}
.ssid-item:hover{background:#1f2a3a}
.msg{font-size:clamp(10px,1.1vw,13px);margin-top:6px}
.msg-ok{color:#3fb950}
.msg-err{color:#f85149}
.photo-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:6px;max-height:300px;overflow-y:auto;padding:4px 0}
.photo-item{cursor:pointer;text-align:center;background:var(--cell);border-radius:6px;padding:4px;position:relative;transition:background .15s}
.photo-item:hover{background:#1f2a3a}
.photo-item img{width:100%;aspect-ratio:800/480;object-fit:cover;border-radius:3px;display:block}
.photo-item .pname{font-size:9px;color:var(--muted);margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.photo-item .del{position:absolute;top:2px;right:2px;background:#da3633;color:#fff;border:none;border-radius:50%;width:16px;height:16px;font-size:10px;line-height:14px;cursor:pointer;display:none}
.photo-item:hover .del{display:block}
@media(min-width:768px){.photo-grid{grid-template-columns:repeat(auto-fill,minmax(150px,1fr));max-height:400px}}
details summary{font-size:clamp(12px,1.5vw,15px);color:var(--muted);cursor:pointer;padding:4px 0}
details{margin-bottom:4px}
.upload-zone{border:2px dashed #30363d;border-radius:8px;padding:20px;text-align:center;cursor:pointer;margin:8px 0}
.upload-zone:hover{border-color:#58a6ff}
#upload-progress{display:none;height:4px;background:#30363d;border-radius:2px;margin:6px 0}
#upload-progress div{height:100%;background:#238636;border-radius:2px;width:0}
@media(min-width:768px){body{padding:16px}.dash{max-width:640px}.compare img{height:340px}}
@media(min-width:1024px){body{padding:20px}.dash{max-width:1100px}.compare img{height:400px}}
</style>
</head>
<body>
<div class="dash">
<div class="topbar">
<h1>PhotoPainter</h1>
<span class="wifi-tag wifi-off" id="wifi-tag">WiFi: --</span>
</div>
<div class="card">
<h2>传感器</h2>
<div class="grid2">
<div class="cell"><div class="lbl">温度</div><div class="val val-red" id="s-temp">--</div><div class="unit">℃</div></div>
<div class="cell"><div class="lbl">湿度</div><div class="val val-blue" id="s-rh">--</div><div class="unit">%</div></div>
</div>
</div>

<div class="card">
<h2>相框状态</h2>
<div class="grid2">
<div class="cell"><div class="lbl">图片数量</div><div class="val val-blue" id="img-count">--</div></div>
<div class="cell"><div class="lbl">当前索引</div><div class="val val-green" id="img-index">--</div></div>
<div class="cell"><div class="lbl">轮播间隔</div><div class="val" id="img-interval">--</div><div class="unit">分</div></div>
<div class="cell"><div class="lbl">运行状态</div><div class="val" id="img-running">--</div></div>
</div>
</div>
<div class="card">
<h2>上传图片</h2>
<div class="upload-zone" onclick="document.getElementById('file-input').click()">
<p style="color:var(--muted);font-size:13px">点击选择图片（支持 JPG/PNG/BMP）</p>
<input type="file" id="file-input" accept="image/*" style="display:none" onchange="window.fileChanged()">
</div>
<div class="compare" id="preview-area" style="display:none">
<div><img id="preview-orig" alt="原图"><div class="label">原图</div></div>
<div><img id="preview-processed" alt="处理后"><div class="label">墨水屏效果（校准色板）</div></div>
</div>
<div id="img-adjust" style="display:none">
<div class="info-row"><span class="key">亮度</span><input type="range" id="adj-br" min="-50" max="50" value="0" oninput="window.onAdjust()"><span class="adjust-val" id="val-br">0</span></div>
<div class="info-row"><span class="key">对比度</span><input type="range" id="adj-ct" min="-50" max="50" value="20" oninput="window.onAdjust()"><span class="adjust-val" id="val-ct">+20</span></div>
<div class="info-row"><span class="key">饱和度</span><input type="range" id="adj-st" min="-50" max="50" value="20" oninput="window.onAdjust()"><span class="adjust-val" id="val-st">+20</span></div>
<div class="info-row"><span class="key">锐化</span><input type="range" id="adj-sh" min="0" max="100" value="50" oninput="window.onAdjust()"><span class="adjust-val" id="val-sh">50</span></div>
<div class="info-row"><span class="key">管线</span><select id="adj-pipe" onchange="window.onPipeChange()"><option value="epdoptimize">epdoptimize</option><option value="opendisplay">OpenDisplay</option></select></div>
<div class="info-row"><span class="key">抖动模式</span><select id="adj-dither" onchange="window.onAdjust()"><option value="floydSteinberg">Floyd-Steinberg</option><option value="atkinson">Atkinson</option><option value="jarvis">Jarvis-Judice-Ninke</option><option value="stucki">Stucki</option><option value="burkes">Burkes</option><option value="sierra3">Sierra-3</option><option value="sierra2">Sierra-2</option></select></div>
<div class="info-row"><span class="key">扩散强度</span><input type="range" id="adj-df" min="0" max="200" value="100" oninput="window.onAdjust()"><span class="adjust-val" id="val-df">100</span></div>
</div>
<div class="btn-row" id="action-row" style="display:none">
<button class="btn btn-purple" onclick="window.rotateImg()" style="flex:1">旋转 0°</button>
<button class="btn btn-green" onclick="window.uploadPhoto()" style="flex:2">显示到屏幕</button>
</div>
<div id="upload-progress"><div></div></div>
<p class="msg" id="upload-msg"></p>
</div>
<div class="card">
<h2>设置</h2>

<details id="wifi-details" style="margin-bottom:8px">
<summary>WiFi 配网</summary>
<p style="margin:6px 0;font-size:13px">AP: <b>PhotoFrame</b> / 密码: <b>12345678</b></p>
<button class="btn btn-blue" onclick="window.scanWifi()" style="margin-bottom:8px">扫描网络</button>
<div id="ssid-list"></div>
<input id="wifi-ssid" placeholder="SSID">
<input id="wifi-pass" type="password" placeholder="密码">
<button class="btn btn-blue" onclick="window.connectWifi()">连接</button>
<p class="msg" id="wifi-msg"></p>
<button class="btn btn-red" onclick="window.resetWifi()" style="margin-top:6px">重置 WiFi</button>
</details>

<details style="margin-bottom:8px" open>
<summary>相框设置</summary>
<div class="info-row"><span class="key">轮播间隔 (分钟)</span><input id="set-interval" type="number" min="1" max="1440" style="width:120px;margin:0"></div>
<div class="toggle-row"><span class="key">自动轮播</span><span class="toggle-sw on" id="set-running" onclick="window.toggleRunning()"></span></div>
<div class="info-row"><span class="key">休眠开始</span><input id="set-sleep-start" type="time" value="23:00" style="width:120px;margin:0"></div>
<div class="info-row"><span class="key">休眠结束</span><input id="set-sleep-end" type="time" value="07:00" style="width:120px;margin:0"></div>
<button class="btn btn-green" onclick="window.saveSettings()" style="margin-top:6px">保存设置</button>
</details>

<button class="btn btn-red" onclick="window.rebootDevice()">重启设备</button>
</div>


<div class="card">
<h2>图片列表 (<span id="photo-count">0</span>)</h2>
<div class="photo-grid" id="photo-list"><p style="color:var(--muted);font-size:12px">加载中...</p></div>
</div>


</div>
<script type="module">
import { ditherImage, replaceColors, aitjcizeSpectra6Palette }
from '/lib/epdoptimize.js';
let odModule=null;
async function loadOD(){if(!odModule){odModule=await import('/lib/opendisplay.js')}return odModule}

let $=function(id){return document.getElementById(id)};
let rotateAngle=0, processedBmp=null, fileFlag=0, adjLoaded=false;
let sourceCanvas, calibratedCanvas, deviceCanvas;
let currentImgW=800, currentImgH=480;

function setMsg(id,text,ok){let e=$(id);e.textContent=text;e.className='msg '+(ok?'msg-ok':'msg-err')}
async function api(method,path,body){
let opts={method:method,headers:{}};if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body)}
let r=await fetch(path,opts);return r.status<400?r.json():Promise.reject(r)}

function updateStatus(){
api('GET','/api/status').then(function(d){
let tag=$('wifi-tag');tag.textContent='WiFi: '+(d.wifi?'已连接':'未连接');
tag.className='wifi-tag '+(d.wifi?'wifi-on':'wifi-off');
$('img-count').textContent=d.img_count;$('img-index').textContent=d.img_index;
$('img-interval').textContent=d.interval;$('img-running').textContent=d.running?'运行中':'已暂停';
$('s-temp').textContent=d.temperature.toFixed(1);$('s-rh').textContent=d.humidity.toFixed(1);
if(d.wifi){$('wifi-details').open=false}
if(!adjLoaded&&d.img_adj){
let a=typeof d.img_adj==='string'?JSON.parse(d.img_adj):d.img_adj;
if(a.br!==undefined)$('adj-br').value=a.br;
if(a.ct!==undefined)$('adj-ct').value=a.ct;
if(a.st!==undefined)$('adj-st').value=a.st;
if(a.sh!==undefined)$('adj-sh').value=a.sh;
if(a.dither)$('adj-dither').value=a.dither;
if(a.df!==undefined)$('adj-df').value=a.df;
updateAdjLabels();adjLoaded=true}}).catch(function(){})}

var calMap={"#000000":"#1f2226","#FFFFFF":"#b9c7c9","#FFFF00":"#c1bb1e","#FF0000":"#62201e","#0000FF":"#233f8e","#00FF00":"#35563a"};
function toCal(r,g,b){var h="#"+[r,g,b].map(function(v){return v.toString(16).padStart(2,"0")}).join("");return calMap[h.toUpperCase()]}
var switchCooldown=0;
function refreshPhotos(){
api('GET','/api/photos').then(function(d){
$('photo-count').textContent=d.total;let h='';(d.files||[]).forEach(function(f,idx){
h+='<div class="photo-item" onclick="window.switchPhoto('+idx+')"><img src="/api/photo?name='+encodeURIComponent(f)+'" onload="window.calibrateThumb(this)" loading="lazy"><span class="pname">'+f+'</span><button class="del" onclick="event.stopPropagation();window.delPhoto(\''+f+'\')">x</button></div>'});
$('photo-list').innerHTML=h||'<p style="color:var(--muted);font-size:12px">暂无图片</p>'})}
function calibrateThumb(img){
var cv=document.createElement('canvas');cv.width=img.naturalWidth;cv.height=img.naturalHeight;
var ctx=cv.getContext('2d');ctx.drawImage(img,0,0);
var id=ctx.getImageData(0,0,cv.width,cv.height);
for(var i=0;i<id.data.length;i+=4){
var cc=toCal(id.data[i],id.data[i+1],id.data[i+2]);
if(cc){var rr=parseInt(cc.slice(1,3),16),gg=parseInt(cc.slice(3,5),16),bb=parseInt(cc.slice(5,7),16);id.data[i]=rr;id.data[i+1]=gg;id.data[i+2]=bb}}
ctx.putImageData(id,0,0);img.src=cv.toDataURL()}
function switchPhoto(idx){
var now=Date.now();
if(now-switchCooldown<15000){setMsg('wifi-msg','请等待15秒后再切换',0);return}
switchCooldown=now;
api('POST','/api/switch',{index:idx}).then(function(r){
if(r.ok){updateStatus()}else{setMsg('wifi-msg',r.msg||'切换失败',0);switchCooldown=0}}).catch(function(){switchCooldown=0})}
var cv=document.createElement('canvas');cv.width=img.naturalWidth;cv.height=img.naturalHeight;
var ctx=cv.getContext('2d');ctx.drawImage(img,0,0);
var id=ctx.getImageData(0,0,cv.width,cv.height);
for(var i=0;i<id.data.length;i+=4){
var cc=toCal(id.data[i],id.data[i+1],id.data[i+2]);
if(cc){var rr=parseInt(cc.slice(1,3),16),gg=parseInt(cc.slice(3,5),16),bb=parseInt(cc.slice(5,7),16);id.data[i]=rr;id.data[i+1]=gg;id.data[i+2]=bb}}
ctx.putImageData(id,0,0);img.src=cv.toDataURL()}
var now=Date.now();
if(now-switchCooldown<15000){setMsg('wifi-msg','请等待15秒后再切换',0);return}
switchCooldown=now;
api('POST','/api/switch',{index:idx}).then(function(r){
if(r.ok){updateStatus()}else{setMsg('wifi-msg',r.msg||'切换失败',0);switchCooldown=0}}).catch(function(){switchCooldown=0})}
function delPhoto(n){if(!confirm('删除 '+n+'?'))return;api('POST','/api/delete',{name:n}).then(function(){refreshPhotos();updateStatus()})}

function scanWifi(){
$('ssid-list').innerHTML='<p style="color:var(--muted);font-size:13px">扫描中...</p>';
api('GET','/api/wifi/scan').then(function(d){let h='';(d.networks||[]).forEach(function(s){
h+='<div class="ssid-item" onclick="window.$(\'wifi-ssid\').value=\''+s+'\'">'+s+'</div>'});
$('ssid-list').innerHTML=h||'<p style="color:var(--muted)">未发现网络</p>'})}
function connectWifi(){
let s=$('wifi-ssid').value.trim(),p=$('wifi-pass').value.trim();
if(!s)return setMsg('wifi-msg','请输入SSID',0);
setMsg('wifi-msg','连接中...',1);
api('POST','/api/wifi/connect',{ssid:s,password:p}).then(function(d){setMsg('wifi-msg',d.ok?'已保存，正在连接...':'失败',d.ok)})}
function resetWifi(){if(!confirm('确定重置WiFi？'))return;
api('POST','/api/wifi/reset').then(function(){setMsg('wifi-msg','已重置，AP已重启',1);$('wifi-details').open=true})}

function saveSettings(){
api('POST','/api/settings',{interval:parseInt($('set-interval').value)||60,running:$('set-running').classList.contains('on'),sleep_start:$('set-sleep-start').value,sleep_end:$('set-sleep-end').value}).then(function(d){setMsg('wifi-msg','已保存',1)})}
function updateToggle(on){let t=$('set-running');if(on){t.classList.add('on')}else{t.classList.remove('on')}}
function toggleRunning(){$('set-running').classList.toggle('on')}
function loadSettings(){api('GET','/api/status').then(function(d){$('set-interval').value=d.interval;updateToggle(d.running);if(d.sleep_start)$('set-sleep-start').value=d.sleep_start;if(d.sleep_end)$('set-sleep-end').value=d.sleep_end})}
function rebootDevice(){if(confirm('重启设备？'))api('POST','/api/reboot').then(function(){setMsg('wifi-msg','重启中...',1)})}

function getAdj(){return{
br:Number($('adj-br').value),ct:Number($('adj-ct').value),st:Number($('adj-st').value),
sh:Number($('adj-sh').value),dither:$('adj-dither').value,df:Number($('adj-df').value),pipe:$('adj-pipe').value}}

function updateAdjLabels(){
$('val-br').textContent=(Number($('adj-br').value)>=0?'+':'')+$('adj-br').value;
$('val-ct').textContent=(Number($('adj-ct').value)>=0?'+':'')+$('adj-ct').value;
$('val-st').textContent=(Number($('adj-st').value)>=0?'+':'')+$('adj-st').value;
$('val-sh').textContent=$('adj-sh').value;
$('val-df').textContent=$('adj-df').value}

function saveAdjusted(){api('POST','/api/adjustments',getAdj()).then(function(){})}
function loadAdjusted(){
api('GET','/api/adjustments').then(function(d){
if(d.br!==undefined)$('adj-br').value=d.br;
if(d.ct!==undefined)$('adj-ct').value=d.ct;
if(d.st!==undefined)$('adj-st').value=d.st;
if(d.sh!==undefined)$('adj-sh').value=d.sh;
if(d.dither)$('adj-dither').value=d.dither;if(d.pipe)$('adj-pipe').value=d.pipe;
if(d.df!==undefined)$('adj-df').value=d.df;
updateAdjLabels();adjLoaded=true})}

function onPipeChange(){if(fileFlag){runPipeline(sourceCanvas,currentImgW,currentImgH)}}
function buildOptsOD(){
let br=Number($('adj-br').value), ct=Number($('adj-ct').value);
let st=Number($('adj-st').value), sh=Number($('adj-sh').value);
let df=Number($('adj-df').value);
return {
errorDiffusionMatrix: $('adj-dither').value,
ditheringType: 'errorDiffusion',
serpentine: true,
toneMapping: {mode:'contrast',exposure:br/50,saturation:st/50,contrast:ct/50,strength:df/100},
clarity: {amount:sh/100,radius:1},
dynamicRangeCompression: {mode:'auto',strength:0.5}
}}
function buildOpts(){
let br=Number($('adj-br').value), ct=Number($('adj-ct').value);
let st=Number($('adj-st').value), sh=Number($('adj-sh').value);
let df=Number($('adj-df').value);
return {
palette: aitjcizeSpectra6Palette,
errorDiffusionMatrix: $('adj-dither').value,
ditheringType: 'errorDiffusion',
serpentine: true,
colorMatching: 'lab',
toneMapping: {
mode: 'contrast',
exposure: br/100,
saturation: st/50,
contrast: ct/50,
strength: df/100
},
clarity: { amount: sh/100, radius: 1 },
dynamicRangeCompression: { mode: 'auto', strength: 0.5 },
processingEngine: 'js',
adjustmentEngine: 'js'
}}

var odDM={floydSteinberg:'FLOYD_STEINBERG',atkinson:'ATKINSON',jarvis:'JARVIS_JUDICE_NINKE',stucki:'STUCKI',burkes:'BURKES',sierra3:'SIERRA',sierra2:'SIERRA_LITE'};
async function runPipelineOD(srcCanvas, tw, th){
let od=await loadOD();
let imgData=srcCanvas.getContext('2d').getImageData(0,0,tw,th);
let mode=od.DitherMode[odDM[$('adj-dither').value]]||od.DitherMode.BURKES;
let r=od.ditherImage({width:tw,height:th,data:imgData.data},od.ColorScheme.BWGBRY,{mode:mode,serpentine:true});
let cal=document.getElementById('cb-cal'),dev=document.getElementById('cb-od');
if(!cal){cal=document.createElement('canvas');cal.id='cb-cal';document.body.appendChild(cal)}
cal.width=tw;cal.height=th;dev.width=tw;dev.height=th;
let ci=cal.getContext('2d').createImageData(tw,th);
for(let i=0;i<r.indices.length;i++){let c=r.palette[r.indices[i]];ci.data[i*4]=c.r;ci.data[i*4+1]=c.g;ci.data[i*4+2]=c.b;ci.data[i*4+3]=255}
cal.getContext('2d').putImageData(ci,0,0);
let di=dev.getContext('2d').createImageData(tw,th);
let devMap={0:[0,0,0],1:[255,255,255],2:[0,255,0],3:[0,0,255],4:[255,0,0],5:[255,255,0]};
for(let i=0;i<r.indices.length;i++){let dc=devMap[r.indices[i]]||devMap[1];di.data[i*4]=dc[0];di.data[i*4+1]=dc[1];di.data[i*4+2]=dc[2];di.data[i*4+3]=255}
dev.getContext('2d').putImageData(di,0,0);
$('preview-processed').src=cal.toDataURL();
processedBmp=buildBmp(dev, tw, th);
}

async function runPipeline(srcCanvas, tw, th){
if($('adj-pipe').value==='opendisplay'){return runPipelineOD(srcCanvas,tw,th)}
if(!calibratedCanvas){calibratedCanvas=document.createElement('canvas')}
if(!deviceCanvas){deviceCanvas=document.createElement('canvas')}
calibratedCanvas.width=tw;calibratedCanvas.height=th;
deviceCanvas.width=tw;deviceCanvas.height=th;
let opts=buildOpts();
await ditherImage(srcCanvas, calibratedCanvas, opts);
replaceColors(calibratedCanvas, deviceCanvas, aitjcizeSpectra6Palette);
$('preview-processed').src=calibratedCanvas.toDataURL();
processedBmp=buildBmp(deviceCanvas, tw, th);
}

function fileChanged(){
let f=$('file-input').files[0];if(!f)return;
if(!f.type.startsWith('image/')){alert('请选择图片文件');return}
$('preview-area').style.display='grid';$('img-adjust').style.display='block';$('action-row').style.display='flex';
$('upload-msg').textContent='处理中...';rotateAngle=0;
let img=new Image();img.onload=async function(){
let tw=800,th=480;if(img.naturalWidth<img.naturalHeight){tw=480;th=800}
currentImgW=tw;currentImgH=th;
$('preview-orig').src=URL.createObjectURL(f);
if(!sourceCanvas){sourceCanvas=document.createElement('canvas')}
sourceCanvas.width=tw;sourceCanvas.height=th;
let ctx=sourceCanvas.getContext('2d');ctx.drawImage(img,0,0,tw,th);
await runPipeline(sourceCanvas, tw, th);
$('upload-msg').textContent='就绪 ('+tw+'x'+th+' 校准色板)';fileFlag=1};
img.src=URL.createObjectURL(f);$('file-input').value=''}

let adjustTimer=null;
function onAdjust(){
updateAdjLabels();
if(adjustTimer)clearTimeout(adjustTimer);
adjustTimer=setTimeout(async function(){
if(fileFlag){await runPipeline(sourceCanvas, currentImgW, currentImgH)}
saveAdjusted()},300)}

function buildBmp(canvas,w,h){
let rowSize=Math.ceil((3*w)/4)*4,padSize=rowSize*h;
let total=54+padSize,bmp=new Uint8Array(total),dv=new DataView(bmp.buffer);
dv.setUint8(0,0x42);dv.setUint8(1,0x4D);
dv.setUint32(2,total,true);dv.setUint32(10,54,true);
dv.setUint32(14,40,true);dv.setUint32(18,w,true);dv.setUint32(22,h,true);
dv.setUint16(26,1,true);dv.setUint16(28,24,true);dv.setUint32(34,padSize,true);
let ctx=canvas.getContext('2d'),id=ctx.getImageData(0,0,w,h),rgba=id.data;
let off=54;for(let y=h-1;y>=0;y--){let ro=0;
for(let x=0;x<w;x++){let i=(y*w+x)*4;
bmp[off+ro]=rgba[i+2];bmp[off+ro+1]=rgba[i+1];bmp[off+ro+2]=rgba[i];ro+=3}
while(ro%4){bmp[off+ro]=0;ro++}off+=rowSize}return bmp}

function rotateImg(){
if(!processedBmp||!fileFlag){alert('请先选择图片');return}
rotateAngle=(rotateAngle+90)%360;$('upload-msg').textContent='已旋转 '+rotateAngle+'°';
let img=new Image();img.onload=async function(){
let srcW=img.naturalWidth,srcH=img.naturalHeight;
let tw=800,th=480;if(srcW<srcH){tw=480;th=800}
currentImgW=tw;currentImgH=th;
let c=document.createElement('canvas'),ctx=c.getContext('2d');
if(rotateAngle%180==0){c.width=srcW;c.height=srcH}else{c.width=srcH;c.height=srcW}
ctx.save();ctx.translate(c.width/2,c.height/2);ctx.rotate(rotateAngle*Math.PI/180);
ctx.drawImage(img,-srcW/2,-srcH/2);ctx.restore();
if(!sourceCanvas){sourceCanvas=document.createElement('canvas')}
sourceCanvas.width=tw;sourceCanvas.height=th;
let ctx2=sourceCanvas.getContext('2d');ctx2.drawImage(c,0,0,tw,th);
await runPipeline(sourceCanvas, tw, th)};
img.src=URL.createObjectURL(new Blob([processedBmp],{type:'image/bmp'}))}

function uploadPhoto(){
if(!processedBmp||!fileFlag){alert('请先选择图片');return}
let p=$('upload-progress');p.style.display='block';
let a=getAdj();let q='?name='+encodeURIComponent($('adj-pipe').value+'_'+a.dither+'_br'+a.br+'_ct'+a.ct+'_st'+a.st+'_sh'+a.sh+'_df'+a.df);
let x=new XMLHttpRequest();x.open('POST','/api/upload'+q);
x.upload.onprogress=function(e){p.firstChild.style.width=(e.loaded/e.total*100)+'%'};
x.onload=function(){p.style.display='none';setMsg('upload-msg',x.status==200?'已显示到屏幕！':'上传失败',x.status==200);
if(x.status==200){updateStatus();refreshPhotos()}};
x.send(processedBmp)}

window.$=$;
window.setMsg=setMsg;window.api=api;
window.updateStatus=updateStatus;window.refreshPhotos=refreshPhotos;
window.delPhoto=delPhoto;window.switchPhoto=switchPhoto;window.calibrateThumb=calibrateThumb;
window.scanWifi=scanWifi;window.connectWifi=connectWifi;window.resetWifi=resetWifi;
window.saveSettings=saveSettings;window.updateToggle=updateToggle;
window.toggleRunning=toggleRunning;window.loadSettings=loadSettings;
window.rebootDevice=rebootDevice;
window.getAdj=getAdj;window.updateAdjLabels=updateAdjLabels;
window.saveAdjusted=saveAdjusted;window.loadAdjusted=loadAdjusted;
window.fileChanged=fileChanged;window.onPipeChange=onPipeChange;window.onAdjust=onAdjust;
window.rotateImg=rotateImg;window.uploadPhoto=uploadPhoto;

setInterval(updateStatus,5000);loadSettings();loadAdjusted();updateStatus();refreshPhotos();
</script>
</body>
</html>
)====";
