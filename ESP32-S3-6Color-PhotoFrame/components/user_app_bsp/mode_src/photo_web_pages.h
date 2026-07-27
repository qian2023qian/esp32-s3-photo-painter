#pragma once

// C++ raw string literal — no escaping issues
// Client-side image processing: Canvas scale + Floyd-Steinberg dithering + BMP construction
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
body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:#c9d1d9;margin:0 auto;padding:12px;width:100%}
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
.compare img{width:100%;border-radius:6px;border:1px solid var(--border)}
.compare .label{font-size:11px;color:var(--muted);text-align:center;margin-top:2px}
.info-row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;font-size:clamp(12px,1.3vw,14px);border-bottom:1px solid var(--border)}
.info-row:last-child{border-bottom:none}
.info-row .key{color:var(--muted)}
.btn-row{display:flex;gap:6px;flex-wrap:wrap}
input{width:100%;padding:8px 10px;border-radius:6px;border:1px solid #30363d;background:var(--bg);color:#c9d1d9;font-size:clamp(12px,1.3vw,14px);margin-bottom:6px;outline:none}
input:focus{border-color:#58a6ff}
.btn{padding:clamp(6px,1vw,10px) clamp(10px,1.5vw,18px);border:none;border-radius:6px;cursor:pointer;font-size:clamp(12px,1.3vw,14px);font-weight:600;white-space:nowrap}
.btn-blue{background:#1f6feb;color:#fff}.btn-blue:hover{background:#388bfd}
.btn-red{background:#da3633;color:#fff}.btn-red:hover{background:#f85149}
.btn-green{background:#238636;color:#fff}
.btn-purple{background:#8250df;color:#fff}
.btn-sm{padding:2px 8px;font-size:11px}
/* toggle switch */
.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;font-size:14px}
.toggle-row .key{color:var(--muted)}
.toggle-sw{width:44px;height:24px;background:#30363d;border-radius:12px;position:relative;cursor:pointer;transition:background .2s;flex-shrink:0}
.toggle-sw.on{background:#238636}
.toggle-sw::after{content:'';width:18px;height:18px;background:#c9d1d9;border-radius:50%;position:absolute;top:3px;left:3px;transition:left .2s}
.toggle-sw.on::after{left:23px}
/* range slider - taller track + larger thumb for dragging */
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
details summary{font-size:clamp(12px,1.5vw,15px);color:var(--muted);cursor:pointer;padding:4px 0}
.upload-zone{border:2px dashed #30363d;border-radius:8px;padding:20px;text-align:center;cursor:pointer;margin:8px 0}
.upload-zone:hover{border-color:#58a6ff}
#upload-progress{display:none;height:4px;background:#30363d;border-radius:2px;margin:6px 0}
#upload-progress div{height:100%;background:#238636;border-radius:2px;width:0}
@media(min-width:768px){body{padding:16px}.dash{max-width:640px}}
@media(min-width:1024px){body{padding:20px}.dash{max-width:700px}}
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

<div class="card" id="wifi-card">
<h2>WiFi 配网</h2>
<details id="wifi-details">
<summary>展开配置</summary>
<p style="margin:6px 0;font-size:13px">AP: <b>PhotoFrame</b> / 密码: <b>12345678</b></p>
<button class="btn btn-blue" onclick="scanWifi()" style="margin-bottom:8px">扫描网络</button>
<div id="ssid-list"></div>
<input id="wifi-ssid" placeholder="SSID">
<input id="wifi-pass" type="password" placeholder="密码">
<button class="btn btn-blue" onclick="connectWifi()">连接</button>
<p class="msg" id="wifi-msg"></p>
<button class="btn btn-red" onclick="resetWifi()" style="margin-top:6px">重置 WiFi</button>
</details>
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
<input type="file" id="file-input" accept="image/*" style="display:none" onchange="fileChanged()">
</div>
<div class="compare" id="preview-area" style="display:none">
<div><img id="preview-orig" alt="原图"><div class="label">原图</div></div>
<div><img id="preview-processed" alt="处理后"><div class="label">墨水屏效果（6色）</div></div>
</div>
<div id="img-adjust" style="display:none">
<div class="info-row"><span class="key">亮度</span><input type="range" id="adj-br" min="-50" max="50" value="10" oninput="onAdjust()"><span class="adjust-val" id="val-br">+10</span></div>
<div class="info-row"><span class="key">对比度</span><input type="range" id="adj-ct" min="-50" max="50" value="20" oninput="onAdjust()"><span class="adjust-val" id="val-ct">+20</span></div>
<div class="info-row"><span class="key">饱和度</span><input type="range" id="adj-st" min="-50" max="50" value="20" oninput="onAdjust()"><span class="adjust-val" id="val-st">+20</span></div>
<div class="info-row"><span class="key">锐化</span><input type="range" id="adj-sh" min="0" max="100" value="50" oninput="onAdjust()"><span class="adjust-val" id="val-sh">50</span></div>
<div class="info-row"><span class="key">抖动模式</span><select id="adj-dither" onchange="onAdjust()"><option value="fs">Floyd-Steinberg</option><option value="atkinson">Atkinson</option></select></div>
<div class="info-row"><span class="key">扩散强度</span><input type="range" id="adj-df" min="0" max="200" value="100" oninput="onAdjust()"><span class="adjust-val" id="val-df">100</span></div>
</div>
<div class="btn-row" id="action-row" style="display:none">
<button class="btn btn-purple" onclick="rotateImg()" style="flex:1">旋转 0°</button>
<button class="btn btn-green" onclick="uploadPhoto()" style="flex:2">显示到屏幕</button>
</div>
<div id="upload-progress"><div></div></div>
<p class="msg" id="upload-msg"></p>
</div>

<div class="card">
<h2>图片列表 (<span id="photo-count">0</span>)</h2>
<div id="photo-list" style="max-height:200px;overflow-y:auto"><p style="color:var(--muted);font-size:12px">加载中...</p></div>
</div>

<div class="card">
<h2>设置</h2>
<div class="info-row"><span class="key">轮播间隔 (分钟)</span><input id="set-interval" type="number" min="1" max="1440" style="width:120px;margin:0"></div>
<div class="toggle-row"><span class="key">自动轮播</span><span class="toggle-sw on" id="set-running" onclick="toggleRunning()"></span></div>
<button class="btn btn-green" onclick="saveSettings()">保存设置</button>
</div>

<div class="card">
<button class="btn btn-red" onclick="rebootDevice()">重启设备</button>
</div>
</div>

<script>
var $=function(id){return document.getElementById(id)};
var rotateAngle=0, processedBmp=null, fileFlag=0, adjLoaded=false;

function setMsg(id,text,ok){var e=$(id);e.textContent=text;e.className='msg '+(ok?'msg-ok':'msg-err')}
async function api(method,path,body){
var opts={method:method,headers:{}};if(body){opts.headers['Content-Type']='application/json';opts.body=JSON.stringify(body)}
var r=await fetch(path,opts);return r.status<400?r.json():Promise.reject(r)}

function updateStatus(){
api('GET','/api/status').then(function(d){
var tag=$('wifi-tag');tag.textContent='WiFi: '+(d.wifi?'已连接':'未连接');
tag.className='wifi-tag '+(d.wifi?'wifi-on':'wifi-off');
$('img-count').textContent=d.img_count;$('img-index').textContent=d.img_index;
$('img-interval').textContent=d.interval;$('img-running').textContent=d.running?'运行中':'已暂停';
$('s-temp').textContent=d.temperature.toFixed(1);$('s-rh').textContent=d.humidity.toFixed(1);
if(d.wifi){$('wifi-details').open=false}
// 首次加载时填充调整参数
if(!adjLoaded&&d.img_adj){
var a=typeof d.img_adj==='string'?JSON.parse(d.img_adj):d.img_adj;
if(a.br!==undefined)$('adj-br').value=a.br;
if(a.ct!==undefined)$('adj-ct').value=a.ct;
if(a.st!==undefined)$('adj-st').value=a.st;
if(a.sh!==undefined)$('adj-sh').value=a.sh;
if(a.dither)$('adj-dither').value=a.dither;
if(a.df!==undefined)$('adj-df').value=a.df;
updateAdjLabels();adjLoaded=true}}).catch(function(){})}

function refreshPhotos(){
api('GET','/api/photos').then(function(d){
$('photo-count').textContent=d.total;var h='';(d.files||[]).forEach(function(f){
h+='<div class="info-row"><span>'+f+'</span><button class="btn btn-red btn-sm" onclick="delPhoto(\''+f+'\')">x</button></div>'});
$('photo-list').innerHTML=h||'<p style="color:var(--muted);font-size:12px">暂无图片</p>'})}
function delPhoto(n){if(!confirm('删除 '+n+'?'))return;api('POST','/api/delete',{name:n}).then(function(){refreshPhotos();updateStatus()})}

function scanWifi(){
$('ssid-list').innerHTML='<p style="color:var(--muted);font-size:13px">扫描中...</p>';
api('GET','/api/wifi/scan').then(function(d){var h='';(d.networks||[]).forEach(function(s){
h+='<div class="ssid-item" onclick="$(\'wifi-ssid\').value=\''+s+'\'">'+s+'</div>'});
$('ssid-list').innerHTML=h||'<p style="color:var(--muted)">未发现网络</p>'})}
function connectWifi(){
var s=$('wifi-ssid').value.trim(),p=$('wifi-pass').value.trim();
if(!s)return setMsg('wifi-msg','请输入SSID',0);
setMsg('wifi-msg','连接中...',1);
api('POST','/api/wifi/connect',{ssid:s,password:p}).then(function(d){setMsg('wifi-msg',d.ok?'已保存，正在连接...':'失败',d.ok)})}
function resetWifi(){if(!confirm('确定重置WiFi？'))return;
api('POST','/api/wifi/reset').then(function(){setMsg('wifi-msg','已重置，AP已重启',1);$('wifi-details').open=true})}

// ---- Client-side image processing ----
var palette=[[0,0,0],[255,255,255],[255,255,0],[255,0,0],null,[0,0,255],[0,255,0]];
var paletteLuma=[0,1.0,0.6,0.25,null,0.4,0.35];
function nearestIdx(r,g,b){
var best=0,min=Infinity;
var luma1=(r*250+g*350+b*400)/255000;
for(var i=0;i<palette.length;i++){
if(!palette[i])continue;
var dr=r-palette[i][0],dg=g-palette[i][1],db=b-palette[i][2];
var rgb_dist=(dr*dr*0.250+dg*dg*0.350+db*db*0.400)*0.75/65025;
var ld=luma1-paletteLuma[i];ld*=ld;
var d=1.5*rgb_dist+0.60*ld;
if(d<min){min=d;best=i}}return best}

function clamp(v){return Math.max(0,Math.min(255,v))}

// 锐化: 3x3 卷积核 [0,-s,0; -s,1+4s,-s; 0,-s,0] 强度 s=0..1
function sharpenImage(src,w,h,strength){
if(strength<=0)return src;
var s=strength/100, ctr=1+4*s, nbr=-s;
var dst=new Uint8ClampedArray(src.length);
for(var y=0;y<h;y++){for(var x=0;x<w;x++){
var i=(y*w+x)*4;
var r=src[i]*ctr,g=src[i+1]*ctr,b=src[i+2]*ctr;
var k=0;
if(x>0){var j=i-4;r+=src[j]*nbr;g+=src[j+1]*nbr;b+=src[j+2]*nbr;k++}
if(x+1<w){var j=i+4;r+=src[j]*nbr;g+=src[j+1]*nbr;b+=src[j+2]*nbr;k++}
if(y>0){var j=i-w*4;r+=src[j]*nbr;g+=src[j+1]*nbr;b+=src[j+2]*nbr;k++}
if(y+1<h){var j=i+w*4;r+=src[j]*nbr;g+=src[j+1]*nbr;b+=src[j+2]*nbr;k++}
dst[i]=clamp(r);dst[i+1]=clamp(g);dst[i+2]=clamp(b);dst[i+3]=src[i+3]}}
return dst}

function processImage(img,w,h,br,ct,st,sharpen,dither,diffuse){
var c=document.createElement('canvas');c.width=w;c.height=h;
var ctx=c.getContext('2d');ctx.drawImage(img,0,0,w,h);
var id=ctx.getImageData(0,0,w,h),d=id.data;
var bf=1+br/100,cf=1+ct/100,sf=1+st/100;
// 亮度/对比度/饱和度
for(var y=0;y<h;y++){for(var x=0;x<w;x++){
var i=(y*w+x)*4,r=d[i],g=d[i+1],b=d[i+2];
r=clamp(r*bf);g=clamp(g*bf);b=clamp(b*bf);
r=clamp((r-128)*cf+128);g=clamp((g-128)*cf+128);b=clamp((b-128)*cf+128);
var gray=0.299*r+0.587*g+0.114*b;
r=clamp(gray+(r-gray)*sf);g=clamp(gray+(g-gray)*sf);b=clamp(gray+(b-gray)*sf);
d[i]=r;d[i+1]=g;d[i+2]=b}}
// 锐化
if(sharpen>0){d=sharpenImage(d,w,h,sharpen)}
// 抖动量化
var df=diffuse/100;
if(dither==='atkinson'){
for(var y=0;y<h;y++){for(var x=0;x<w;x++){
var i=(y*w+x)*4,or=d[i],og=d[i+1],ob=d[i+2];
var idx=nearestIdx(or,og,ob),pr=palette[idx][0],pg=palette[idx][1],pb=palette[idx][2];
d[i]=pr;d[i+1]=pg;d[i+2]=pb;
var er=(or-pr)*df,eg=(og-pg)*df,eb=(ob-pb)*df;
if(x+1<w){var j=i+4;d[j]=clamp(d[j]+er/8);d[j+1]=clamp(d[j+1]+eg/8);d[j+2]=clamp(d[j+2]+eb/8)}
if(y+1<h){
if(x>0){var j=i+(w-1)*4;d[j]=clamp(d[j]+er/8);d[j+1]=clamp(d[j+1]+eg/8);d[j+2]=clamp(d[j+2]+eb/8)}
var j=i+w*4;d[j]=clamp(d[j]+er/4);d[j+1]=clamp(d[j+1]+eg/4);d[j+2]=clamp(d[j+2]+eb/4);
if(x+1<w){var j=i+(w+1)*4;d[j]=clamp(d[j]+er/8);d[j+1]=clamp(d[j+1]+eg/8);d[j+2]=clamp(d[j+2]+eb/8)}}}}}
else{
for(var y=0;y<h;y++){for(var x=0;x<w;x++){
var i=(y*w+x)*4,or=d[i],og=d[i+1],ob=d[i+2];
var idx=nearestIdx(or,og,ob),pr=palette[idx][0],pg=palette[idx][1],pb=palette[idx][2];
d[i]=pr;d[i+1]=pg;d[i+2]=pb;
var er=(or-pr)*df,eg=(og-pg)*df,eb=(ob-pb)*df;
if(x+1<w){var j=i+4;d[j]=clamp(d[j]+er*7/16);d[j+1]=clamp(d[j+1]+eg*7/16);d[j+2]=clamp(d[j+2]+eb*7/16)}
if(x>0&&y+1<h){var j=i+(w-1)*4;d[j]=clamp(d[j]+er*3/16);d[j+1]=clamp(d[j+1]+eg*3/16);d[j+2]=clamp(d[j+2]+eb*3/16)}
if(y+1<h){var j=i+w*4;d[j]=clamp(d[j]+er*5/16);d[j+1]=clamp(d[j+1]+eg*5/16);d[j+2]=clamp(d[j+2]+eb*5/16)}
if(x+1<w&&y+1<h){var j=i+(w+1)*4;d[j]=clamp(d[j]+er/16);d[j+1]=clamp(d[j+1]+eg/16);d[j+2]=clamp(d[j+2]+eb/16)}}}}
if(sharpen>0){id.data.set(d)}
ctx.putImageData(id,0,0);return c}

function getAdj(){return{
br:Number($('adj-br').value),ct:Number($('adj-ct').value),st:Number($('adj-st').value),
sh:Number($('adj-sh').value),dither:$('adj-dither').value,df:Number($('adj-df').value)}}

function fileChanged(){
var f=$('file-input').files[0];if(!f)return;
if(!f.type.startsWith('image/')){alert('请选择图片文件');return}
$('preview-area').style.display='grid';$('img-adjust').style.display='block';$('action-row').style.display='flex';
$('upload-msg').textContent='处理中...';rotateAngle=0;
var img=new Image();img.onload=function(){
var tw=800,th=480;if(img.naturalWidth<img.naturalHeight){tw=480;th=800}
$('preview-orig').src=URL.createObjectURL(f);
var a=getAdj();
var pc=processImage(img,tw,th,a.br,a.ct,a.st,a.sh,a.dither,a.df);
processedBmp=buildBmp(pc,tw,th);
$('preview-processed').src=pc.toDataURL();
$('upload-msg').textContent='就绪 ('+tw+'x'+th+' 6色)';fileFlag=1};
img.src=URL.createObjectURL(f);$('file-input').value=''}

var adjustTimer=null;
function updateAdjLabels(){
$('val-br').textContent=(Number($('adj-br').value)>=0?'+':'')+$('adj-br').value;
$('val-ct').textContent=(Number($('adj-ct').value)>=0?'+':'')+$('adj-ct').value;
$('val-st').textContent=(Number($('adj-st').value)>=0?'+':'')+$('adj-st').value;
$('val-sh').textContent=$('adj-sh').value;
$('val-df').textContent=$('adj-df').value}
function onAdjust(){
updateAdjLabels();
if(adjustTimer)clearTimeout(adjustTimer);
adjustTimer=setTimeout(function(){reprocessImage();saveAdjusted()},300)}
function reprocessImage(){
if(!fileFlag)return;
var img=new Image();img.onload=function(){
var tw=800,th=480;if(img.naturalWidth<img.naturalHeight){tw=480;th=800}
var a=getAdj();
var pc=processImage(img,tw,th,a.br,a.ct,a.st,a.sh,a.dither,a.df);
processedBmp=buildBmp(pc,tw,th);
$('preview-processed').src=pc.toDataURL()};
img.src=$('preview-orig').src}

function saveAdjusted(){
api('POST','/api/adjustments',getAdj()).then(function(){})}
function loadAdjusted(){
api('GET','/api/adjustments').then(function(d){
if(d.br!==undefined)$('adj-br').value=d.br;
if(d.ct!==undefined)$('adj-ct').value=d.ct;
if(d.st!==undefined)$('adj-st').value=d.st;
if(d.sh!==undefined)$('adj-sh').value=d.sh;
if(d.dither)$('adj-dither').value=d.dither;
if(d.df!==undefined)$('adj-df').value=d.df;
updateAdjLabels();adjLoaded=true})}

function buildBmp(canvas,w,h){
var rowSize=Math.ceil((3*w)/4)*4,padSize=rowSize*h;
var total=54+padSize,bmp=new Uint8Array(total),dv=new DataView(bmp.buffer);
dv.setUint8(0,0x42);dv.setUint8(1,0x4D);
dv.setUint32(2,total,true);dv.setUint32(10,54,true);
dv.setUint32(14,40,true);dv.setUint32(18,w,true);dv.setUint32(22,h,true);
dv.setUint16(26,1,true);dv.setUint16(28,24,true);dv.setUint32(34,padSize,true);
var ctx=canvas.getContext('2d'),id=ctx.getImageData(0,0,w,h),rgba=id.data;
var off=54;for(var y=h-1;y>=0;y--){var ro=0;
for(var x=0;x<w;x++){var i=(y*w+x)*4;
bmp[off+ro]=rgba[i+2];bmp[off+ro+1]=rgba[i+1];bmp[off+ro+2]=rgba[i];ro+=3}
while(ro%4){bmp[off+ro]=0;ro++}off+=rowSize}return bmp}

function rotateImg(){
if(!processedBmp||!fileFlag){alert('请先选择图片');return}
rotateAngle=(rotateAngle+90)%360;$('upload-msg').textContent='已旋转 '+rotateAngle+'°';
var img=new Image();img.onload=function(){
var tw=800,th=480,srcW=img.naturalWidth,srcH=img.naturalHeight;
if(srcW<srcH){tw=480;th=800}
var c=document.createElement('canvas'),ctx=c.getContext('2d');
if(rotateAngle%180==0){c.width=srcW;c.height=srcH}else{c.width=srcH;c.height=srcW}
ctx.save();ctx.translate(c.width/2,c.height/2);ctx.rotate(rotateAngle*Math.PI/180);
ctx.drawImage(img,-srcW/2,-srcH/2);ctx.restore();
var a=getAdj();
var pc=processImage(c,tw,th,a.br,a.ct,a.st,a.sh,a.dither,a.df);
processedBmp=buildBmp(pc,tw,th);
$('preview-processed').src=pc.toDataURL()};
img.src=URL.createObjectURL(new Blob([processedBmp],{type:'image/bmp'}))}

function uploadPhoto(){
if(!processedBmp||!fileFlag){alert('请先选择图片');return}
var p=$('upload-progress');p.style.display='block';
var x=new XMLHttpRequest();x.open('POST','/api/upload');
x.setRequestHeader('X-Filename','phone_photo.bmp');
x.upload.onprogress=function(e){p.firstChild.style.width=(e.loaded/e.total*100)+'%'};
x.onload=function(){p.style.display='none';setMsg('upload-msg',x.status==200?'已显示到屏幕！':'上传失败',x.status==200);
if(x.status==200){updateStatus();refreshPhotos()}};
x.send(processedBmp)}

function saveSettings(){
api('POST','/api/settings',{interval:parseInt($('set-interval').value)||60,running:$('set-running').classList.contains('on')}).then(function(d){setMsg('wifi-msg','已保存',1)})}
function updateToggle(on){var t=$('set-running');if(on){t.classList.add('on')}else{t.classList.remove('on')}}
function toggleRunning(){$('set-running').classList.toggle('on')}
function loadSettings(){api('GET','/api/status').then(function(d){$('set-interval').value=d.interval;updateToggle(d.running)})}
function rebootDevice(){if(confirm('重启设备？'))api('POST','/api/reboot').then(function(){setMsg('wifi-msg','重启中...',1)})}

setInterval(updateStatus,5000);loadSettings();loadAdjusted();updateStatus();refreshPhotos();
</script>
</body>
</html>
)====";
