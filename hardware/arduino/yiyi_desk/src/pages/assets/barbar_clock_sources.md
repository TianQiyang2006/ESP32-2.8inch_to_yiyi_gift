# BarBar Clock geometry

- Upstream: <https://github.com/aednlaxer/flutter-clock-challenge-barbar>
- Author/copyright: aednlaxer, 2020.
- Source license: BSD-3-Clause; unchanged notice in `barbar_clock_LICENSE.txt`.
- Original shape data: `digital_clock/lib/data/digits_collection.dart`, generated upstream from `digits-generator/digits.svg`.
- Retrieved 2026-11-15 from the master branch archive. Archive SHA-256: `3b378bdae19b10aa64845b55c238b70a3a88cba54d35f4818d7226f866dc990d`.
- Saved source/reference: `design/barbar-clock-20261115/` in this workspace.
- Reproduce header and license: `python hardware/scripts/generate_barbar_clock_assets.py`.

The header preserves all ten original negative-space digit shapes exactly. Each digit has six columns with up to four solid vertical intervals per column, sorted from top to bottom. Coordinates are integers normalized to 0–128. Padded `{0,0}` entries are ignored according to `barbar_clock_digit_counts`. Endpoint 0 meets the viewport top; endpoint 128 meets the viewport bottom. Other endpoints map into the central 44% of the viewport, beginning at 28% height.

The original palette is background `#090042`, bars/text `#00C2B7`. Original bar pitch is viewport width divided by `(column count + 0.6)`, with teal width equal to 40% of pitch. HH:MM uses 39 columns with digit starts 4, 11, 22, 29 and colon column 19. Extending the same pattern to HH:MM:SS uses 57 columns, digit starts 4, 11, 22, 29, 40, 47 and colon columns 19, 37. Colon intervals are [0,0.3], [0.4,0.6], [0.7,1]. Original animation is 300 ms with decelerate easing (`1-(1-t)^2`).

Seconds and double-tap visibility are additions for this LVGL port. Supporting text uses an Oswald Medium subset at 14 pixels, 4bpp, uncompressed, with ASCII U+0020–U+007E plus degree U+00B0. Font copyright is The Oswald Project Authors (2016), SIL OFL 1.1; the unchanged notice is `barbar_clock_Oswald_OFL.txt`.

Reproduce the supporting font from the repository root (PowerShell):

```powershell
.\tools\font-tools\node_modules\.bin\lv_font_conv.cmd --font design/barbar-clock-20261115/upstream/flutter-clock-challenge-barbar-master/digital_clock/fonts/oswald/Oswald-Medium.ttf --size 14 --bpp 4 --format lvgl --lv-include lvgl.h --lv-font-name barbar_clock_oswald14 --range 0x20-0x7e,0xb0 --no-compress -o hardware/arduino/yiyi_desk/src/pages/assets/barbar_clock_oswald14.h
```

## Original weather icons

The weather footer uses the original bundled `digital_clock/fonts/weather-icons-lite/weather-icons-lite.ttf`, rasterized at 16 pixels, 4bpp, uncompressed. The exported font is named `barbar_clock_weather16` and contains only the eight weather glyphs used by the upstream clock. It is compiled as `barbar_clock_weather16.c` with a separate extern-C header to avoid generated font-private symbol collisions with the Oswald header.

Weather icon mapping follows upstream `digital_clock/lib/model/weather_icon.dart`:

| Condition | Upstream constant | Unicode |
|---|---|---|
| Cloudy | `IconCloudy` | U+F013 |
| Fog | `IconFoggy` | U+F04A |
| Rain | `IconRainy` | U+F019 |
| Snow | `IconSnowy` | U+F01B |
| Clear day | `IconClearDay` | U+F00D |
| Clear night | `IconClearNight` | U+F02E |
| Thunderstorm | `IconThunderstorm` | U+F01E |
| Wind | `IconWindy` | U+F050 |

The original font is copyright Paul Reed (2017) and Erik Flowers (2014), licensed under SIL OFL 1.1. Its unchanged notice is `barbar_clock_Weather_OFL.txt`. The LVGL subset uses the new internal resource name above. Reproduce it from the repository root:

```powershell
.\tools\font-tools\node_modules\.bin\lv_font_conv.cmd --font design/barbar-clock-20261115/upstream/flutter-clock-challenge-barbar-master/digital_clock/fonts/weather-icons-lite/weather-icons-lite.ttf --size 16 --bpp 4 --format lvgl --lv-include lvgl.h --lv-font-name barbar_clock_weather16 --range 0xf013,0xf04a,0xf019,0xf01b,0xf00d,0xf02e,0xf01e,0xf050 --no-compress -o hardware/arduino/yiyi_desk/src/pages/assets/barbar_clock_weather16.c
```
