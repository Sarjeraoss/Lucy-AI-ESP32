import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const file=readFileSync('worker/worker.js','utf8').replace('export default','globalThis.handler =');
const sandbox={Response,Request,URL,crypto:globalThis.crypto,TextEncoder,AbortController,console,setTimeout,clearTimeout,fetch:async(url,opts)=>{
 assert.equal(url,'https://api.openai.com/v1/chat/completions');
 const body=JSON.parse(opts.body);
 assert.equal(body.model,'gpt-4o-mini');
 return Response.json({choices:[{message:{content:'New Delhi.'}}]});
}};
vm.createContext(sandbox);vm.runInContext(file,sandbox);
const env={OPENAI_API_KEY:'test-only-not-a-real-secret',LUCY_DEVICE_TOKEN:'0123456789abcdefghijklmnop',OPENAI_MODEL:'gpt-4o-mini',
 LUCY_RATE_LIMITER:{limit:async()=>({success:true})}};
function req(path,method,body,headers={}) {return new Request('https://worker.test'+path,{method,headers,body:body?JSON.stringify(body):undefined})}
const browser=await sandbox.handler.fetch(req('/','POST',{messages:[{role:'user',content:'Capital?'}]},{Origin:'http://192.168.4.1','Content-Type':'application/json'}),env);
assert.equal((await browser.json()).reply,'New Delhi.');
const c3=await sandbox.handler.fetch(req('/api/chat','POST',{messages:[{role:'user',content:'Capital?'}]},{'x-lucy-token':env.LUCY_DEVICE_TOKEN,'Content-Type':'application/json'}),env);
assert.equal((await c3.json()).message.content,'New Delhi.');
const tags=await sandbox.handler.fetch(req('/api/tags','GET',null,{'x-lucy-token':env.LUCY_DEVICE_TOKEN}),env);
assert.equal((await tags.json()).models[0].name,'gpt-4o-mini');
const denied=await sandbox.handler.fetch(req('/api/chat','POST',{messages:[{role:'user',content:'x'}]},{'x-lucy-token':'wrong','Content-Type':'application/json'}),env);
assert.equal(denied.status,401);
const originDenied=await sandbox.handler.fetch(req('/','POST',{messages:[{role:'user',content:'x'}]},{Origin:'https://evil.test','Content-Type':'application/json'}),env);
assert.equal(originDenied.status,403);
console.log('Worker compatibility tests passed.');
