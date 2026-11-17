# Lucy Cat OS 0.3.0 — ESP32-C3 Mini

Target: **ESP32-C3 Mini/SuperMini with 4 MB flash, no PSRAM**, an SSD1306
128x64 I2C OLED on GPIO6/7, and a MAX98357A I2S speaker amplifier.
This extends `offline-os-v1`. The classic ESP32 target is also retained.

## What is implemented

- Animated tiny cat boot window: “Hi! I'm your virtual friend.” and “Lucy is
  booting” at the bottom. A bundled English greeting plays through the MAX98357A.
- Home: large cat, network icon at top left, small rounded battery at top right.
  No clock, labels, app buttons or status text on the home screen.
- **120 procedural animation clips**: 24 behaviours, each performed in Gentle,
  Bouncy, Curious, Dreamy and Spirited variants. These are motion variations,
  not 120 unrelated drawings. Kneading, head bunting, loafing, slow blinking,
  zoomies, scratching, flehmen, purring, meowing, chirping, chattering, hissing,
  yowling, happy tail, puffed tail/back, tucked tail, twitching tail, airplane
  ears, belly rolling, sleeping, grooming, stretching, pouncing and waving.
  All 120 play before the sequence repeats, at about 12 frames per second.
  They are playful programmed behaviours, not emotion or physical sensors.
- Quiet synthesized cat sounds for vocal behaviours. Volume 0 mutes everything.
  The boot greeting is prerecorded speech; this is not general chat TTS.
- One-button settings: brightness, language, speaker volume, Wi-Fi, access
  point, AI model, MCP setup, Info, and Apps. Settings survive reboot.
- Phone setup page for names/passwords, custom model URL/name/token, and MCP.
- Optional **read-only MCP server** on `/mcp`: `lucy_status`, `lucy_animations`.
- Existing offline keyword + int8 intent + Markov brain, PC model bridge, clock,
  Snake, serial console and live framebuffer mirror remain available.

## Wiring

All GPIO numbers below are **GPIO numbers**, not physical header positions.

| Module connection | C3 Mini connection |
|---|---|
| OLED SDA | GPIO6 |
| OLED SCL | GPIO7 |
| OLED VCC / GND | 3V3 / GND (3.3 V compatible module) |
| MAX98357A DIN | GPIO3 |
| MAX98357A BCLK | GPIO4 |
| MAX98357A LRC / WS | GPIO5 |
| MAX98357A VIN / GND | Suitable 3.3–5 V amplifier supply / common GND |
| Speaker | MAX98357A SPK+ and SPK−; neither terminal goes to GND |
| Settings button | GPIO10 to GND; internal pull-up |
| Optional DS3231 SDA / SCL | GPIO6 / GPIO7, shared I2C bus |

Set the amplifier's SD/EN and GAIN as specified by your module. The firmware
sends the same mono signal on both I2S channels. Do not connect a speaker
directly to an ESP32 pin. Use a suitable regulated supply for the board and amp.
GPIO18/19 are reserved for native USB. The built-in BOOT button is not the
settings button. The C3 target disables the old SD-card wiring to avoid conflicts.
An RTC is optional; Lucy works without one.

The owner confirmed GPIO6/7 for the OLED and a MAX98357A. **The amplifier pins
above are proposed wiring**; change `src/board_config.h` if already wired differently.

## Battery sensor — configure your actual circuit first

The C3 Mini has no built-in battery gauge. The sensor model/wiring and battery
chemistry were not supplied. This build deliberately shows **?** in the battery
icon until a sensor is configured. It does not invent a charge level.

For **one 4.2 V Li-ion/LiPo cell** and a checked 100k/100k voltage divider into
GPIO0, add these flags to the C3 environment (preserve its existing flags):

```ini
  -DLUCY_BATTERY_PIN=0
  -DLUCY_BATTERY_DIVIDER_MILLI=2000
```

Divider: cell+ → 100k → GPIO0 → 100k → GND; common battery/board GND.
A 100 nF capacitor from GPIO0 to GND helps stabilize ADC readings. Never put
the battery voltage directly onto GPIO0. The display then shows a smoothed,
voltage-based percentage estimate, not charging status or a precision fuel gauge.
Use a proper protected cell, charger and regulator; this firmware is not a charger.
An I2C fuel-gauge module needs its own driver instead of these ADC flags.

## Button controls

Actions happen **on release**, after 35 ms debounce:

| Where / action | Result |
|---|---|
| Home: press button | Open settings |
| Tap under 650 ms | Next row / change value |
| Hold 650–1799 ms | Open row / save and return |
| Hold 1800 ms or longer | Back; saves a changed value |
| Leave settings idle for 15 s | Save and return home |

Brightness and volume preview immediately; network changes apply on save/back.
To recover a disabled AP: Home → settings → Access point → hold → tap to enable
→ hold to save. The web console also supplies explicit UP / SELECT / BACK buttons.
The classic board keeps its three original buttons.

## Phone setup

