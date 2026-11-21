'use strict';
const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const CFG = path.join(__dirname, 'opencode-adapter.json');
const EX = path.join(__dirname, 'opencode-adapter.example.json');
function load() {
  const f = JSON.parse(fs.readFileSync(fs.existsSync(CFG)?CFG:EX,'utf8'));
  return {
    port: Number(process.env.LUCY_ADAPTER_PORT || f.port || 8788),
    bind: process.env.LUCY_ADAPTER_BIND || f.bind || '127.0.0.1',
    token: process.env.LUCY_DEVICE_TOKEN || f.token || '',
    opencodeUrl: (process.env.OPENCODE_URL || f.opencodeUrl || 'http://127.0.0.1:4096').replace(/\/+$/,''),
    opencodeUser: process.env.OPENCODE_SERVER_USERNAME || f.opencodeUser || 'opencode',
    opencodePass: process.env.OPENCODE_SERVER_PASSWORD || f.opencodePassword || '',
    agent: process.env.LUCY_OPENCODE_AGENT || f.agent || 'lucy',
    model: f.model || null, // {providerID, modelID} or "provider/model"
    timeoutMs: Number(f.timeoutMs || 90000),
    maxBodyBytes: Number(f.maxBodyBytes || 16000),
    maxMessages: Number(f.maxMessages || 16),
    maxChars: Number(f.maxCharsPerMessage || 2000),
    maxTotal: Number(f.maxTotalChars || 12000),
    sessionFile: path.join(__dirname, f.sessionFile || '.lucy-opencode-session'),
  };
}
const cfg = load();
if (!cfg.token || cfg.token.length < 24) { console.error('[config] token missing/short'); process.exit(1); }
if (!cfg.opencodePass) { console.error('[config] OPENCODE_SERVER_PASSWORD missing'); process.exit(1); }

function safeEq(a,b){ if(!a||!b) return false; const A=Buffer.from(String(a)),B=Buffer.from(String(b)); if(A.length!==B.length) return false; return crypto.timingSafeEqual(A,B); }
function getToken(req){ const h=req.headers['x-lucy-token']; if(typeof h==='string'&&h) return h.trim(); const a=req.headers.authorization||''; return a.toLowerCase().startsWith('bearer ')?a.slice(7).trim():''; }
function readBody(req){ return new Promise((res,rej)=>{ let n=0,buf=[]; req.on('data',c=>{ n+=c.length; if(n>cfg.maxBodyBytes){req.destroy();rej(Object.assign(new Error('too large'),{status:413})); return;} buf.push(c); }); req.on('end',()=>res(Buffer.concat(buf).toString('utf8'))); req.on('error',rej); }); }
function json(res,status,obj){ const b=JSON.stringify(obj); res.writeHead(status,{'Content-Type':'application/json; charset=utf-8','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'}); res.end(b); }
function log(...a){ console.log(`[${new Date().toISOString()}]`,...a); }

function basic(){ return 'Basic '+Buffer.from(cfg.opencodeUser+':'+cfg.opencodePass).toString('base64'); }
async function oc(pathname, init={}){
  const r = await fetch(cfg.opencodeUrl+pathname, { ...init, signal: init.signal || AbortSignal.timeout(cfg.timeoutMs), headers: { 'Authorization': basic(), ...(init.headers||{}) } });
  const t = await r.text();
  let d; try { d = t?JSON.parse(t):null; } catch { d = t; }
  if (!r.ok) throw Object.assign(new Error(`opencode ${r.status}`), {status:r.status, detail:d});
  return d;
}
function parseModel(m){ if(!m) return undefined; if(typeof m==='object') return m; const i=m.indexOf('/'); return i>0?{providerID:m.slice(0,i),modelID:m.slice(i+1)}:undefined; }
function loadSession(){ try { return fs.readFileSync(cfg.sessionFile,'utf8').trim(); } catch { return ''; } }
function saveSession(id){ try { fs.writeFileSync(cfg.sessionFile,id,'utf8'); } catch {} }
async function ensureSession(signal){
  let id = loadSession();
  if (id) { try { await oc(`/session/${encodeURIComponent(id)}`, {signal}); return id; } catch(e) { if(e.status!==404) throw e; } }
  const body = { title: 'Lucy device session' };
  const created = await oc('/session', { method:'POST', signal, headers:{'Content-Type':'application/json'}, body: JSON.stringify(body) });
  id = created && created.id; if(!id) throw new Error('no session id');
  saveSession(id); log('created session', id); return id;
}

function sanitizeMessages(raw){
  if(!Array.isArray(raw)||raw.length<1||raw.length>cfg.maxMessages) return null;
  const out=[]; let total=0;
  for(const m of raw){
    if(!m||typeof m.content!=='string') return null;
    if(!['user','assistant','system'].includes(m.role)) return null;
    if(m.role==='system') continue;
    const content=m.content.trim().slice(0,cfg.maxChars);
    if(!content) continue;
    total+=content.length; if(total>cfg.maxTotal) return null;
    out.push({role:m.role, content});
  }
  if(!out.length || out[out.length-1].role!=='user') return null;
  return out;
}
function buildPrompt(msgs){
  const last = msgs[msgs.length-1].content;
  const prior = msgs.slice(0,-1).slice(-6);
  if(!prior.length) return last;
  const ctx = prior.map(m=>`${m.role==='assistant'?'Lucy':'User'}: ${m.content}`).join('\n');
  return `Recent conversation context:\n${ctx}\n\nCurrent user message:\n${last}\n\nReply to the current user message.`;
}

