#pragma once

#include <string_view>

namespace sphaira::webpages {

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
<form id="send-form" action="javascript:void(0)">
<div id="input-container">
  <input id="text-input" type="text" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="Paste or type a URL">
</div>
<p class="hint" id="hint">Paste or type the address, then Send.</p>
<div class="btn-row">
  <button id="paste-btn" class="btn-secondary" type="button">Paste</button>
  <button id="send-btn" type="submit">Send to Switch</button>
</div>
</form>
<div class="msg" id="msg"></div>
</div>
<script>
const titleEl=document.getElementById('title'),guideEl=document.getElementById('guide'),container=document.getElementById('input-container'),pasteBtn=document.getElementById('paste-btn'),sendBtn=document.getElementById('send-btn'),msg=document.getElementById('msg'),hintEl=document.getElementById('hint');
let field=document.getElementById('text-input');
let T={hint:'Type or paste the text, then press Send.',send:'Send',paste:'Paste',sending:'Sending to the console...',sent:'Sent. You can close this page.',rejected:'The console did not accept the text.',offline:'Could not reach the console.',empty:'Type or paste the text first.'};
const isPhone=/Mobi|Android|iPhone|iPad/i.test(navigator.userAgent)||(navigator.maxTouchPoints>0&&matchMedia('(pointer:coarse)').matches);
if(!isPhone){pasteBtn.style.display='none';}
async function init(){
  try{
    const res=await fetch('/input/config');
    if(res.ok){
      const cfg=await res.json();
      if(cfg.ui)T=Object.assign(T,cfg.ui);
      if(cfg.title)titleEl.textContent=cfg.title;
      if(cfg.guide&&cfg.guide!==cfg.title)guideEl.textContent=cfg.guide;else guideEl.style.display='none';
      if(cfg.multiline){
        container.innerHTML='<textarea id="text-input" spellcheck="false"></textarea>';
        field=document.getElementById('text-input');
      }
      if(cfg.secret)field.type='password';
      field.placeholder=cfg.placeholder||'';
      if(cfg.default_text)field.value=cfg.default_text;
      hintEl.textContent=T.hint;sendBtn.textContent=T.send;pasteBtn.textContent=T.paste;
    }
  }catch(e){}
  field.focus();
  if(field.tagName==='TEXTAREA'){
    field.addEventListener('keydown',e=>{
      if(e.key==='Enter'&&(e.ctrlKey||e.metaKey)){e.preventDefault();send();}
    });
  }else{
    bindUrlField();
  }
}
function collapseSchemes(s){
  s=(s||'').trim();
  while(/^(https?:\/\/)(?=https?:\/\/)/i.test(s)){
    s=s.replace(/^(https?:\/\/)/i,'');
  }
  return s;
}
function tidyField(){
  const c=collapseSchemes(field.value);
  if(c!==field.value)field.value=c;
}
function bindUrlField(){
  field.addEventListener('input',tidyField);
  field.addEventListener('change',tidyField);
  field.addEventListener('paste',()=>setTimeout(tidyField,0));
  field.addEventListener('focus',()=>{
    if(/^(https?:\/\/)$/i.test(field.value)&&field.select)field.select();
  });
  field.addEventListener('keydown',e=>{
    if(e.key!=='Enter')return;
    if(field.tagName==='TEXTAREA'&&!(e.ctrlKey||e.metaKey))return;
    e.preventDefault();
    send();
  });
  if(/^(https?:\/\/)$/i.test(field.value)&&field.select)field.select();
}
let sending=false;
async function send(){
  if(sending)return;
  const isArea=field.tagName==='TEXTAREA';
  const val=isArea?field.value:collapseSchemes(field.value);
  if(!isArea&&val)field.value=val;
  if(!val){msg.className='msg err';msg.textContent=T.empty;return;}
  sending=true;
  sendBtn.disabled=true;pasteBtn.disabled=true;msg.className='msg';msg.textContent=T.sending;
  try{
    const res=await fetch('/input',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:val});
    if(res.ok){msg.className='msg ok';msg.textContent='✓ '+T.sent;}
    else{msg.className='msg err';msg.textContent=T.rejected;sending=false;sendBtn.disabled=false;pasteBtn.disabled=false;}
  }catch(e){msg.className='msg err';msg.textContent=T.offline;sending=false;sendBtn.disabled=false;pasteBtn.disabled=false;}
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
document.getElementById('send-form').addEventListener('submit',e=>{e.preventDefault();send();});
init();
</script>
</body></html>
)HTML";

