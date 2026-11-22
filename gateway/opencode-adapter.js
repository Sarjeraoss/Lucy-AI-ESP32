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
function json(res,status,obj){
  const b=Buffer.from(JSON.stringify(obj), 'utf8');
  res.writeHead(status,{
    'Content-Type':'application/json; charset=utf-8',
    'Cache-Control':'no-store',
    'X-Content-Type-Options':'nosniff',
    'Content-Length': String(b.length),
    'Connection':'close'
  });
  res.end(b);
}
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
function cleanReplyText(s){ return typeof s==='string'?s.replace(/\r\n/g,'\n').trim():''; }
function extractReply(data){
  if (typeof data === 'string') return cleanReplyText(data);
  if (!data || typeof data !== 'object') return '';
  const parts = Array.isArray(data.parts) ? data.parts : [];
  const textParts = [];
  for (const p of parts) {
    if (!p || typeof p !== 'object') continue;
    if (p.type === 'text' || p.type === 'output_text') {
      const t = cleanReplyText(p.text || p.content);
      if (t) textParts.push(t);
    }
  }
  if (textParts.length) return textParts.join('\n').trim();
  const candidates = [
    data.message && data.message.content,
    data.output_text,
    data.text,
    data.response,
    data.content
  ];
  for (const c of candidates) {
    const t = cleanReplyText(c);
    if (t) return t;
  }
  for (const p of parts) {
    if (!p || typeof p !== 'object') continue;
    if (['reasoning','tool','tool-call','tool-result','step-start','step-finish','patch','file'].includes(p.type)) continue;
    const t = cleanReplyText(p.text || p.content);
    if (t) return t;
  }
  return '';
}
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
    if(data && data.info && data.info.error) throw Object.assign(new Error('opencode message error'),{status:502,detail:data.info.error});
    const reply = extractReply(data);
    if(!reply) {
      const types = Array.isArray(data && data.parts) ? data.parts.map(p=>p && p.type).filter(Boolean) : [];
      log('empty reply parts', types.join(',') || 'none');
      throw Object.assign(new Error('empty reply'), {status:502, detail:{reason:'empty assistant text', partTypes:types}});
    }
    return reply.slice(0,700);
  }
}


const os = require('node:os');
const {spawn} = require('node:child_process');
function speechWavToPcm(wav) {
  if(wav.length<44||wav.toString('ascii',0,4)!=='RIFF'||wav.toString('ascii',8,12)!=='WAVE') throw Error('Invalid WAV output');
  let pos=12, fmt, audio;
  while(pos+8<=wav.length){
    const type=wav.toString('ascii',pos,pos+4), size=wav.readUInt32LE(pos+4), start=pos+8;
    if(start+size>wav.length) throw Error('Truncated WAV output');
    if(type==='fmt '&&size>=16) fmt=[wav.readUInt16LE(start),wav.readUInt16LE(start+2),wav.readUInt32LE(start+4),wav.readUInt16LE(start+14)];
    if(type==='data') audio=wav.subarray(start,start+size);
    pos=start+size+(size%2);
  }
  if(!fmt||fmt.join(',')!=='1,1,16000,16'||!audio?.length||audio.length%2) throw Error('Expected 16 kHz mono signed 16-bit PCM');
  return audio;
}
async function generateLucySpeech(text){
  if(process.platform!=='win32') throw Error('Built-in SAPI requires a Windows PC');
  const output=path.join(os.tmpdir(),'lucy-'+crypto.randomUUID()+'.wav');
  try {
    await new Promise((resolve,reject)=>{
      const script=path.join(__dirname,'sapi-tts.ps1'), encoded=Buffer.from(text,'utf8').toString('base64');
      const child=spawn('powershell.exe',['-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',script,'-EncodedText',encoded,'-OutputFile',output],{windowsHide:true,stdio:['ignore','ignore','pipe']});
      let err='',settled=false;
      const timer=setTimeout(()=>child.kill(),25000);
      child.stderr.on('data',c=>{if(err.length<800)err+=c.toString()});
      child.once('error',e=>{if(settled)return;settled=true;clearTimeout(timer);reject(e)});
      child.once('close',code=>{if(settled)return;settled=true;clearTimeout(timer);code===0?resolve():reject(Error('SAPI error: '+err.slice(0,150)))});
    });
    return speechWavToPcm(await fs.promises.readFile(output));
  } finally { await fs.promises.unlink(output).catch(()=>{}); }
}
let ttsBusy=false;

let busy = 0;
const server = http.createServer(async (req,res)=>{
  const url = new URL(req.url,'http://localhost');
  if (req.method==='GET' && (url.pathname==='/'||url.pathname==='/health')) return json(res,200,{ok:true,service:'lucy-opencode-adapter',busy,agent:cfg.agent});
  if(req.method==='GET' && url.pathname==='/tts'){
    if(typeof req.headers['x-clipbot-token']!=='string'||!safeEq(req.headers['x-clipbot-token'],cfg.token)) return json(res,401,{error:'Unauthorized TTS'});
    const speech=url.searchParams.get('text')||'';
    if(!speech.trim()||speech.length>180||/[\x00-\x1f]/.test(speech)) return json(res,400,{error:'Invalid speech text'});
    if(ttsBusy) return json(res,429,{error:'TTS busy'});
    ttsBusy=true;
    try {
      const pcm=await generateLucySpeech(speech);
      if(pcm.length>1024000) return json(res,413,{error:'Speech too long'});
      res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Length':String(pcm.length),'Connection':'close','Cache-Control':'no-store'});
      return res.end(pcm);
    } catch(e){log('TTS error',e.message);return json(res,502,{error:'TTS error',detail:e.message});}
    finally{ttsBusy=false;}
  }
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
      const out = url.pathname==='/chat'
        ? {reply,model:cfg.agent}
        : {model:cfg.agent,created_at:new Date().toISOString(),message:{role:'assistant',content:reply},done:true,done_reason:'stop'};
      log('chat ok', reply.length, 'chars', 'ct=application/json');
      return json(res,200,out);
    } catch(e) {
      const timedOut = ctl.signal.aborted;
      const msg = backendFailure || (timedOut?'deadline or disconnect':e.message);
      log('chat err', msg, 'elapsedMs='+ (Date.now()-started));
      if(timedOut && sid) {
        try { await oc(`/session/${encodeURIComponent(sid)}/abort`, {method:'POST', signal:AbortSignal.timeout(2000)}); } catch {}
      }
      const status = backendFailure?503:(timedOut?504:(e.status && e.status>=400 && e.status<600 ? e.status : 502));
      return json(res,status,{error:backendFailure || 'Lucy backend unavailable', detail:msg});
    } finally { clearInterval(monitor); clearTimeout(timer); res.off('close',disconnected); busy--; }
  }
  return json(res,404,{error:'Not found'});
});
server.listen(cfg.port,cfg.bind,()=>log(`lucy-opencode-adapter on http://${cfg.bind}:${cfg.port} -> ${cfg.opencodeUrl} agent=${cfg.agent}`));