async function promptSession(sessionID, text, signal){
  const body = { agent: cfg.agent, parts: [{type:'text', text}] };
  const model = parseModel(cfg.model); if (model) body.model = model;
  {
    const r = await fetch(cfg.opencodeUrl+`/session/${encodeURIComponent(sessionID)}/message`, {
      method:'POST', headers:{'Authorization':basic(),'Content-Type':'application/json'},
      body: JSON.stringify(body), signal
    });
    const raw = await r.text(); let data; try{data=JSON.parse(raw);}catch{data=raw;}
    if(!r.ok) throw Object.assign(new Error(`opencode ${r.status}`),{status:r.status,detail:data});
    const parts = (data && data.parts) || [];
    const textParts = parts.filter(p=>p && p.type==='text' && typeof p.text==='string').map(p=>p.text.trim()).filter(Boolean);
    const reply = textParts.join('\n').trim();
    if(data && data.info && data.info.error) throw Object.assign(new Error('opencode message error'),{status:502});
    if(!reply) throw Object.assign(new Error('empty reply'),{status:502});
    return reply.slice(0,700);
  }
}

let busy = 0;
const server = http.createServer(async (req,res)=>{
  const url = new URL(req.url,'http://localhost');
  if (req.method==='GET' && (url.pathname==='/'||url.pathname==='/health')) return json(res,200,{ok:true,service:'lucy-opencode-adapter',busy,agent:cfg.agent});
  if (req.method==='GET' && url.pathname==='/api/tags') {
    if(!safeEq(getToken(req),cfg.token)) return json(res,401,{error:'Unauthorized'});
    const name = typeof cfg.model==='string'?cfg.model:(cfg.model?`${cfg.model.providerID}/${cfg.model.modelID}`:'opencode/default');
    return json(res,200,{models:[{name,model:name}]});
  }
  if (req.method==='POST' && (url.pathname==='/api/chat' || url.pathname==='/chat')) {
    if(!safeEq(getToken(req),cfg.token)) return json(res,401,{error:'Unauthorized'});
    if(busy>=1) return json(res,429,{error:'Busy'});
    let body; try { body = JSON.parse(await readBody(req)||'{}'); } catch(e){ return json(res,e.status||400,{error:'Invalid JSON'}); }
    const msgs = sanitizeMessages(body.messages);
    if(!msgs) return json(res,400,{error:'Invalid messages'});
    if(busy>=1) return json(res,429,{error:'Busy'});
    busy++;
    const started = Date.now();
    const ctl = new AbortController();
    const timer = setTimeout(()=>ctl.abort(), cfg.timeoutMs);
    const disconnected = ()=>{ if(!res.writableEnded) ctl.abort(); };
    res.on('close', disconnected);
    let sid, backendFailure, checking = false;
    const monitor = setInterval(async ()=>{
      if(!sid || checking || ctl.signal.aborted) return;
      checking = true;
      try {
        const statuses = await oc('/session/status', {signal:AbortSignal.timeout(2000)});
        const state = statuses && statuses[sid];
        if(state && state.type==='retry' && state.action && state.action.reason==='free_tier_limit') {
          backendFailure = 'OpenCode free usage limit reached';
          ctl.abort();
        }
      } catch {} finally { checking = false; }
    }, 1000);
    try {
      sid = await ensureSession(ctl.signal);
      const text = buildPrompt(msgs);
      const reply = await promptSession(sid, text, ctl.signal);
      log('chat ok', reply.length, 'chars');
      if (url.pathname==='/chat') return json(res,200,{reply,model:cfg.agent});
      return json(res,200,{model:cfg.agent,created_at:new Date().toISOString(),message:{role:'assistant',content:reply},done:true,done_reason:'stop'});
    } catch(e) {
      const timedOut = ctl.signal.aborted;
      log('chat err', backendFailure || (timedOut?'deadline or disconnect':e.message), 'elapsedMs='+ (Date.now()-started));
      if(timedOut && sid) {
        try { await oc(`/session/${encodeURIComponent(sid)}/abort`, {method:'POST', signal:AbortSignal.timeout(2000)}); } catch {}
      }
      const status = backendFailure?503:(timedOut?504:502);
      return json(res,status,{error:backendFailure || 'Lucy backend unavailable'});
    } finally { clearInterval(monitor); clearTimeout(timer); res.off('close',disconnected); busy--; }
  }
  return json(res,404,{error:'Not found'});
});
server.listen(cfg.port,cfg.bind,()=>log(`lucy-opencode-adapter on http://${cfg.bind}:${cfg.port} -> ${cfg.opencodeUrl} agent=${cfg.agent}`));