Join **Lucy-Setup**, then open `http://192.168.4.1/settings`. A fresh setup AP
is open so first-time setup needs no embedded password. Set your own AP password
there if wanted. Configuration edits are accepted only through Lucy's AP interface.
Leave password/token inputs blank to keep existing values; choose the open-network
checkbox explicitly to clear a Wi-Fi or AP password. SSIDs containing spaces work.
Reconnect after changing the AP. On an existing installation, saved AP settings apply.

Language selects English, romanized Hindi or romanized Marathi for local replies
and the requested server reply language. The menu and boot greeting remain English;
there is no Devanagari font or general speech recognizer/TTS.

Choose the built-in offline brain, saved model, or one of the example server model
names in the button menu. Enter a custom name and endpoint on the phone page.
Models must already be installed on the server. The existing token gateway enforces
its server-configured model; changing the device's requested model does not override
that policy. A real generative LLM runs on the PC/server, not the C3. If it is
unreachable or heap is low, Lucy uses its local brain. A separate network task keeps
the cat/audio running during a chat request. Web requests and button navigation
wait until that chat request returns. Keep Ollama port 11434 private.

## MCP setup

MCP is disabled by default. On the phone page, enter your own random 24–95 character
Bearer token, then enable MCP. Add `http://192.168.4.1/mcp` (or Lucy's station IP
with `/mcp`) and that Bearer token to a **local MCP client**. Headers:

```text
Authorization: Bearer <your locally configured token>
Content-Type: application/json
Accept: application/json, text/event-stream
MCP-Protocol-Version: 2025-06-18
```

Example JSON-RPC request after initialization:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
```

This is a small stateless Streamable HTTP MCP **server**, with JSON responses,
`initialize`, `notifications/initialized`, `ping`, `tools/list`, and `tools/call`.
It also negotiates 2025-03-26. GET returns 405 (no SSE); notifications return 202.
It checks Origins and tokens, limits request JSON to 1536 bytes/nesting 8, and
accepts at most 30 requests per minute. It exposes no credentials or write tools.
There is no OAuth discovery, legacy SSE transport, arbitrary outbound MCP client,
or internet hosting. Use a local client supporting a static Bearer header. Remote
MCP services and model-driven external tools need a host/bridge implementation.
Protocol reference: https://modelcontextprotocol.io/specification/2025-06-18/basic/transports

## Build and USB flash

```sh
python -m pip install platformio==6.2.0
cd offline
pio run -e lucy_c3_mini
```

Dependencies and platform are pinned. Default environment is `lucy_c3_mini`.
For the old board: `pio run -e lucy_offline`. **The two binaries are not interchangeable.**
The C3 build uses RISC-V, native USB CDC, a 3 MB app slot (`huge_app.csv`) and SPIFFS.
There is no OTA slot in this layout; use USB.

Back up the complete existing flash before changing a working device. Use your
actual serial port in place of `COMx`:

```sh
python -m pip install esptool==4.5.1
python -m esptool --chip esp32c3 --port COMx read_flash 0 0x400000 lucy-backup.bin
pio run -e lucy_c3_mini -t upload --upload-port COMx
pio device monitor --port COMx --baud 115200
```

PlatformIO uploads the bootloader, partition table, boot metadata and application
at their correct offsets. For the downloadable merged `lucy-cat-c3-mini-usb.bin`,
flash that merged image at **0x0** using `--chip esp32c3`. The `-app.bin` is only
the application and belongs at **0x10000**, after the partition layout is verified.
Do not OTA-flash either C3 binary onto the classic ESP32 build.
The merged image reinstalls the partition table and clears saved settings in its
covered flash range; keep your full backup. Settings then persist across reboots.

After making the backup above, a merged-image USB flash is:

```sh
python -m esptool --chip esp32c3 --port COMx write_flash -z 0x0 lucy-cat-c3-mini-usb.bin
```

## Verification

Native harness (with real U8g2 fonts and rasterizer):

```sh
python -m pip install ziglang pillow
python tools/test_companion.py
```

It checks 120 distinct moving clips, complete scheduling before repeats, millis
rollover, framebuffer memory guards, boot/settings text bounds, button thresholds,
MCP initialization/notifications/tools/errors, and produces PBM screens + a preview.

On-device checks after flashing: greeting, corner icons, GPIO10 tap/hold controls,
brightness/volume, save/reboot persistence, Wi-Fi/AP changes and recovery, AI/offline
fallback, speaker sound quality, and the actual battery circuit if enabled.

Serial: `CAT list`, `CAT 0` through `CAT 119`, `CAT auto`, `SHOT`,
`SCREEN home|settings|about|menu|clock|companion|games|snake`. Existing AI/WIFI/LANG
and brain commands remain available. Settings, framebuffer and MCP endpoints can
be checked without selecting a fake battery value.

Build size and native test results are recorded in the release notes. A successful
compile proves binary fit, not electrical wiring, real speaker quality or live
network/TLS heap headroom. No connected serial device was available during development.

Hardware references: https://docs.platformio.org/en/latest/boards/espressif32/esp32-c3-devkitm-1.html
and https://documentation.espressif.com/esp32-c3_datasheet_en.html
