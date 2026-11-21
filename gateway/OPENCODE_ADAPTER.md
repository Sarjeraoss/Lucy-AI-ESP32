# Lucy opencode adapter

Small localhost adapter that keeps Lucy firmware protocol (`GET /api/tags`, `POST /api/chat`) while using opencode as the only AI backend.

## Setup

1. Copy config:
   - `Copy-Item gateway/opencode-adapter.example.json gateway/opencode-adapter.json`
2. Set `token` to a long random device token.
3. Set `opencodePassword` to your `OPENCODE_SERVER_PASSWORD` (or env var).
4. Copy `gateway/lucy-agent.example.md` to `.opencode/agent/lucy.md` (create the directory first).
5. Start opencode server:
   - `$env:OPENCODE_SERVER_PASSWORD="..."; opencode serve --hostname 127.0.0.1 --port 4096`
6. Start adapter:
   - `node gateway/opencode-adapter.js`

## Cloudflare Worker front

`worker/worker.js` can proxy `/api/chat` and `/api/tags` to this adapter through `ORIGIN_URL` secret. Set Worker secrets:
- `LUCY_DEVICE_TOKEN`
- `ORIGIN_URL` (Cloudflare Tunnel URL that reaches adapter)

Do not store OpenAI/Ollama keys in firmware.

## Failure handling
Session creation uses title only; model selection is sent with the message. One chat runs at a time because the device uses a shared session. Additional chats receive HTTP 429. The configured timeout covers session lookup, creation, and generation; expired requests return HTTP 504 and attempt to abort upstream generation. OpenCode free-tier retry states are aborted promptly and return HTTP 503 with a usage-limit error. Restore provider quota or configure an available model before retrying. Keep the local OpenCode password consistent with the server environment. Run regression checks with: node --test gateway/opencode-adapter.test.cjs.


## Local Iceman device configuration
Lucy uses http://192.168.1.169:8788 on the PC LAN and opencode/iceman-free in opencode-adapter.json. The adapter has a 12-second request deadline, shorter than the firmware LAN timeout. Device endpoint/model/token are saved in NVS and survive reboot; display/network settings no longer reset the device to the old cloud provider. The ignored lucy_device_config.h supplies local defaults. Keep the PC and OpenCode server running; update the endpoint if the PC address changes. Windows firewall permits only the current device address 192.168.1.140 on TCP 8788. Model-specific quota errors remain upstream provider errors and fall back to Lucy's offline brain.
