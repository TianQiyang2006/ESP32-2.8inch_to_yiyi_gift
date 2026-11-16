# BarBar Clock source reference

Retrieved on 2026-11-15 from <https://github.com/aednlaxer/flutter-clock-challenge-barbar>.
Archive URL: <https://codeload.github.com/aednlaxer/flutter-clock-challenge-barbar/zip/refs/heads/master>.
Archive SHA-256: `3b378bdae19b10aa64845b55c238b70a3a88cba54d35f4818d7226f866dc990d`.
The archive captures the master branch contents at retrieval; no immutable commit ID was available from the rate-limited GitHub API.

Original source and licenses are under `upstream/flutter-clock-challenge-barbar-master/`.
`upstream-reference.png` is a contact sheet of frames 0, 15, 30 and 45 of the upstream demonstration GIF.
`digit-bars-128.json` and `digit-bars-reference.h` preserve all ten original digit shapes using integer vertical coordinates in the range 0–128.

## Appearance and layout

The clock uses negative space cut out of many narrow, vertical teal bars. Background is `#090042`, and bars and supporting text use `#00C2B7`. Bar segments have rounded ends unless they meet the top or bottom edge of the screen. Main digits are custom geometry, not font glyphs. Supporting date and temperature use the bundled Oswald Medium font, licensed under SIL OFL. Original weather icons use weather-icons-lite, also under OFL.

The original HH:MM display contains 39 columns: four columns of left padding; six for hour tens; one separator; six for hour units; two separators; one colon; two separators; six for minute tens; one separator; six for minute units; four right padding. At width W, pitch is W/39.6, bar width is 0.4 times pitch, and leading space is 0.6 times pitch. Digit indices are 4, 11, 22 and 29; colon index is 19. Each digit column may contain up to four solid bar intervals. Empty digit columns remain full-height bars.

Digit active height is 44% of the viewport, beginning at y=28% and ending at y=72%. Interval endpoint 0 is special and maps to the top edge; endpoint 128 is special and maps to the bottom edge. Interior endpoints map linearly into that active height. Colon solid intervals are [0, 0.3], [0.4, 0.6] and [0.7, 1], leaving two negative dots.

The bottom footer has a weather icon and integer temperature at left, uppercase localized `MMMEd` date at right, and bottom spacing of 5% of screen height. Bars interrupt around the footer bounds to keep text clear. The upstream painter mistakenly passes the date safe area to the weather branch too; an adaptation should respect each footer's actual bounds.

## Animation and adaptation

The original shows HH:MM and updates on minute boundaries. Its transition lasts 300 ms with Flutter `Curves.decelerate` (equivalent to `1-(1-t)^2`). Top and bottom segment endpoints interpolate directly. Interior segments appear by growing from their center and disappear by shrinking toward it; when one segment splits into two, both originate from the previous interval.

The requested seconds display and double-tap toggle are additions to the original. For the 320×240 panel, preserve the four-digit layout for HH:MM and extend the same sequence by a second colon and two six-column digits for HH:MM:SS. The resulting 57-column version gives bars about 2.22 pixels wide, so native antialiased LVGL rounded rectangles or fixed-point drawing should preserve usable two-pixel strokes. Maintaining constant bar thickness while transitioning pitch/positions is another option, provided endpoints use the original geometry. Full-height bars and footer gaps should remain in both layouts.

Use existing project time and weather sources. Show readable unknown-time placeholders until the clock is valid. Skip per-frame animation once bar endpoints settle, release timers on page deletion, and treat time jumps as immediate updates. Support a double-tap window consistent with the existing clocks (320 ms), ignore single taps, cancel tap candidates on drags, and retain the established left-edge return gesture.

## Licensing

The clock source is copyright 2020 aednlaxer under BSD-3-Clause. Retain the upstream copyright, conditions and disclaimer with adapted production geometry/code. Oswald Medium and weather-icons-lite have their own OFL notices under the source archive. If the implementation uses existing project fonts or native weather drawings, only the assets actually copied need to be bundled in the firmware assets directory.
