# Clock Dashboard native clock assets

The native clock layouts and motion adapt [teojs/clock-dashboard](https://github.com/teojs/clock-dashboard), commit `7ded058b63e14b85b1afc6e63d8b31481c28ed77` (version 1.21.5), copyright 2025 teo, under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Changes: C++/LVGL 8 rendering, 320×240 layout, PSRAM glyph masks, bounded weather particles, and integration with the device's time/weather/configuration services. Keep attribution and modification notice when sharing this adaptation.

The ordinary and clock-only views use **Fredoka** digit masks to retain rounded numeric shapes without packaging Apple's SF Compact Rounded fonts. Fredoka is supplied by the [Google Fonts Fredoka family](https://github.com/google/fonts/tree/main/ofl/fredoka) and is SIL OFL 1.1; see `dashboard_clock_Fredoka_OFL.txt`. The nine UI weights 100–900 map linearly to Fredoka's supported variable weights 300–700 (`font weight = 250 + UI weight / 2`), at its normal width axis 100. Thus the default UI weight 700 uses Fredoka 600. All nine masks differ; these controls are an optical weight scale and do not claim an exact SF font outline or numeric weight match.

Fredoka replaces the earlier Nunito masks to give the tops of `0`, `8`, and `9` a more pronounced round crown. Generation renders at four times the final mask size, filters with Lanczos, and only then quantizes to four-bit alpha. This avoids the broad first raster row produced by direct small-size rendering. At the default UI weight 700, the first solid row of `0`/`8`/`9` is respectively 11/16/13 pixels across their 68/65/62-pixel ink bounds; the previous masks were 21/25/21 pixels across 66/67/66. Transparent margins remain around every complete outline. The historical Nunito OFL notice remains alongside the assets for provenance; current firmware no longer packs its glyphs.

The cached source is Fredoka Version 2.001, file `Fredoka[wdth,wght].ttf`, SHA-256 `2ba02e68b152868aef9ba28e24b3648c7d457fe6f25c761f2c2c53fb61a73fc8`. Its width axis is fixed at 100 during generation.

The compact view's **ClockDigits** shapes are generated from upstream `public/fonts/ClockDigits/ClockDigits.woff2`, identified as a Roboto Flex derivative. Its supplied SIL OFL 1.1 notice is preserved in `dashboard_clock_ClockDigits_OFL.txt`. Firmware contains only rendered 0–9/`-` alpha masks, with no full font file.

`generate_dashboard_clock_assets.py` creates four-bit alpha runs for 110 glyphs across ten faces. Packed mask data totals 102,724 bytes. Each mask centers its actual ink bounds, including ClockDigits' negative left bearing on `8`, and preserves transparent margins. ClockDigits has no visible dash; the invalid-time placeholder uses a locally drawn horizontal dash. Runtime decodes the selected face into PSRAM and recolors its numeric cells, allowing arbitrary RGB colors and opacity without duplicating bitmap assets. The runtime image canvases include a 12-pixel transparent border on all four sides for rolling and rotation; layout also reserves the full silhouette's ±6° projection at the screen edges.

`dashboard_clock_calendar.c` contains 1900–2099 calendar labels generated using [lunar-javascript 1.7.7](https://github.com/6tail/lunar-javascript) (MIT; `dashboard_clock_calendar_LICENSE.txt`). The generator uses the same precedence as upstream: first lunar festival, then first solar festival, then solar term. The table contains 11,077 dated events and 58 label strings, plus Li Chun dates for the corresponding Ganzhi year. Lunar day/month and zodiac use the device's existing tested lunar conversion and Spring Festival dates. Calendar labels are supported for 1900–2099; no label is invented outside this range.

Weather icons here are drawn from basic circles and line segments in C++ and animated at runtime. Weather values come from the device's shared `Weather` fields. AQI is displayed as the API's raw value without applying the upstream Open-Meteo US AQI classification. The device has no precipitation probability field; the clock does not substitute humidity for probability.

Regenerate (inputs may be cached in `build/dashboard-font-source/`):

```text
python hardware/scripts/generate_dashboard_clock_assets.py --rounded <Fredoka.ttf> --compact <ClockDigits.ttf>
node hardware/scripts/generate_dashboard_calendar.cjs <lunar.cjs>
```

The digit generator requires Pillow and fontTools. Converting upstream WOFF2 to TTF additionally requires Brotli. These are asset-authoring dependencies, not firmware dependencies.
