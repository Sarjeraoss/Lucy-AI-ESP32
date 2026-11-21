# Lucy Cat OS for Clipbot ESP32-C3 Super Mini

This target preserves the original Lucy Cat OS renderer (24 behaviours x 5 variants = 120 clips) and uses the hardware configuration already tested on Clipbot.

| Device | Pin |
| --- | --- |
| SH1106 OLED SDA | GPIO21 |
| SH1106 OLED SCL | GPIO20 |
| MAX98357A DIN | GPIO3 |
| MAX98357A BCLK | GPIO4 |
| MAX98357A LRC/WS | GPIO5 |
| OLED VCC | 3.3 V |
| Amplifier VIN | 5 V (adequately rated supply) |
| Grounds | Common |

The existing Lucy volume setting is a percentage, **not** the 10000 PCM amplitude used by the earlier 1 kHz speaker test. This build does not promise safe continuous power for an 8-ohm 0.5 W speaker. Start at low volume. Lucy is silent during Wi-Fi setup and plays its greeting only after station Wi-Fi connects. The Lucy-Setup access point remains available according to Lucy settings.

## Build

From `offline/` run:

```sh
pio run -e lucy_clipbot_c3
```

GitHub Actions publishes `lucy-clipbot-c3-app.bin` as a build artifact when the build job succeeds. Build and hardware validation are required before calling it compatible.

## OTA caveat — important

This environment selects the dual-app-slot `min_spiffs.csv` layout rather than `huge_app.csv`, but **a web firmware upload cannot replace an ESP32 partition table**. Your already-installed Web OTA updater must have a suitable OTA layout, and its **actual** OTA slot must be large enough for the compiled app. The installed bootloader, partition table and OTA updater must agree. An app image built under this configuration is not automatically compatible with the updater's current partition table.

Check the current partition map on the running device before uploading. If the current device has incompatible partitions, stop: a standard application Web OTA update cannot correct the partition table. Never send `*-usb.bin`, `bootloader.bin`, or `partitions.bin` to the Web OTA app endpoint.

The first target is to display Lucy's real cat animations on SH1106. The INMP441 microphone is not yet integrated.

## Automatic cat sounds and chat response

On the Clipbot build Lucy automatically cycles through the original 120 procedural
animations. Once initial networking is ready, she also performs occasional
low-duty-cycle synthesized meows, chirps, purrs, and chatter, synchronized with
matching animations on the Home screen (roughly every 9–17 seconds). If a
Wi-Fi station is configured, cat audio remains muted until it connects; in
AP-only mode, no station connection is needed. A configured station disconnect
also temporarily mutes further new sounds.

Text sent through the built-in `/say` web interface or USB Serial console
opens the Companion screen with Lucy's reply and triggers a short cat sound.
This is **not** speech synthesis: the response is displayed as text, and the
MAX98357A plays a cat vocalization. Lucy cannot yet listen to the INMP441
microphone or pronounce arbitrary responses. Use Lucy's existing Speaker Volume
setting to mute or adjust the output. Fresh devices default to volume 35/100;
saved user volume settings are retained.

For the 8-ohm, 0.5 W speaker, monitor for distortion or heating. Volume is
a firmware relative control, not a verified physical loudness/power limit.

## Spoken reply integration (optional)

For accurate general answers, configure a reachable **AI model server** in Lucy Settings. The
on-device offline intent model is intentionally small and cannot answer arbitrary questions.
A missing or unreachable AI server causes offline fallback. A TTS server changes the sound
of a reply but does not improve factual accuracy.

This Clipbot branch can stream speech to the ESP32 MAX98357A:
- Run `python -m pip install edge-tts` on a local Windows PC, and install FFmpeg on PATH.
- Run `python tools/clipbot_tts_server.py` on that PC. Internet access is required for Edge TTS.
- Connect Clipbot and the PC to the same trusted Wi-Fi network.
- On Lucy's `http://192.168.4.1/settings` page, enable **Speak text replies**,
  and set the TTS server URL to `http://PC-LAN-IP:8765`.
- Configure the separate Lucy AI model endpoint if you want general-question answering.
- Send a chat message through the web console or serial. Text stays on the OLED,
  and the ESP requests 16 kHz signed-16-bit mono PCM speech via HTTP and plays it
  on its I2S amplifier. The server must return a Content-Length.
- A TTS failure leaves the reply visible but cannot provide spoken audio.
  Check Serial for diagnostics. The server binds all interfaces by default;
  only run on a trusted LAN and block untrusted access in your PC firewall.

The speech server is **not installed on ESP32**. It must remain running for
spoken replies. Arbitrary English/Marathi/Hindi pronunciation depends on
the chosen TTS voice; the default is English. The ESP does not run a neural
speech synthesizer locally. Listening through INMP441 is a separate feature.

For an 8-ohm 0.5 W speaker start at low volume: TTS samples are digitally
attenuated by `volume / 200`. The actual acoustic/electrical power remains
unverified.
