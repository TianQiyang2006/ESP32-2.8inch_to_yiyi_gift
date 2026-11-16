# Yiyi ES3C28P GrokBot

ESP32-S3 N16R8, ILI9341 IPS 320x240 landscape, FT6336G touch, ES8311
microphone/speaker and active-low amplifier enable. This is a separate board
identity; M5Stack StopWatch pins and power-management code are not used.

This variant retains the project's GrokBot state/emotion mapping and all 28
animation clips. The atlas is resampled to 200x200. Rendering targets 20 FPS,
with a 20-line internal DMA strip and animation buffers in PSRAM. There is no
panel TE signal. Text and status icons use regular 16px/4bpp antialiased fonts.
The common Chinese CBIN font is embedded in app1 so the 2.5 MiB assets partition
can retain all 28 animations. An app-only upgrade also works with the original
14px/1bpp assets pack: the display rejects its monochrome font. Subtitle and
status containers reserve the full font line height to avoid clipped glyphs.
The glyph-push capability conservatively advertises the built-in basic charset;
the embedded common font covers a superset of it at the same size and bpp.

## Dual application contract

- app0 desktop: `0x10000`, `0x880000` bytes (8.5 MiB); app1 GrokBot:
  `0x890000`, `0x400000` bytes (4 MiB).
- Assets: `0xc90000`, `0x280000` bytes. This replaces the old `model` label and
  raw model contents with the new project's indexed assets package.
- Desktop LittleFS remains `spiffs` at `0xf10000`, `0xf0000` bytes.
- NVS and OTA metadata addresses/capacities remain unchanged.
- Both firmware and asset downloads are disabled. Discovery, activation and
  protocol configuration still use the original project services.
- NVS initialization failure never erases shared desktop settings.

The desktop must recognize version `2.5.3` before opening this app1. Its
existing entry sets the app1 boot partition and restarts. The top-right exit
button sits below the battery icon, sets app0, stores `yiyi-desk/voiceReturn`,
and restarts. The startup board/version footer is suppressed. Exit remains
available without Wi-Fi or server connectivity.

Wi-Fi credentials are imported from desktop `yiyi-desk/ssid` and `pass`; this
application does not start AP provisioning. Existing `board/uuid`, audio
volume and display brightness are retained. Touch imports the existing
version-2 affine calibration, with two-point calibration and default panel
mapping as fallbacks. The codec runs capture/output at 24 kHz; AudioService
resamples uplink PCM to its 16 kHz processing format.

## Build

Activate ESP-IDF 6.0.1 or later, then:

```sh
python scripts/build.py yiyi-es3c28p-grokbot --name yiyi-es3c28p-grokbot --language zh-CN --wake-word nihaoxiaozhi
```

On this workspace's Windows setup, `hardware/scripts/build_grokbot_firmware.ps1`
uses an ASCII build cache and archives the application, resources and partition
table in `build/grokbot-firmware`. The script checks the 4 MiB application and
2.5 MiB resource limits. Never flash the default merged image to this device:
it also contains a bootloader and places the application in app0.

The 2026-11-12 layout transfers 1 MiB from app1 to the desktop. Migrate an
existing 7.5/5 MiB device with `hardware/scripts/install_grokbot.py
--migrate-layout`, supplying a fresh checked 16 MiB backup and the rebuilt
desktop. The installer moves app1, updates the partition table and starts
app0 while preserving NVS, the deployed assets and LittleFS. Ordinary app-only
updates require the new layout; `--restore` follows the backup's original
addresses and capacities.

Regenerate the atlas from the unchanged M5Stack input:

```sh
python scripts/build_grokbot_subset.py main/boards/m5stack/stopwatch-grokbot/assets/grokbot.atlas main/boards/yiyi-es3c28p-grokbot/assets/grokbot.atlas --size 200 --max-size 0x200000
```

USB diagnostics: `status`, `back`, `talk`, `tap X Y`, `emotion happy`,
`screenshot`, `fontcheck`, `text CONTENT` (or `text` to clear), and
`notify CONTENT`. Commands are limited to 80 UTF-8 bytes. `fontcheck` reports
the actual subtitle font, missing glyph count and label/container bounds.
Synthetic taps use the same LVGL input callback as physical touch.

Board touch helpers and battery policy were copied from the workspace's tested
ES3C28P implementation. Battery ADC is GPIO9 through a 2:1 divider; charging and
discharging are unknown because the board has no readable charger/VBUS signal.
Build success is not a substitute for panel, touch and acoustic validation.

## Workspace hardware check (2026-11-05)

The IDF 6.0.1 image was installed into app1 with the bounded installer, followed
by independent flash verification. The desktop launcher entered app1 and the
LVGL exit button returned to app0's application list. Existing brightness,
volume, touch calibration, alarms and game score were preserved. Four emotion
screenshots were inspected; offline animation measured 20 FPS in a 35-second
monitoring window without a restart. The revised button below the battery icon
also passed the return check; the startup board/version footer is suppressed.
After updating the saved desktop network to the laptop's 2.4 GHz hotspot, both
applications connected. GrokBot reached online idle with its wake detector
running and successfully opened the cloud audio channel into listening, then
returned to idle. Physical finger operation, actual wake-word detection,
speech recognition and playback quality still require user checks.

## Font check (2026-11-11)

An app-only update to regular 16px/4bpp fonts passed independent flash
verification. The common font's 25px line box fits in a 29px subtitle bar;
Chinese, Latin descenders, digits and punctuation rendered without missing
glyphs or clipped label bounds. Long subtitles scrolled and clearing text hid
the bar. The original 14px resource pack remained installed, verifying that
it cannot override the new font. A 35-second online observation maintained
20 FPS without restart, and the LVGL exit button returned to the desktop with
its firmware identity, brightness, volume and checked saved settings intact.
Run `hardware/scripts/check_grokbot_fonts.py` from the workspace root while
GrokBot is idle to repeat the font and layout checks.
