'use strict';
// Independent speech server: does NOT change the working OpenCode adapter.
// Windows only: native System.Speech synthesizer -> PCM for ESP32 MAX98357A.
const http=require('node:http');
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const local=path.join(__dirname,'opencode-adapter.json');
if(!fs.existsSync(local)) { console.error('Missing gateway/opencode-adapter.json (same token as AI adapter)');process.exit(1); }
const cfg=JSON.parse(fs.readFileSync(local,'utf8'));
const token=process.env.LUCY_DEVICE_TOKEN||cfg.token;
if(typeof token!=='string'||token.length<24){console.error('No configured device token');process.exit(1);}
const port=Number(process.env.LUCY_TTS_PORT||8790);
const bind=process.env.LUCY_TTS_BIND||'0.0.0.0';
function auth(v){
 if(typeof v!=='string')return false;
 const a=Buffer.from(v),b=Buffer.from(token);
 return a.length===b.length&&crypto.timingSafeEqual(a,b);
}
function reply(res,status,text){
 const b=Buffer.from(JSON.stringify({error:text}));
 res.writeHead(status,{'Content-Type':'application/json','Content-Length':String(b.length),'Connection':'close'});
 res.end(b);
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
http.createServer(async(req,res)=>{
 const u=new URL(req.url,'http://localhost');
 if(req.method==='GET'&&u.pathname==='/health')return reply(res,200,'Lucy TTS ready');
 if(req.method!=='GET'||u.pathname!=='/tts')return reply(res,404,'Not found');
 if(!auth(req.headers['x-clipbot-token']))return reply(res,401,'Unauthorized');
 const phrase=u.searchParams.get('text')||'';
 if(!phrase.trim()||phrase.length>180||/[\x00-\x1f]/.test(phrase))return reply(res,400,'Invalid text');
 if(ttsBusy)return reply(res,429,'Speech busy');
 ttsBusy=true;
 try{
   const audio=await generateLucySpeech(phrase);
   if(audio.length>1024000)return reply(res,413,'Speech too long');
   res.writeHead(200,{'Content-Type':'application/octet-stream','Content-Length':String(audio.length),'Connection':'close','Cache-Control':'no-store'});
   res.end(audio);
 }catch(e){console.error('TTS failure:',e.message);reply(res,502,e.message)}
 finally{ttsBusy=false}
}).listen(port,bind,()=>console.log('Lucy TTS listening at http://'+bind+':'+port+' (device token required)'));
