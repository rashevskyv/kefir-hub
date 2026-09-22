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
} // namespace sphaira::webpages
