# Zero-config Lucy Worker

The Cloudflare Worker is the production backend: `https://sparkling-fog-fdc8.vikasshu7.workers.dev`.

Endpoints:
- Browser POST / with allowed Origin and {messages} -> {reply}.
- ESP32 GET /api/tags with x-lucy-token -> {models:[{name,model}]}.
- ESP32 POST /api/chat with x-lucy-token and Ollama-shaped {messages} ->
  {model,created_at,message:{role:"assistant",content},done:true}.

The OpenAI API key only exists as the Worker secret `OPENAI_API_KEY`.
The token `LUCY_DEVICE_TOKEN` must be generated independently. Do NOT
put either secret into git. Rate limiter protects requests per Cloudflare
region, but **it is not a hard usage/budget cap**. Configure API project
usage budgets, monitor spend, and add Cloudflare WAF/turnstile or durable
user/device provisioning before distributing to untrusted users.

On this GitHub branch the default Worker uses the original OpenRouter code,
so this change intentionally replaces that code with OpenAI Chat Completions.
This repository may be behind your Windows clone: reconcile before merging.

Deploy:
```powershell
cd C:\Users\Sarjerao\Lucy-AI-ESP32
git fetch origin
git checkout clipbot/zero-config-cloudflare
npm exec --yes --package wrangler -- wrangler secret put OPENAI_API_KEY
npm exec --yes --package wrangler -- wrangler secret put LUCY_DEVICE_TOKEN
npm exec --yes --package wrangler -- wrangler deploy
```
Your previous OpenAI key was disclosed: rotate it before redeployment.
The current OpenAI API credit balance is reported exhausted; HTTP 502
is expected until credits are available.

TLS: do not rely on a single hard-coded certificate observed today.
Cloudflare may use different publicly trusted CA chains for different
clients/edge regions. Test the live certificate with OpenSSL and choose
public root CA trust anchors; preserve hostname verification.
Never use `setInsecure()` in production.

Security caveat: a token embedded in firmware distributed to all users is
extractable and shared. This is only sensible for a controlled personal
prototype. Production zero-config requires per-device provisioning,
token rotation, enrollment/revocation and hard spending limits.
