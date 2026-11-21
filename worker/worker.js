// Lucy Cloudflare Worker: stable HTTPS front for ESP32 devices.
// Auth: x-lucy-token (secret LUCY_DEVICE_TOKEN). Backend: ORIGIN_URL secret points to
// an opencode-adapter exposed via Cloudflare Tunnel. No OpenAI/Ollama keys in firmware.
const ALLOWED_ORIGINS = new Set(["http://192.168.4.1", "http://edsoft.local"]);
const JSON_HEADERS = {"Content-Type":"application/json; charset=utf-8","Cache-Control":"no-store","X-Content-Type-Options":"nosniff"};
const json = (value,status=200,extra={})=>Response.json(value,{status,headers:{...JSON_HEADERS,...extra}});
async function equalSecret(a,b){
  if(!a||!b) return false;
  const enc=new TextEncoder();
  const [x,y]=await Promise.all([crypto.subtle.digest("SHA-256",enc.encode(a)),crypto.subtle.digest("SHA-256",enc.encode(b))]);
  let diff=0; const xa=new Uint8Array(x), ya=new Uint8Array(y);
  for(let i=0;i<xa.length;i++) diff|=xa[i]^ya[i];
  return diff===0;
}
function sanitizeMessages(v){
  if(!Array.isArray(v)||v.length<1||v.length>16) return null;
  const out=[]; let total=0;
  for(const m of v){
    if(!m||!["system","user","assistant"].includes(m.role)||typeof m.content!=="string") return null;
    const content=m.content.trim().slice(0,2000);
    if(!content) return null;
    total+=content.length; if(total>12000) return null;
    if(m.role!=="system") out.push({role:m.role,content});
  }
  return out.length && out[out.length-1].role==="user" ? out : null;
}
async function readJson(request, limit=16000){
  if(request.headers.get("content-type")?.toLowerCase().split(";")[0].trim()!=="application/json") return {error:json({error:"JSON required"},415)};
  const len=Number(request.headers.get("content-length")||0); if(len>limit) return {error:json({error:"Request too large"},413)};
  const reader=request.body?.getReader(); if(!reader) return {error:json({error:"Invalid JSON"},400)};
  const chunks=[]; let size=0;
  while(true){ const {done,value}=await reader.read(); if(done) break; size+=value.byteLength; if(size>limit){await reader.cancel(); return {error:json({error:"Request too large"},413)};} chunks.push(value); }
  const bytes=new Uint8Array(size); let off=0; for(const c of chunks){bytes.set(c,off); off+=c.length;}
  try { return {data:JSON.parse(new TextDecoder().decode(bytes))}; } catch { return {error:json({error:"Invalid JSON"},400)}; }
}
async function proxyToOrigin(request, env, pathname, bodyObj){
  const origin=(env.ORIGIN_URL||"").replace(/\/+$/,"");
  if(!origin) return json({error:"Origin is not configured"},503);
  const headers={"Content-Type":"application/json","Accept":"application/json","x-lucy-token":request.headers.get("x-lucy-token")||""};
  const ctl=new AbortController(); const timer=setTimeout(()=>ctl.abort(),90000);
  try {
    const upstream=await fetch(origin+pathname,{method:request.method,headers,body:bodyObj===undefined?undefined:JSON.stringify(bodyObj),signal:ctl.signal});
    const text=await upstream.text();
    let data; try{data=text?JSON.parse(text):null;}catch{data=null;}
    if(!upstream.ok){
      const msg = data && typeof data.error==="string" ? data.error : "Backend unavailable";
      const status = upstream.status>=400 && upstream.status<600 ? upstream.status : 502;
      return json({error:msg},status);
    }
    return new Response(text||"{}",{status:200,headers:JSON_HEADERS});
  } catch(e){
    return json({error:e?.name==="AbortError"?"Backend timeout":"Backend unavailable"},504);
  } finally { clearTimeout(timer); }
}

export default {
  async fetch(request, env){
    const url=new URL(request.url); const route=url.pathname; const origin=request.headers.get("Origin")||"";
    const browser = route==="/";
    const cors = ALLOWED_ORIGINS.has(origin) ? {
      "Access-Control-Allow-Origin":origin,"Access-Control-Allow-Methods":"POST,OPTIONS","Access-Control-Allow-Headers":"Content-Type,x-lucy-token","Vary":"Origin"
    } : {};
    if(browser && !ALLOWED_ORIGINS.has(origin)) return json({error:"Forbidden origin"},403,cors);
    if(request.method==="OPTIONS"){
      if(!browser) return json({error:"Not found"},404);
      return new Response(null,{status:204,headers:cors});
    }
    const device = route==="/api/chat" || route==="/api/tags";
    if(!browser && !device) return json({error:"Not found"},404);

    if(device || (browser && env.ALLOW_LEGACY_BROWSER !== "true")) {
      if(!env.LUCY_DEVICE_TOKEN || env.LUCY_DEVICE_TOKEN.length<24) return json({error:"Device authentication is not configured"},503,cors);
      if(!await equalSecret(request.headers.get("x-lucy-token"), env.LUCY_DEVICE_TOKEN)) return json({error:"Unauthorized"},401,cors);
    }
    if(!env.LUCY_RATE_LIMITER) return json({error:"Rate limiting not configured"},503,cors);
    const identity = device ? "device" : (request.headers.get("CF-Connecting-IP")||"unknown");
    let allowed; try { allowed = await env.LUCY_RATE_LIMITER.limit({key:(device?"device:":"browser:")+identity}); } catch { return json({error:"Rate limiting unavailable"},503,cors); }
    if(!allowed.success) return json({error:"Too many requests"},429,cors);

    if(route==="/api/tags"){
      if(request.method!=="GET") return json({error:"GET required"},405,cors);
      return proxyToOrigin(request, env, "/api/tags");
    }
    if(request.method!=="POST") return json({error:"POST required"},405,cors);
    const parsed = await readJson(request, 16000);
    if(parsed.error) return new Response(parsed.error.body, {status:parsed.error.status, headers:{...JSON_HEADERS,...cors}});
    if(parsed.data?.stream===true) return json({error:"Streaming is not supported; use stream:false"},400,cors);
    const messages = sanitizeMessages(parsed.data?.messages);
    if(!messages) return json({error:"Invalid messages"},400,cors);
    const resp = await proxyToOrigin(request, env, "/api/chat", {messages, stream:false});
    // add CORS for legacy browser route
    if(browser && ALLOWED_ORIGINS.has(origin)) {
      const text = await resp.text();
      return new Response(text, {status:resp.status, headers:{...JSON_HEADERS,...cors}});
    }
    return resp;
  }
};
