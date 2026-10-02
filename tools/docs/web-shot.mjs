// Screenshot a web page (Kefir Hub web server) with headless Chrome, 1280x800.
//   node tools/docs/web-shot.mjs <url> <out.png> [js-file-to-run-before-shot] [waitMs]
// The Hub web server in Eden answers on http://127.0.0.1:80 (Install & Share -> Web Server).
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
const [url, out, jsFile, waitMs = '2500'] = process.argv.slice(2);
const dir = tmpdir().replaceAll('\\', '/') + '/';
const chrome = spawn('C:/Program Files/Google/Chrome/Application/chrome.exe', ['--headless=new', '--remote-debugging-port=9333',
  `--user-data-dir=${dir}kefir-docs-chrome`, '--window-size=1280,800', '--hide-scrollbars', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let tabs; for (let i = 0; i < 40; i++) { try { tabs = await (await fetch('http://127.0.0.1:9333/json')).json(); break; } catch { await sleep(250); } }
const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => ws.onopen = r);
let id = 0; const pend = {};
ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && pend[m.id]) { pend[m.id](m); delete pend[m.id]; } };
const send = (method, params = {}) => new Promise(r => { const i = ++id; pend[i] = r; ws.send(JSON.stringify({ id: i, method, params })); });
await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 800, deviceScaleFactor: 1, mobile: false });
await send('Page.enable'); await send('Page.navigate', { url }); await sleep(+waitMs);
if (jsFile) { const r = await send('Runtime.evaluate', { expression: readFileSync(jsFile, 'utf8'), awaitPromise: true, returnByValue: true }); console.log(JSON.stringify(r.result?.result?.value ?? r.result)); await sleep(+waitMs); }
const s = await send('Page.captureScreenshot', { format: 'png' });
writeFileSync(out, Buffer.from(s.result.data, 'base64')); console.log('saved', out);
ws.close(); chrome.kill(); process.exit(0);