// Full-page file editor. CodeMirror 5 is loaded from a CDN in the browser so
// the NRO only ships this shell; if the CDN is unreachable a full-page
// textarea still works (line numbers and highlight are skipped).
constexpr std::string_view REMOTE_EDITOR_PAGE = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Edit &bull; Kefir Hub</title>
<style>
html,body{height:100%;margin:0;display:flex;flex-direction:column;background:#0f0f12;color:#e2e8f0;font-family:system-ui,-apple-system,sans-serif}
#bar{flex:0 0 auto;display:flex;align-items:center;gap:10px;padding:10px 14px;background:#18181b;border-bottom:1px solid #27272a}
#name{flex:1;min-width:0;font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#msg{font-size:13px;color:#a1a1aa}
button{padding:8px 14px;font-size:14px;font-weight:500;border:0;border-radius:8px;background:#0284c7;color:#fff;cursor:pointer}
button.secondary{background:#3f3f46;color:#e4e4e7}
button:disabled{background:#3f3f46;color:#71717a}
#veil{display:none;position:fixed;inset:0;z-index:50;background:rgba(9,9,11,.92);align-items:center;justify-content:center;padding:24px;text-align:center}
#veil.show{display:flex}
#veil h2{margin:0 0 10px;font-size:28px;color:#f4f4f5}
#veil p{margin:0;max-width:420px;font-size:16px;line-height:1.5;color:#a1a1aa}
#wrap{flex:1 1 auto;min-height:0;position:relative}
#ed{width:100%;height:100%;box-sizing:border-box;border:0;outline:none;resize:none;padding:12px 14px;background:#0f0f12;color:#e2e8f0;font:14px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace;tab-size:2}
#wrap .CodeMirror{position:absolute;inset:0;height:100%;width:100%;background:#0f0f12;color:#e2e8f0;font-size:14px;line-height:1.5;font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
#wrap .CodeMirror-gutters{background:#18181b;border-right:1px solid #27272a}
#wrap .CodeMirror-linenumber{color:#71717a}
#wrap .CodeMirror-cursor{border-left:1px solid #38bdf8}
#wrap .CodeMirror-selected{background:#1e3a5f}
#wrap .cm-s-default .cm-comment{color:#64748b}
#wrap .cm-s-default .cm-string,#wrap .cm-s-default .cm-string-2{color:#86efac}
#wrap .cm-s-default .cm-number,#wrap .cm-s-default .cm-atom{color:#fda4af}
#wrap .cm-s-default .cm-keyword,#wrap .cm-s-default .cm-meta,#wrap .cm-s-default .cm-builtin{color:#7dd3fc}
#wrap .cm-s-default .cm-def{color:#93c5fd}
#wrap .cm-s-default .cm-variable,#wrap .cm-s-default .cm-variable-2{color:#e2e8f0}
#wrap .cm-s-default .cm-property,#wrap .cm-s-default .cm-attribute{color:#cbd5e1}
#wrap .cm-s-default .cm-operator,#wrap .cm-s-default .cm-qualifier{color:#a1a1aa}
#wrap .cm-s-default .cm-tag{color:#c4b5fd}
.ok{color:#4ade80}.err{color:#f87171}
</style>
</head><body>
<div id="bar"><div id="name">file</div><div id="msg"></div><button type="button" id="save">Save to Switch</button><button type="button" id="save-close" class="secondary">Save and Close</button><button type="button" id="close" class="secondary">Close</button></div>
<div id="wrap"><textarea id="ed" spellcheck="false" wrap="off"></textarea></div>
<div id="veil"><div><h2 id="veil-title">Session closed</h2><p id="veil-msg">This page is no longer connected to the Switch. Typing here will not be saved.</p></div></div>
<script>
const CM='https://cdn.jsdelivr.net/npm/codemirror@5.65.16/';
const ed=document.getElementById('ed'),saveBtn=document.getElementById('save'),saveCloseBtn=document.getElementById('save-close'),closeBtn=document.getElementById('close'),msg=document.getElementById('msg'),veil=document.getElementById('veil');
let cm=null,dirty=false,closed=false,draftTimer=0,statusFails=0;
function val(){return cm?cm.getValue():ed.value;}
function markDirty(){if(closed)return;if(!dirty){dirty=true;document.title='* '+document.title.replace(/^\* /,'');}}
function clearDirty(){dirty=false;document.title=document.title.replace(/^\* /,'');}
function setBusy(b){saveBtn.disabled=b||closed;saveCloseBtn.disabled=b||closed;closeBtn.disabled=b||closed;}
function lockEditor(){
  ed.readOnly=true;
  if(cm){cm.setOption('readOnly','nocursor');cm.getWrapperElement().style.opacity='.45';}
  else{ed.style.opacity='.45';}
}
function markClosed(title,detail){
  if(closed)return;
  closed=true;clearTimeout(draftTimer);setBusy(true);lockEditor();
  msg.className='';msg.textContent=title||'Session closed';
  document.getElementById('veil-title').textContent=title||'Session closed';
  document.getElementById('veil-msg').textContent=detail||'This page is no longer connected to the Switch. Typing here will not be saved.';
  veil.classList.add('show');
  document.title=(title||'Session closed')+' \u2022 Kefir Hub';
}
async function postDraft(){
  if(closed)return;
  try{await fetch('/input/draft',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:val()});}catch(e){}
}
function scheduleDraft(){
  if(closed)return;
  markDirty();
  clearTimeout(draftTimer);
  draftTimer=setTimeout(postDraft,400);
}
async function pollStatus(){
  if(closed)return;
  try{
    const res=await fetch('/input/status');
    if(!res.ok)throw 'gone';
    statusFails=0;
    const s=await res.json();
    if(s.closing){
      try{await fetch('/input/draft',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:val()});}catch(e){}
      markClosed('Session closed on the Switch','The console ended this edit session. Further typing will not be saved.');
      return;
    }
  }catch(e){
    statusFails++;
    if(statusFails>=2){
      markClosed('Lost connection to the Switch','This edit session has ended. Further typing will not be saved.');
      return;
    }
  }
  setTimeout(pollStatus,250);
}
function modeOf(name){
  const n=(name||'').toLowerCase();
  if(/\.(ini|cfg|conf|config)$/.test(n))return ['properties','mode/properties/properties.min.js'];
  if(/\.(json|js|mjs|cjs)$/.test(n))return ['javascript','mode/javascript/javascript.min.js'];
  if(/\.(xml|html|htm)$/.test(n))return ['xml','mode/xml/xml.min.js'];
  if(/\.css$/.test(n))return ['css','mode/css/css.min.js'];
  if(/\.(md|markdown)$/.test(n))return ['markdown','mode/markdown/markdown.min.js'];
  if(/\.py$/.test(n))return ['python','mode/python/python.min.js'];
  if(/\.(c|h|cpp|hpp|cc|hh)$/.test(n))return ['clike','mode/clike/clike.min.js'];
  if(/\.(yml|yaml)$/.test(n))return ['yaml','mode/yaml/yaml.min.js'];
  if(/\.(sh|bash)$/.test(n))return ['shell','mode/shell/shell.min.js'];
  if(/\.toml$/.test(n))return ['toml','mode/toml/toml.min.js'];
  return null;
}
function loadScript(src){return new Promise((res,rej)=>{const s=document.createElement('script');s.src=src;s.onload=res;s.onerror=rej;document.head.appendChild(s);});}
function loadCss(href){return new Promise((res,rej)=>{const l=document.createElement('link');l.rel='stylesheet';l.href=href;l.onload=res;l.onerror=rej;document.head.appendChild(l);});}
async function postSave(close){
  if(closed)return;
  setBusy(true);msg.className='';msg.textContent='Saving...';
  try{
    const res=await fetch(close?'/input':'/input/save',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:val()});
    if(res.ok){
      clearDirty();
      if(close){markClosed('Saved. Session closed','The file was written to the Switch. This page is no longer connected.');}
      else{msg.className='ok';msg.textContent='Saved.';setBusy(false);}
    }else{msg.className='err';msg.textContent='Console rejected the file.';setBusy(false);}
  }catch(e){msg.className='err';msg.textContent='Could not reach the console.';setBusy(false);}
}
function save(){postSave(false);}
function saveAndClose(){postSave(true);}
async function closeSession(){
  if(closed)return;
  if(dirty && !confirm('Close without saving? Unsaved changes will not be written to the Switch.'))return;
  setBusy(true);
  try{await fetch('/input/close?discard='+(dirty?'1':'0'),{method:'POST'});}catch(e){}
  markClosed('Session closed','This page is no longer connected to the Switch. Typing here will not be saved.');
}
saveBtn.addEventListener('click',save);
saveCloseBtn.addEventListener('click',saveAndClose);
closeBtn.addEventListener('click',closeSession);
document.addEventListener('keydown',e=>{
  if(!(e.ctrlKey||e.metaKey)||e.key.toLowerCase()!=='s')return;
  e.preventDefault();
  if(e.shiftKey)saveAndClose();
  else save();
});
window.addEventListener('beforeunload',e=>{
  if(!dirty||closed)return;
  e.preventDefault();e.returnValue='';
  try{navigator.sendBeacon('/input/draft',new Blob([val()],{type:'text/plain;charset=utf-8'}));}catch(x){}
});
ed.addEventListener('input',scheduleDraft);
(async()=>{
  let title='file';
  try{
    const cfg=await(await fetch('/input/config')).json();
    title=cfg.title||'file';
    document.getElementById('name').textContent=title;
    document.title=title+' \u2022 Kefir Hub';
  }catch(e){}
  try{ed.value=await(await fetch('/input/body')).text();}catch(e){ed.value='';}
  const spec=modeOf(title);
  try{
    await Promise.race([
      Promise.all([loadCss(CM+'lib/codemirror.css'),loadScript(CM+'lib/codemirror.min.js')]),
      new Promise((_,rej)=>setTimeout(()=>rej('timeout'),5000))
    ]);
    if(spec&&window.CodeMirror){try{await loadScript(CM+spec[1]);}catch(e){}}
    if(!window.CodeMirror)throw 'no cm';
    const json=/\.json$/i.test(title);
    cm=CodeMirror.fromTextArea(ed,{
      lineNumbers:true,indentUnit:2,tabSize:2,
      lineWrapping:matchMedia('(pointer:coarse)').matches,
      mode:json?{name:'javascript',json:true}:(spec?spec[0]:null),
      extraKeys:{'Ctrl-S':save,'Cmd-S':save,'Shift-Ctrl-S':saveAndClose,'Shift-Cmd-S':saveAndClose}
    });
    cm.setSize('100%','100%');
    cm.on('change',scheduleDraft);
    window.addEventListener('resize',()=>cm.refresh());
    cm.focus();
  }catch(e){ed.focus();}
  pollStatus();
})();
</script></body></html>
)HTML";
} // namespace sphaira::webpages
