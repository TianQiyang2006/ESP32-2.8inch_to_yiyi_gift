# M5Stack StopWatch GrokBot

Xiaozhi board variant for M5Stack StopWatch / C152. It keeps the existing
StopWatch hardware setup and replaces the default emoji UI with a GrokBot
animation rendered through the normal LVGL display pipeline.

## Runtime design

- 360x360, 4-bit grayscale, 30 FPS GrokBot clips.
- Only the 28 GrokBot states used by Xiaozhi are packaged. The full 53-state
  showcase atlas is not included.
- The atlas is stored in the existing 8 MiB `assets` partition, so the standard
  dual-OTA partition table remains unchanged.
- RGB565 double buffers, packed frames and delta frames are allocated in PSRAM.
- CO5300 QSPI runs at 80 MHz. LVGL continues to flush through its internal
  64-line DMA buffer rather than reading a full PSRAM frame directly.
- Animation decoding runs in a separate low-priority task on core 0. Audio and
  the application event loop are not blocked by frame decoding.

## State mapping

All built-in Xiaozhi emoji names are mapped to GrokBot clips. Examples:

| Xiaozhi emotion | GrokBot state |
| --- | --- |
| neutral, relaxed | idle |
| happy, loving, delicious | happy |
| confident, cool | proud |
| funny, silly, winking, kissy | playful |
| embarrassed | shy |
| crying, sad | sad |
| shocked, surprised | surprised |
| sleepy | sleeping |
| warning | alerting |

Assistant states such as listening, thinking, speaking, connecting, searching,
updating, notifications and failures select their corresponding GrokBot status
clips. Internal Xiaozhi alert icons (`gear`, `cloud_download`, `download`,
`cancel`, `cloud_off`, `link`) are mapped to loading, uploading, failed and
approving states.

Button 1 keeps the normal wake/chat behavior and Button 2 keeps the normal
volume behavior. No GrokBot showcase controls are assigned to them.

## Build

```sh
. "$HOME/esp/esp-idf/export.ps1"
python scripts/build.py m5stack/stopwatch-grokbot --name m5stack-stopwatch-grokbot
```

The checked-in atlas can be regenerated from the full StopWatch-GrokBot atlas:

```sh
python scripts/build_grokbot_subset.py \
  ../Stopwatch-GrokBot/main/grokbot.atlas \
  main/boards/m5stack/stopwatch-grokbot/assets/grokbot.atlas \
  --size 360 --max-size 0x700000
```

## Assets update note

The Xiaozhi OTA path updates application partitions, not the `assets`
partition. Updating the animation therefore requires flashing the assets
partition or the complete merged image. The display pauses animation decoding
while Xiaozhi reports that it is loading assets.

## Hardware validation still required

Build success does not validate the panel at 80 MHz or the concurrent
animation/audio workload. Check frame rate, audio continuity, PSRAM headroom,
panel stability, touch/button behavior and long-running playback on hardware.
