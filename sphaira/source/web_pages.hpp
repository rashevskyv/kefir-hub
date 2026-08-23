#pragma once

#include <string_view>

namespace sphaira::webpages {

constexpr std::string_view LIGHTBOX_CONTENT = R"HTML(
<style>
.lightbox{display:none;position:fixed;z-index:1000;top:0;left:0;width:100%;height:100%;background-color:rgba(0,0,0,0.95);align-items:center;justify-content:center;gap:14px;padding:0 14px;box-sizing:border-box;user-select:none}
.lightbox-content{position:relative;flex:0 1 auto;min-width:0;max-height:85%;display:flex;flex-direction:column;align-items:center}
.lightbox-img{max-width:100%;max-height:80vh;object-fit:contain;border-radius:4px;box-shadow:0 4px 20px rgba(0,0,0,0.5)}
.lightbox-caption{margin-top:12px;color:#eee;font-size:15px;text-align:center;word-break:break-all;max-width:600px}
.lightbox-close{position:absolute;top:20px;right:25px;color:#bbb;font-size:40px;font-weight:bold;cursor:pointer;transition:color 0.2s;line-height:1;z-index:1}
.lightbox-close:hover{color:#fff}
.lightbox-btn{flex:0 0 auto;background:rgba(40,45,50,0.5);border:1px solid rgba(255,255,255,0.1);color:#fff;font-size:24px;width:50px;height:50px;border-radius:50%;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:background 0.2s,color 0.2s}
.lightbox-btn:hover{background:rgba(60,65,70,0.8)}
@media (max-width: 600px) {
  .lightbox{gap:6px;padding:0 6px}
  .lightbox-btn{width:36px;height:36px;font-size:18px}
}
</style>
<div id="lightbox" class="lightbox">
<span class="lightbox-close" onclick="closeLightbox()">&times;</span>
<button class="lightbox-btn lightbox-prev" onclick="prevImage(event)">&lt;</button>
<div class="lightbox-content">
<img id="lightbox-img" class="lightbox-img" src="" alt="">
<div id="lightbox-caption" class="lightbox-caption"></div>
</div>
<button class="lightbox-btn lightbox-next" onclick="nextImage(event)">&gt;</button>
</div>
<script>
let imageList=[];let currentImageIndex=-1;
function isLightboxOpen(){const m=document.getElementById('lightbox');return !!m&&m.style.display==='flex';}
function initLightbox(){
imageList=[];
const links=document.querySelectorAll('a');
for(const link of links){
const href=link.getAttribute('href');
if(href&&href.includes('/view?path=')){
let name='';const span=link.querySelector('span:nth-child(2)')||link.querySelector('span');if(span){name=span.textContent;}else{const img=link.querySelector('img');if(img){name=img.getAttribute('alt')||'';}}
const idx=imageList.length;imageList.push({href:href,name:name});
if(link.dataset.lightboxBound)continue;
link.dataset.lightboxBound='1';
link.addEventListener('click',function(e){
e.preventDefault();
const i=imageList.findIndex(it=>it.href===link.getAttribute('href'));
openLightbox(i<0?idx:i);
});
}
}
}
document.addEventListener('keydown',function(e){
if(!isLightboxOpen())return;
if(e.key==='ArrowLeft'){e.preventDefault();e.stopImmediatePropagation();prevImage();}
else if(e.key==='ArrowRight'){e.preventDefault();e.stopImmediatePropagation();nextImage();}
else if(e.key==='Escape'||e.key==='Backspace'){e.preventDefault();e.stopImmediatePropagation();closeLightbox();}
},true);
function openLightbox(index){
if(index<0||index>=imageList.length)return;
currentImageIndex=index;
const modal=document.getElementById('lightbox');
const img=document.getElementById('lightbox-img');
const caption=document.getElementById('lightbox-caption');
img.src=imageList[index].href;caption.textContent=imageList[index].name;
modal.style.display='flex';
}
function closeLightbox(){document.getElementById('lightbox').style.display='none';}
function prevImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex-1;if(idx<0)idx=imageList.length-1;openLightbox(idx);}
function nextImage(e){if(e)e.stopPropagation();if(imageList.length<=1)return;let idx=currentImageIndex+1;if(idx>=imageList.length)idx=0;openLightbox(idx);}
document.addEventListener('DOMContentLoaded',initLightbox);
initLightbox();
</script>
)HTML";

constexpr std::string_view CONFIRM_MODAL_CSS = R"HTML(
<style>
.modal{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(15,15,18,0.75);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);z-index:1100;display:flex;align-items:center;justify-content:center}
.modal-content{background:#181822;border:1px solid rgba(255,255,255,0.08);border-radius:16px;padding:24px;width:360px;max-width:90%;box-shadow:0 20px 40px rgba(0,0,0,0.6);display:flex;flex-direction:column;gap:20px;transform:scale(0.95);transition:transform 0.15s ease}
.modal-text{font-size:16px;font-weight:500;color:#f1f5f9;text-align:center;line-height:1.5;word-break:break-all}
.modal-buttons{display:flex;gap:12px}
.modal-btn{flex:1;padding:12px;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;border:1px solid transparent;transition:all 0.15s}
.yes-btn{background:#10b981;color:#fff;border-color:#10b981}
.yes-btn:hover{background:#059669}
.no-btn{background:#ef4444;color:#fff;border-color:#ef4444}
.no-btn:hover{background:#dc2626}
.key-badge{background:rgba(255,255,255,0.25);border-radius:4px;padding:2px 6px;font-size:11px;font-weight:700;border:1px solid rgba(255,255,255,0.4);box-shadow:0 2px 0 rgba(0,0,0,0.2)}
</style>
)HTML";

constexpr std::string_view CONFIRM_MODAL_HTML = R"HTML(
<div id="confirm-modal" class="modal" style="display:none;"><div class="modal-content"><div class="modal-text" id="confirm-text">Are you sure?</div><div class="modal-buttons"><button id="confirm-yes-btn" class="modal-btn yes-btn"><span class="key-badge">+</span> Yes</button><button id="confirm-no-btn" class="modal-btn no-btn"><span class="key-badge">B</span> No</button></div></div></div>
)HTML";

constexpr std::string_view CONFIRM_MODAL_JS = R"HTML(
let confirmPromiseResolve=null;
function showConfirmDialog(text){return new Promise(res=>{const m=document.getElementById('confirm-modal');const t=document.getElementById('confirm-text');if(!m||!t){res(confirm(text));return;}t.textContent=text;m.style.display='flex';confirmPromiseResolve=res;});}
function handleConfirmResult(res){const m=document.getElementById('confirm-modal');if(m)m.style.display='none';if(confirmPromiseResolve){const r=confirmPromiseResolve;confirmPromiseResolve=null;r(res);}}
document.addEventListener('keydown',function(e){const m=document.getElementById('confirm-modal');if(!m||m.style.display==='none')return;if(e.key==='+'||e.key==='='||e.key==='Add'){e.preventDefault();e.stopImmediatePropagation();handleConfirmResult(true);}else if(e.key==='b'||e.key==='B'||e.key==='Escape'||e.key==='Backspace'){e.preventDefault();e.stopImmediatePropagation();handleConfirmResult(false);}},true);
document.addEventListener('DOMContentLoaded',()=>{const y=document.getElementById('confirm-yes-btn');if(y)y.onclick=()=>handleConfirmResult(true);const n=document.getElementById('confirm-no-btn');if(n)n.onclick=()=>handleConfirmResult(false);});
)HTML";

constexpr std::string_view FOLDER_PAGE_HEADER = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kefir Hub Files</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0}
header{position:sticky;top:0;background:rgba(23,25,35,0.85);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);padding:16px 24px;border-bottom:1px solid rgba(255,255,255,0.08);z-index:10}
.header-top{display:flex;justify-content:space-between;align-items:center;gap:16px}
h1{font-size:22px;margin:0;font-weight:600;letter-spacing:-0.5px}
.crumbs{font-size:14px;color:#94a3b8;text-align:right;max-width:60%;word-break:break-all}
.crumbs a{color:#38bdf8;text-decoration:none}
.crumbs a:hover{text-decoration:underline}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:16px}
button{border:1px solid rgba(255,255,255,0.15);background:#1e293b;color:#fff;border-radius:8px;padding:8px 16px;font-size:14px;font-weight:500;cursor:pointer;transition:all 0.2s}
button:hover{background:#334155;border-color:rgba(255,255,255,0.25)}
button:disabled{opacity:0.5;cursor:not-allowed}
input{display:none}.status{color:#38bdf8;font-size:14px}
.container{padding:24px;max-width:1200px;margin:0 auto}
.list{display:flex;flex-direction:column;gap:8px}
.list .item{display:flex;align-items:center;gap:16px;padding:12px 16px;background:#1e1e24;border:1px solid rgba(255,255,255,0.05);border-radius:8px;color:inherit;text-decoration:none;transition:background 0.15s,transform 0.15s;scroll-margin-top:160px}
.list .item:hover{background:#272730;transform:translateY(-1px)}
.list .thumbnail-box{width:40px;height:40px;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,0.2);border-radius:6px;overflow:hidden;flex-shrink:0}
.list .thumbnail-box img{width:100%;height:100%;object-fit:cover}
.list .thumbnail-box svg{width:24px;height:24px}
.list .info{display:flex;flex-grow:1;align-items:center;min-width:0}
.list .name{font-weight:500;margin-right:16px;flex-grow:1;word-break:break-all}
.list .meta{color:#64748b;font-size:13px;flex-shrink:0;width:120px;text-align:right;margin-right:24px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:16px}
.grid .item{display:flex;flex-direction:column;background:#18181f;border:1px solid rgba(255,255,255,0.05);border-radius:12px;color:inherit;text-decoration:none;overflow:hidden;transition:all 0.2s;scroll-margin-top:160px}
.grid .item:hover{background:#202029;transform:translateY(-3px);box-shadow:0 10px 20px rgba(0,0,0,0.3);border-color:rgba(56,189,248,0.3)}
.grid .thumbnail-box{width:100%;aspect-ratio:16/10;display:flex;align-items:center;justify-content:center;background:#0d0d11;overflow:hidden;border-bottom:1px solid rgba(255,255,255,0.03)}
.grid .thumbnail-box img{width:100%;height:100%;object-fit:cover}
.grid .thumbnail-box svg{width:48px;height:48px}
.grid .info{padding:12px;display:flex;flex-direction:column;gap:4px;min-width:0}
.grid .name{font-size:14px;font-weight:500;word-break:break-all}
.grid .meta{color:#64748b;font-size:12px}
.empty{padding:40px;text-align:center;color:#64748b;font-size:16px}
.delete-btn{margin-left:8px;border:1px solid rgba(239,68,68,0.4);background:rgba(239,68,68,0.1);color:#f87171;border-radius:50%;width:28px;height:28px;display:inline-flex;align-items:center;justify-content:center;font-size:18px;font-weight:500;cursor:pointer;flex-shrink:0;transition:all 0.15s;padding:0;line-height:1}
.delete-btn:hover{background:rgba(239,68,68,0.25);border-color:rgba(239,68,68,0.7)}
.grid .delete-btn{position:absolute;top:12px;right:12px;margin:0;z-index:5}
.file-checkbox{-webkit-appearance:none;appearance:none;background:rgba(255,255,255,0.08);border:2px solid rgba(255,255,255,0.3);border-radius:4px;outline:none;cursor:pointer;transition:all 0.15s;display:block}
.file-checkbox:hover{border-color:#38bdf8;background:rgba(56,189,248,0.08)}
.file-checkbox:checked{background:#38bdf8;border-color:#38bdf8}
.file-checkbox:checked::after{content:'';position:absolute;border:solid #0f0f12;border-width:0 2px 2px 0;transform:rotate(45deg)}
.list .file-checkbox{position:relative;width:18px;height:18px;margin-right:12px;flex-shrink:0}
.list .file-checkbox:checked::after{left:5px;top:1px;width:4px;height:9px}
.grid .item{position:relative}
.grid .file-checkbox{position:absolute;top:12px;left:12px;width:20px;height:20px;margin:0;z-index:5}
.grid .file-checkbox:checked::after{left:6px;top:2px;width:4px;height:9px}
.item.focused{outline:2px solid #38bdf8;outline-offset:-2px;box-shadow:0 0 12px rgba(56,189,248,0.25)}
.queue-panel{position:fixed;top:0;right:-380px;width:340px;height:100%;background:rgba(20,20,25,0.95);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);border-left:1px solid rgba(255,255,255,0.08);box-shadow:-10px 0 30px rgba(0,0,0,0.5);transition:right 0.3s cubic-bezier(0.4,0,0.2,1);z-index:100;display:flex;flex-direction:column;padding:20px;box-sizing:border-box}
.queue-panel.open{right:0}
.queue-header{display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid rgba(255,255,255,0.08);padding-bottom:12px;margin-bottom:16px}
.queue-header h2{margin:0;font-size:18px;font-weight:600;color:#f1f5f9}
.queue-close{background:none;border:none;color:#94a3b8;font-size:24px;cursor:pointer;padding:0;line-height:1}
.queue-list{flex-grow:1;overflow-y:auto;display:flex;flex-direction:column;gap:12px;margin-bottom:16px;padding-right:4px}
.queue-item{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.05);border-radius:8px;padding:12px;position:relative;display:flex;flex-direction:column;gap:6px}
.queue-item-header{display:flex;justify-content:space-between;align-items:flex-start;gap:8px}
.queue-item-name{font-size:13px;font-weight:500;word-break:break-all;color:#e2e8f0;flex-grow:1}
.queue-item-cancel{background:none;border:none;color:#f87171;cursor:pointer;padding:0 4px;font-size:16px;font-weight:bold;line-height:1}
.queue-item-progress-bg{height:6px;background:rgba(255,255,255,0.08);border-radius:3px;overflow:hidden}
.queue-item-progress-fill{height:100%;background:#38bdf8;width:0;transition:width 0.1s ease}
.queue-item-meta{display:flex;justify-content:space-between;font-size:11px;color:#94a3b8}
.queue-footer{display:flex;gap:10px}
.queue-footer button{flex:1;padding:10px 14px}
#start-transfers-btn{background:#38bdf8;color:#0f0f12;border-color:#38bdf8}
#start-transfers-btn:hover{background:#0ea5e9;border-color:#0ea5e9}
.queue-item.completed .queue-item-progress-fill{background:#4ade80}
.queue-item.failed .queue-item-progress-fill{background:#f87171}
.queue-item-install-label{display:flex;align-items:center;gap:4px;font-size:11px;color:#cbd5e1;cursor:pointer}
.queue-item-install-label input{display:inline-block;margin:0;cursor:pointer}
@media (max-width: 600px) {
  header{padding:12px 16px}
  .header-top{flex-direction:column;align-items:flex-start;gap:6px}
  .crumbs{text-align:left;max-width:100%}
  .bar{gap:4px;margin-top:8px}
  button{padding:6px 10px}
  button .text{display:none}
  .list{gap:4px}
  .list .item{padding:8px 10px;gap:8px}
  .list .file-checkbox{margin-right:4px}
  .grid .file-checkbox{top:6px;left:6px}
  .list .name{font-size:13px}
  .meta-folder{display:none !important}
  .meta-size{font-size:10px;margin-right:0 !important;width:auto !important;text-align:right;margin-left:auto}
  .list .thumbnail-box{width:28px;height:28px}
  .delete-btn{display:none !important}
  .grid{grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:8px}
  .grid .info{padding:6px;gap:2px}
  .grid .name{font-size:11px}
  .grid .meta{font-size:9px}
}
</style></head><body>
<div id="transfer-overlay" style="display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(15,15,18,0.7);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);z-index:90;align-items:center;justify-content:center;flex-direction:column;gap:16px;box-sizing:border-box;">
<div style="font-size:24px;font-weight:600;color:#38bdf8;">Transfer in Progress</div>
<div style="color:#94a3b8;font-size:14px;text-align:center;max-width:400px;padding:0 20px;line-height:1.5;">Please wait while the console is processing file transfers or game installations. You can monitor the progress in the Queue panel on the right.</div>
<div id="transfer-progress-info" style="display:flex;flex-direction:column;align-items:center;"></div>
</div>
<div id="queue-panel" class="queue-panel"><div class="queue-header"><h2>Transfer Queue</h2><button class="queue-close" onclick="toggleQueuePanel()">&times;</button></div><div id="queue-list" class="queue-list"></div><div class="queue-footer"><button id="start-transfers-btn" onclick="startTransfers()">Start transfers</button><button id="clear-queue-btn" onclick="clearCompletedQueue()">Clear completed</button></div></div>
<header>
)HTML";

constexpr std::string_view FOLDER_PAGE_JS = R"HTML(
let transferQueue=[];let isTransferring=false;
async function pollServerStatus(){
try{
const res=await fetch('/status');
if(!res.ok)return;
const s=await res.json();
const ov=document.getElementById('transfer-overlay');
const info=document.getElementById('transfer-progress-info');
if(s.active){
if(info){
const pct=s.total>0?Math.min(100,Math.round((s.bytes/s.total)*100)):0;
info.innerHTML='<div style="font-size:14px;color:#e2e8f0;margin-top:4px;word-break:break-all;max-width:340px;text-align:center;">'+escapeHtml(s.name)+'</div>'+
'<div style="width:280px;height:10px;background:rgba(255,255,255,0.1);border-radius:5px;overflow:hidden;margin-top:10px;"><div style="height:100%;background:#38bdf8;width:'+pct+'%;transition:width 0.3s;"></div></div>'+
'<div style="font-size:13px;color:#38bdf8;margin-top:6px;">'+pct+'%</div>';
}
if(ov&&ov.style.display!=='flex')ov.style.display='flex';
}else if(!isTransferring){
if(ov)ov.style.display='none';
if(info)info.innerHTML='';
}
}catch(e){}
}
setInterval(pollServerStatus,1000);
pollServerStatus();
function toggleQueuePanel(){const p=document.getElementById('queue-panel');if(p)p.classList.toggle('open');}
function addFilesToUploadQueue(files){if(!files||!files.length)return;
for(const f of files){const isGame=/\.(nsp|nsz|xci|xcz)$/i.test(f.name);transferQueue.push({id:'up_'+Math.random().toString(36).substr(2,9),type:'upload',file:f,name:f.name,size:f.size,status:'pending',progress:0,speed:'',install:isGame,xhr:null,uploadPath:currentPath});}
updateQueueCount();renderQueue();const p=document.getElementById('queue-panel');if(p&&!p.classList.contains('open'))p.classList.add('open');document.getElementById('files').value='';}
function addSelectedToDownloadQueue(){const ch=document.querySelectorAll('.file-checkbox:checked');if(!ch.length)return;
for(const cb of ch){const p=cb.getAttribute('data-path');const dp=decodeURIComponent(p);const n=dp.split('/').pop()||'file';if(transferQueue.some(item=>item.type==='download'&&item.path===p))continue;transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:p,name:n,size:0,status:'pending',progress:0,speed:'',controller:null});}
for(const cb of ch)cb.checked=false;updateSelectCount();updateQueueCount();renderQueue();toggleQueuePanel();}
function updateQueueCount(){const b=document.getElementById('queue-toggle-btn');if(b){const active=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status)).length;const countEl=b.querySelector('.count');if(countEl)countEl.textContent='('+active+')';}}
function renderQueue(){const l=document.getElementById('queue-list');if(!l)return;l.innerHTML='';
for(const i of transferQueue){const el=document.createElement('div');el.className='queue-item '+i.status;const pct=Math.round(i.progress);const speed=i.speed?' · '+i.speed:'';const sizeStr=i.size?formatBytes(i.size):'Unknown size';
let st=i.status;if(i.status==='pending')st='Pending';else if(i.status==='uploading')st='Uploading';else if(i.status==='downloading')st='Downloading';else if(i.status==='installing')st='Installing...';else if(i.status==='completed')st='Completed';else if(i.status==='failed')st='Failed';else if(i.status==='cancelled')st='Cancelled';
let hdr='<div class="queue-item-header"><span class="queue-item-name">'+escapeHtml(i.name)+'</span>';if(['pending','uploading','downloading','installing'].includes(i.status)){hdr+='<button class="queue-item-cancel" onclick="cancelTransfer(\''+i.id+'\')">&times;</button>';}hdr+='</div>';
let inst='';if(i.type==='upload'&&i.status==='pending'&&/\.(nsp|nsz|xci|xcz)$/i.test(i.name)){inst='<label class="queue-item-install-label"><input type="checkbox" '+(i.install?'checked':'')+' onchange="toggleInstallOption(\''+i.id+'\',this.checked)">Install directly</label>';}else if(i.type==='upload'&&i.install){inst='<div style="font-size:11px;color:#c084fc">Direct Install mode</div>';}
el.innerHTML=hdr+'<div style="font-size:11px;color:#94a3b8;margin-bottom:4px">'+(i.type==='upload'?'Upload':'Download')+'</div>'+inst+'<div class="queue-item-progress-bg"><div class="queue-item-progress-fill" style="width:'+pct+'%"></div></div><div class="queue-item-meta"><span>'+pct+'%'+speed+'</span><span>'+sizeStr+'</span></div>';l.appendChild(el);}}
function toggleInstallOption(id,chk){const i=transferQueue.find(item=>item.id===id);if(i)i.install=chk;}
function formatBytes(b){if(b===0)return '0 Bytes';const k=1024;const sizes=['Bytes','KB','MB','GB'];const i=Math.floor(Math.log(b)/Math.log(k));return parseFloat((b/Math.pow(k,i)).toFixed(2))+' '+sizes[i];}
function escapeHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
function cancelTransfer(id){const i=transferQueue.find(item=>item.id===id);if(!i)return;
if(i.status==='uploading'&&i.xhr){i.xhr.abort();}else if(i.status==='downloading'&&i.controller){i.controller.abort();}
i.status='cancelled';i.progress=0;i.speed='';renderQueue();updateQueueCount();}
function clearCompletedQueue(){transferQueue=transferQueue.filter(i=>['pending','uploading','downloading','installing'].includes(i.status));renderQueue();updateQueueCount();}
async function startTransfers(){if(isTransferring)return;isTransferring=true;const btn=document.getElementById('start-transfers-btn');if(btn)btn.disabled=true;const ov=document.getElementById('transfer-overlay');if(ov)ov.style.display='flex';
try{while(true){const next=transferQueue.find(i=>i.status==='pending');if(!next)break;if(next.type==='upload')await uploadFileItem(next);else await downloadFileItem(next);updateQueueCount();renderQueue();}}finally{isTransferring=false;if(btn)btn.disabled=false;if(ov)ov.style.display='none';navigateTo(currentPath,false);}}
function uploadFileItem(item){return new Promise(res=>{item.status='uploading';renderQueue();const xhr=new XMLHttpRequest();item.xhr=xhr;let url='/upload?path='+encodeURIComponent(item.uploadPath)+'&name='+encodeURIComponent(item.name);if(item.install){url+='&install=1';item.status='installing';}xhr.open('PUT',url,true);let startTime=Date.now();let lastTime=startTime;let lastLoaded=0;
xhr.upload.addEventListener('progress',e=>{if(e.lengthComputable){const now=Date.now();item.progress=(e.loaded/e.total)*100;const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(e.loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=e.loaded;}if(item.install&&item.progress>=99){item.status='installing';}renderQueue();}});
xhr.onload=()=>{if(xhr.status===200){item.status='completed';item.progress=100;item.speed='';}else{item.status='failed';item.speed='Error: '+(xhr.responseText||xhr.statusText);}res();};
xhr.onerror=()=>{item.status='failed';item.speed='Network error';res();};
xhr.onabort=()=>{item.status='cancelled';res();};
xhr.send(item.file);});}
async function downloadFileItem(item){item.status='downloading';renderQueue();const ctrl=new AbortController();item.controller=ctrl;try{const res=await fetch('/download?path='+item.path,{signal:ctrl.signal});if(!res.ok)throw new Error(res.statusText);const len=res.headers.get('content-length');const total=len?parseInt(len,10):0;item.size=total;const reader=res.body.getReader();let loaded=0;let chunks=[];let lastTime=Date.now();let lastLoaded=0;
while(true){const {done,value}=await reader.read();if(done)break;chunks.push(value);loaded+=value.length;if(total)item.progress=(loaded/total)*100;const now=Date.now();const diff=(now-lastTime)/1000;if(diff>=0.5){const speed=(loaded-lastLoaded)/diff;item.speed=formatBytes(speed)+'/s';lastTime=now;lastLoaded=loaded;}renderQueue();}
const blob=new Blob(chunks);const dlUrl=URL.createObjectURL(blob);const a=document.createElement('a');a.href=dlUrl;a.download=item.name;document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(dlUrl);item.status='completed';item.progress=100;item.speed='';}catch(err){if(err.name==='AbortError')item.status='cancelled';else{item.status='failed';item.speed=err.message;}}}
function toggleViewMode(){
const container=document.getElementById('items-container');
const btn=document.getElementById('view-toggle');
if(container.classList.contains('list')){
container.classList.remove('list');
container.classList.add('grid');
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
localStorage.setItem('viewMode','grid');
}else{
container.classList.remove('grid');
container.classList.add('list');
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='Grid View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='⊞';
localStorage.setItem('viewMode','list');
}
}
document.addEventListener('DOMContentLoaded',()=>{
const container=document.getElementById('items-container');
const btn=document.getElementById('view-toggle');
const saved=localStorage.getItem('viewMode');
if(saved==='grid'&&container){
container.classList.remove('list');
container.classList.add('grid');
if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
}
}
});
async function deleteFile(e,path){e.preventDefault();e.stopPropagation();if(!await showConfirmDialog('Delete '+decodeURIComponent(path.replace(/\+/g,'%20')).split('/').pop()+'?'))return;const res=await fetch('/delete?path='+path,{method:'DELETE'});if(res.ok){navigateTo(currentPath,false);}else{alert('Delete failed: '+await res.text());}}
function updateSelectCount(){
const checked=document.querySelectorAll('.file-checkbox:checked');
const btn=document.getElementById('delete-selected');
if(btn){
btn.disabled=checked.length===0;
const countEl=btn.querySelector('.count');
if(countEl)countEl.textContent='('+checked.length+')';
}
const dlBtn=document.getElementById('download-selected');
if(dlBtn){
dlBtn.disabled=checked.length===0;
const countEl=dlBtn.querySelector('.count');
if(countEl)countEl.textContent='('+checked.length+')';
}
const selectAllBtn=document.getElementById('select-all-btn');
if(selectAllBtn){
const all=document.querySelectorAll('.file-checkbox');
const txt=selectAllBtn.querySelector('.text');
if(txt){
if(all.length&&checked.length===all.length){txt.textContent='Deselect All';}else{txt.textContent='Select All';}
}
}
}
function toggleSelectAll(){
const checkboxes=document.querySelectorAll('.file-checkbox');
const checked=document.querySelectorAll('.file-checkbox:checked');
const targetState=checked.length<checkboxes.length;
for(const cb of checkboxes){cb.checked=targetState;}
updateSelectCount();
}
async function deleteSelected(){
const checked=document.querySelectorAll('.file-checkbox:checked');
if(!checked.length)return;
if(!await showConfirmDialog('Delete '+checked.length+' selected files/folders?'))return;
const btn=document.getElementById('delete-selected');
if(btn)btn.disabled=true;
const status=document.getElementById('status');
let count=0;
for(const cb of checked){
count++;
status.textContent='Deleting ('+count+'/'+checked.length+')...';
const path=cb.getAttribute('data-path');
await fetch('/delete?path='+path,{method:'DELETE'});
}
status.textContent='Done';
navigateTo(currentPath,false);
}
function getFocusedItem(){return document.querySelector('.item.focused');}
function focusItem(item){if(!item)return;const prev=getFocusedItem();if(prev)prev.classList.remove('focused');item.classList.add('focused');item.scrollIntoView({block:'nearest'});}
function navigateGrid(direction){
const current=getFocusedItem();if(!current){focusItem(document.querySelector('.item'));return;}
const items=Array.from(document.querySelectorAll('.item'));
const currRect=current.getBoundingClientRect();
const currCenterX=currRect.left+currRect.width/2;const currCenterY=currRect.top+currRect.height/2;
let bestMatch=null;let minDistance=Infinity;
for(const item of items){
if(item===current)continue;
const rect=item.getBoundingClientRect();
const centerX=rect.left+rect.width/2;const centerY=rect.top+rect.height/2;
if(direction==='down'&&rect.top>=currRect.bottom-5){
const dy=centerY-currCenterY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;
if(dist<minDistance){minDistance=dist;bestMatch=item;}
}else if(direction==='up'&&rect.bottom<=currRect.top+5){
const dy=currCenterY-centerY;const dx=centerX-currCenterX;const dist=dy*dy+dx*dx*5;
if(dist<minDistance){minDistance=dist;bestMatch=item;}
}
}
if(bestMatch)focusItem(bestMatch);
}
document.addEventListener('mouseover',function(e){const item=e.target.closest('.item');if(item)focusItem(item);});
document.addEventListener('dragstart',function(e){e.preventDefault();});
document.addEventListener('keydown',function(e){
const m=document.getElementById('confirm-modal');if(m&&m.style.display==='flex')return;
if(e.target.tagName==='INPUT'&&e.target.type!=='checkbox')return;
const items=Array.from(document.querySelectorAll('.item'));if(!items.length)return;
const current=getFocusedItem();if(!current){focusItem(items[0]);return;}
const isGrid=document.getElementById('items-container').classList.contains('grid');
if(e.key==='ArrowDown'){
e.preventDefault();
if(isGrid)navigateGrid('down');else{const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}
}else if(e.key==='ArrowUp'){
e.preventDefault();
if(isGrid)navigateGrid('up');else{const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}
}else if(e.key==='ArrowLeft'){
if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx>0)focusItem(items[idx-1]);}
}else if(e.key==='ArrowRight'){
if(isGrid){e.preventDefault();const idx=items.indexOf(current);if(idx<items.length-1)focusItem(items[idx+1]);}
}else if(e.key===' '){
e.preventDefault();
const cb=current.querySelector('.file-checkbox');if(cb){cb.checked=!cb.checked;updateSelectCount();}
}else if(e.key==='Escape'){
e.preventDefault();
const checkboxes=document.querySelectorAll('.file-checkbox');for(const cb of checkboxes)cb.checked=false;
updateSelectCount();
}else if(e.key==='Delete'){
e.preventDefault();
const checked=document.querySelectorAll('.file-checkbox:checked');
if(checked.length>0){deleteSelected();}
else{
const path=current.querySelector('.file-checkbox')?.getAttribute('data-path');
if(path){
showConfirmDialog('Delete '+current.querySelector('.name').textContent+'?').then(async (approved)=>{
if(approved){await fetch('/delete?path='+path,{method:'DELETE'});navigateTo(currentPath,false);}
});
}
}
}else if(e.key==='Enter'){
e.preventDefault();
current.click();
}else if(e.key==='Backspace'){
e.preventDefault();
goToParent();
}
});
function makeCRCTable(){
let c;const crcTable=[];
for(let n=0;n<256;n++){
c=n;
for(let k=0;k<8;k++){
c=((c&1)?(0xEDB88320^(c>>>1)):(c>>>1));
}
crcTable[n]=c;
}
return crcTable;
}
const crcTable=makeCRCTable();
function crc32(arr){
let crc=0^(-1);
for(let i=0;i<arr.length;i++){
crc=(crc>>>8)^crcTable[(crc^arr[i])&0xFF];
}
return (crc^(-1))>>>0;
}
function createZip(files){
const localHeaders=[];const centralHeaders=[];let offset=0;
for(const file of files){
const nameBytes=new TextEncoder().encode(file.name);
const dataBytes=file.data;
const crc=crc32(dataBytes);
const size=dataBytes.length;
const lh=new ArrayBuffer(30+nameBytes.length);
const lhView=new DataView(lh);
lhView.setUint32(0,0x04034b50,true);
lhView.setUint16(4,10,true);
lhView.setUint16(6,0,true);
lhView.setUint16(8,0,true);
lhView.setUint16(10,0,true);
lhView.setUint16(12,0,true);
lhView.setUint32(14,crc,true);
lhView.setUint32(18,size,true);
lhView.setUint32(22,size,true);
lhView.setUint16(26,nameBytes.length,true);
lhView.setUint16(28,0,true);
new Uint8Array(lh,30).set(nameBytes);
localHeaders.push(new Uint8Array(lh));
localHeaders.push(dataBytes);
const ch=new ArrayBuffer(46+nameBytes.length);
const chView=new DataView(ch);
chView.setUint32(0,0x02014b50,true);
chView.setUint16(4,20,true);
chView.setUint16(6,10,true);
chView.setUint16(8,0,true);
chView.setUint16(10,0,true);
chView.setUint16(12,0,true);
chView.setUint16(14,0,true);
chView.setUint32(16,crc,true);
chView.setUint32(20,size,true);
chView.setUint32(24,size,true);
chView.setUint16(28,nameBytes.length,true);
chView.setUint16(30,0,true);
chView.setUint16(32,0,true);
chView.setUint16(34,0,true);
chView.setUint16(36,0,true);
chView.setUint32(38,0,true);
chView.setUint32(42,offset,true);
new Uint8Array(ch,46).set(nameBytes);
centralHeaders.push(new Uint8Array(ch));
offset+=lh.byteLength+size;
}
const cdOffset=offset;let cdSize=0;
for(const ch of centralHeaders)cdSize+=ch.byteLength;
const eocd=new ArrayBuffer(22);
const eocdView=new DataView(eocd);
eocdView.setUint32(0,0x06054b50,true);
eocdView.setUint16(4,0,true);
eocdView.setUint16(6,0,true);
eocdView.setUint16(8,files.length,true);
eocdView.setUint16(10,files.length,true);
eocdView.setUint32(12,cdSize,true);
eocdView.setUint32(16,cdOffset,true);
eocdView.setUint16(20,0,true);
const blobParts=[];
for(const part of localHeaders)blobParts.push(part);
for(const part of centralHeaders)blobParts.push(part);
blobParts.push(new Uint8Array(eocd));
return new Blob(blobParts,{type:'application/zip'});
}
async function addSelectedToDownloadQueue(){
const checked=document.querySelectorAll('.file-checkbox:checked');
if(!checked.length)return;
const status=document.getElementById('status');
if(status)status.textContent='Preparing download queue...';
for(const cb of checked){
const path=cb.getAttribute('data-path');
const decodedPath=decodeURIComponent(path);
const name=decodedPath.split('/').pop()||'file';
const isDir=cb.closest('.item').querySelector('.meta').textContent==='folder';
if(isDir){
if(status)status.textContent='Scanning folder '+name+'...';
try{
const res=await fetch('/list-recursive?path='+path);
if(res.ok){
const nested=await res.json();
for(const f of nested){
const fName=decodeURIComponent(f.path).substring(decodeURIComponent(currentPath).length);
const cleanFName=fName.startsWith('/')?fName.substring(1):fName;
if(!transferQueue.some(item=>item.type==='download'&&item.path===f.path)){
transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:f.path,name:cleanFName,size:f.size,status:'pending',progress:0,speed:'',controller:null});
}
}
}
}catch(e){console.error(e);}
}else{
if(!transferQueue.some(item=>item.type==='download'&&item.path===path)){
let sizeVal=0;
const sizeMeta=cb.closest('.item').querySelector('.meta').textContent;
if(sizeMeta.includes('MiB'))sizeVal=parseFloat(sizeMeta)*1024*1024;
else if(sizeMeta.includes('KiB'))sizeVal=parseFloat(sizeMeta)*1024;
transferQueue.push({id:'dl_'+Math.random().toString(36).substr(2,9),type:'download',path:path,name:name,size:sizeVal,status:'pending',progress:0,speed:'',controller:null});
}
}
}
for(const cb of checked)cb.checked=false;
updateSelectCount();updateQueueCount();renderQueue();
if(status)status.textContent='';
const panel=document.getElementById('queue-panel');
if(panel&&!panel.classList.contains('open'))panel.classList.add('open');
}
async function navigateTo(path,shouldPushState=true){
const status=document.getElementById('status');if(status)status.textContent='Loading...';
try{const res=await fetch('/list?path='+encodeURIComponent(path));if(!res.ok)throw new Error(res.statusText);
const data=await res.json();currentPath=data.path;
const pathDiv=document.querySelector('.path');if(pathDiv)pathDiv.textContent=data.path;
renderCrumbs(data.path);renderItems(data.path,data.entries);
if(shouldPushState){const newUrl=window.location.protocol+'//'+window.location.host+'/?path='+encodeURIComponent(data.path);window.history.pushState({path:data.path},'',newUrl);}
updateSelectCount();
const container=document.getElementById('items-container');const saved=localStorage.getItem('viewMode');const btn=document.getElementById('view-toggle');
if(saved==='grid'&&container){container.classList.remove('list');container.classList.add('grid');if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='List View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='☰';
}}
else if(container){container.classList.remove('grid');container.classList.add('list');if(btn){
if(btn.querySelector('.text'))btn.querySelector('.text').textContent='Grid View';
if(btn.querySelector('.icon'))btn.querySelector('.icon').textContent='⊞';
}}
if(typeof initLightbox==='function')initLightbox();
}catch(err){alert('Failed to load folder: '+err.message);}finally{if(status)status.textContent='';}}
/* where ".." goes from `path`, or null when there is nowhere up to go.
   "/" is the root page listing the mounted sources, shown only when there is
   more than the card to pick from -- it is a plain link, so the click handler
   below leaves it alone and the server renders it. */
function parentHrefFor(path){
if(path===srcRoot)return hasRoot?'/':null;
let parent=path;const lastSlash=parent.lastIndexOf('/');if(lastSlash!==-1)parent=parent.substring(0,lastSlash);
if(parent.length<srcRoot.length)parent=srcRoot;
return '/?path='+encodeURIComponent(parent);}
function goToParent(){
const href=parentHrefFor(currentPath);if(!href)return;
if(href==='/'){window.location.href='/';return;}
navigateTo(new URL(href,window.location.origin).searchParams.get('path')||'/');}
function renderCrumbs(path){
const container=document.querySelector('.crumbs');if(!container)return;
let html='<a href="/">Root</a>';
let accum=srcRoot==='/'?'':srcRoot;
if(accum)html+=' / <a href="/?path='+encodeURIComponent(accum)+'">'+escapeHtml(accum)+'</a>';
for(const part of path.substring(accum.length).split('/').filter(Boolean)){
if(!accum.endsWith('/'))accum+='/';
accum+=part;html+=' / <a href="/?path='+encodeURIComponent(accum)+'">'+escapeHtml(part)+'</a>';}
container.innerHTML=html;}
function renderItems(path,entries){
const container=document.getElementById('items-container');if(!container)return;container.innerHTML='';
const parentHref=parentHrefFor(path);
if(parentHref){
const el=document.createElement('a');el.className='item';el.href=parentHref;
el.innerHTML='<div class="thumbnail-box"><svg viewBox="0 0 24 24" fill="#ffca28"><path d="M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg></div><div class="info"><span class="name">..</span><span class="meta">parent folder</span></div>';
container.appendChild(el);
}
if(!entries||!entries.length){
const el=document.createElement('div');el.className='empty';el.textContent='Empty folder';container.appendChild(el);return;
}
for(const entry of entries){
let child=path;if(!child.endsWith('/'))child+='/';child+=entry.name;const encChild=encodeURIComponent(child);const nameEsc=escapeHtml(entry.name);
const el=document.createElement('a');el.className='item';let thumb='';
if(entry.type===0){
el.href='/?path='+encChild;
thumb='<svg viewBox="0 0 24 24" fill="#ffca28"><path d="M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>';
}else{
const ext=entry.name.split('.').pop().toLowerCase();const isImage=['png','jpg','jpeg','gif','bmp'].includes(ext);
el.href=isImage?('/view?path='+encChild):('/download?path='+encChild);
if(isImage)thumb='<img class="thumb" src="/view?path='+encChild+'" alt="" loading="lazy">';
else thumb='<svg viewBox="0 0 24 24" fill="#90a4ae"><path d="M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/></svg>';
}
let metaStr='folder';if(entry.type!==0){
if(entry.size>=1024*1024)metaStr=(entry.size/(1024*1024)).toFixed(2)+' MiB';
else metaStr=(entry.size/1024).toFixed(2)+' KiB';
}
el.innerHTML='<input type="checkbox" class="file-checkbox" data-path="'+encChild+'" onclick="event.stopPropagation(); updateSelectCount();">';
el.innerHTML+='<div class="thumbnail-box">'+thumb+'</div>';
el.innerHTML+='<div class="info"><span class="name">'+nameEsc+'</span><span class="meta">'+metaStr+'</span><button class="delete-btn" onclick="deleteFile(event,\''+encChild+'\')">&times;</button></div>';
container.appendChild(el);
}}
document.addEventListener('click',e=>{
const a=e.target.closest('a');if(!a)return;
if(e.target.tagName==='INPUT'||e.target.tagName==='BUTTON'||e.target.closest('.delete-btn'))return;
const href=a.getAttribute('href');
if(href&&href.startsWith('/?path=')){
e.preventDefault();const url=new URL(href,window.location.origin);const path=url.searchParams.get('path')||'/';navigateTo(path);
}
});
window.addEventListener('popstate',e=>{
const url=new URL(window.location.href);const path=url.searchParams.get('path')||'/';navigateTo(path,false);
});
</script>
)HTML";

// handoff page for the SteamGridDB api key: the phone signs in on
// steamgriddb.com (their own Steam login, nothing to do with us), copies the
// key and posts it back here, so nothing has to be typed on the console.
constexpr std::string_view APIKEY_PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SteamGridDB API key</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:24px;box-sizing:border-box}
.card{max-width:460px;width:100%}
h1{font-size:20px;margin:0 0 6px}
p{color:#94a3b8;font-size:14px;line-height:1.5;margin:0 0 18px}
ol{color:#94a3b8;font-size:14px;line-height:1.7;padding-left:20px;margin:0 0 18px}
a{color:#60a5fa}
input{width:100%;box-sizing:border-box;padding:14px;font-size:16px;border-radius:8px;border:1px solid #334155;background:#1e293b;color:#e2e8f0}
button{width:100%;margin-top:12px;padding:14px;font-size:16px;border:0;border-radius:8px;background:#2563eb;color:#fff;cursor:pointer}
button:disabled{background:#334155;color:#64748b}
.msg{margin-top:14px;font-size:14px;min-height:20px}
.ok{color:#4ade80}
.err{color:#f87171}
</style></head><body><div class="card">
<h1>SteamGridDB API key</h1>
<p>Kefir Hub needs a personal API key to look up icons.</p>
<ol>
<li>Open <a href="https://www.steamgriddb.com/profile/preferences/api" target="_blank" rel="noreferrer noopener">steamgriddb.com API preferences</a> and sign in with Steam.</li>
<li>Copy the key shown there.</li>
<li>Paste it below and press Send.</li>
</ol>
<input id="key" type="text" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="Paste the API key">
<button id="send">Send to console</button>
<div class="msg" id="msg"></div>
</div>
<script>
const key=document.getElementById('key'),send=document.getElementById('send'),msg=document.getElementById('msg');
send.addEventListener('click',async()=>{
  const value=key.value.trim();
  if(!value){msg.className='msg err';msg.textContent='Paste the key first.';return;}
  send.disabled=true;msg.className='msg';msg.textContent='Sending...';
  try{
    const res=await fetch('/apikey',{method:'POST',headers:{'Content-Type':'text/plain'},body:value});
    if(res.ok){msg.className='msg ok';msg.textContent='Sent. You can close this page.';}
    else{msg.className='msg err';msg.textContent='The console rejected the key.';send.disabled=false;}
  }catch(e){msg.className='msg err';msg.textContent='Could not reach the console.';send.disabled=false;}
});
</script>
</body></html>
)HTML";

constexpr std::string_view PROGRESS_PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kefir Hub Progress</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:24px;box-sizing:border-box}
.card{max-width:420px;width:100%;text-align:center}
h1{font-size:20px;margin:0 0 24px}
.name{font-size:15px;color:#94a3b8;word-break:break-all;margin-bottom:16px;min-height:20px}
.bar-bg{height:14px;background:rgba(255,255,255,0.08);border-radius:7px;overflow:hidden}
.bar-fill{height:100%;background:#38bdf8;width:0%;transition:width 0.3s ease}
.pct{margin-top:12px;font-size:28px;font-weight:600}
.idle{color:#64748b;font-size:15px}
</style></head><body>
<div class="card">
<h1>Kefir Hub Progress</h1>
<div id="content"><div class="idle">Waiting for activity&hellip;</div></div>
</div>
<script>
async function poll(){
try{
const res=await fetch('/status');
const s=await res.json();
const c=document.getElementById('content');
if(!s.active){c.innerHTML='<div class="idle">No transfer in progress</div>';return;}
const pct=s.total>0?Math.min(100,Math.round((s.bytes/s.total)*100)):0;
c.innerHTML='<div class="name">'+s.name.replace(/[&<>]/g,ch=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[ch]))+'</div>'+
'<div class="bar-bg"><div class="bar-fill" style="width:'+pct+'%"></div></div>'+
'<div class="pct">'+pct+'%</div>';
    }catch(e){}
}
poll();
setInterval(poll,1000);
</script>
</body></html>
)HTML";

constexpr std::string_view REMOTE_INPUT_PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Remote Input &bull; Kefir Hub</title>
<style>
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f0f12;color:#e2e8f0;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px;box-sizing:border-box}
.card{max-width:520px;width:100%;background:#18181b;border:1px solid #27272a;border-radius:12px;padding:24px;box-shadow:0 10px 25px rgba(0,0,0,0.5)}
h1{font-size:20px;margin:0 0 8px;color:#f4f4f5}
p{color:#a1a1aa;font-size:14px;line-height:1.5;margin:0 0 16px}
.hint{color:#a1a1aa;font-size:13px;line-height:1.4;margin:8px 0 0}
input,textarea{width:100%;box-sizing:border-box;padding:12px 14px;font-size:15px;border-radius:8px;border:1px solid #3f3f46;background:#27272a;color:#f4f4f5;outline:none;font-family:inherit}
input:focus,textarea:focus{border-color:#38bdf8;box-shadow:0 0 0 2px rgba(56,189,248,0.2)}
textarea{min-height:120px;resize:vertical}
.btn-row{display:flex;gap:10px;margin-top:14px}
button{flex:1;padding:12px;font-size:15px;font-weight:500;border:0;border-radius:8px;background:#0284c7;color:#fff;cursor:pointer;transition:background 0.2s}
button:hover{background:#0369a1}
button:disabled{background:#3f3f46;color:#71717a;cursor:not-allowed}
.btn-secondary{flex:0 0 auto;background:#3f3f46;color:#e4e4e7}
.btn-secondary:hover{background:#52525b}
.msg{margin-top:14px;font-size:14px;min-height:20px}
.ok{color:#4ade80}
.err{color:#f87171}
</style>
</head><body>
<div class="card">
<h1 id="title">Remote Input</h1>
<p id="guide">Send text or URL directly to Nintendo Switch.</p>
<div id="input-container">
  <input id="text-input" type="text" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="Paste or type a URL">
</div>
<p class="hint" id="hint">Paste or type the address, then Send.</p>
<div class="btn-row">
  <button id="paste-btn" class="btn-secondary" type="button">Paste</button>
  <button id="send-btn" type="button">Send to Switch</button>
</div>
<div class="msg" id="msg"></div>
</div>
<script>
const titleEl=document.getElementById('title'),guideEl=document.getElementById('guide'),container=document.getElementById('input-container'),pasteBtn=document.getElementById('paste-btn'),sendBtn=document.getElementById('send-btn'),msg=document.getElementById('msg'),hintEl=document.getElementById('hint');
let field=document.getElementById('text-input');
const isPhone=/Mobi|Android|iPhone|iPad/i.test(navigator.userAgent)||(navigator.maxTouchPoints>0&&matchMedia('(pointer:coarse)').matches);
if(!isPhone){pasteBtn.style.display='none';}
async function init(){
  try{
    const res=await fetch('/input/config');
    if(res.ok){
      const cfg=await res.json();
      if(cfg.title)titleEl.textContent=cfg.title;
      if(cfg.guide)guideEl.textContent=cfg.guide;
      if(cfg.multiline){
        container.innerHTML='<textarea id="text-input" spellcheck="false"></textarea>';
        field=document.getElementById('text-input');
      }
      if(cfg.placeholder)field.placeholder=cfg.placeholder;
      if(cfg.default_text)field.value=cfg.default_text;
    }
  }catch(e){}
  field.focus();
}
pasteBtn.addEventListener('click',async()=>{
  field.focus();
  if(field.select)field.select();
  if(window.isSecureContext&&navigator.clipboard&&navigator.clipboard.readText){
    try{
      const text=await navigator.clipboard.readText();
      if(text){field.value=text;msg.className='msg ok';msg.textContent='Pasted from clipboard.';return;}
    }catch(e){}
  }
  msg.className='msg';
  msg.textContent='Long-press the field and tap Paste.';
});
sendBtn.addEventListener('click',async()=>{
  const val=field.value.trim();
  if(!val){msg.className='msg err';msg.textContent='Paste or type the address first.';return;}
  sendBtn.disabled=true;pasteBtn.disabled=true;msg.className='msg';msg.textContent='Sending to console...';
  try{
    const res=await fetch('/input',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:val});
    if(res.ok){msg.className='msg ok';msg.textContent='✓ Sent successfully! You can close this page.';}
    else{msg.className='msg err';msg.textContent='Console rejected the input.';sendBtn.disabled=false;pasteBtn.disabled=false;}
  }catch(e){msg.className='msg err';msg.textContent='Could not connect to console.';sendBtn.disabled=false;pasteBtn.disabled=false;}
});
init();
</script>
</body></html>
)HTML";

} // namespace sphaira::webpages
