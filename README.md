# ttgo-fr24

An ADS-B traffic display for the LilyGO/TTGO T-Display (ESP32 + 240×135 ST7789),
driven by a local dump1090 / readsb receiver over the SBS-1 BaseStation feed.

![The three pages: Overview, Nearest and Radar](docs/pages.png)

Each panel above is one full screen at 240×135, shown at 3×. These are not
photographs or drawings — they come straight out of [the host renderer](#the-host-renderer),
which runs the real LVGL over the same `src/ui.c` that ships to the board, so
they are what the panel displays, pixel for pixel. The traffic is seeded
(Loganair and easyJet being the local operators at Glasgow).

Three pages, changed with the two on-board buttons:

| Page | Shows |
| --- | --- |
| **Overview** | Aircraft count in a ring gauge, message rate, positions decoded, uptime |
| **Nearest** | Closest contact — callsign, ICAO hex, squawk, altitude, ground speed, vertical rate, range, and a track rose |
| **Radar** | Plan view — compass rose, range rings, traffic plotted by range and bearing from the antenna |

Colours follow the Boeing/Airbus Navigation Display convention rather than
being invented: cyan for data and scale annotation, **magenta for the selected
target** (so the nearest aircraft is magenta on both the radar and its own
page), green for other traffic, white for headline values, and amber reserved
for things that actually warrant attention — feed down, RSSI below −70 dBm, a
descending aircraft, or a 7500/7600/7700 squawk.

## Hardware

- LilyGO T-Display ESP32 ([board notes](https://done.land/components/microcontroller/families/esp/esp32/developmentboards/esp32s/t-display/))
- 240×135 ST7789, landscape (rotation 1)
- Top button `GPIO0` pages forward, bottom button `GPIO35` pages back

`GPIO35` is input-only with no internal pull-up — it relies on the board's
on-board one, so `pinMode(BTN_PREV, INPUT)` is deliberate.

## Data source

The receiver's SBS-1 feed on TCP port 30003: unauthenticated, plaintext CSV,
one message per line. No API key or login needed. Confirm yours is up with:

```bash
nc your-receiver-host 30003
```

Messages arrive sparsely when traffic is light — long gaps are normal and are
not a fault.

## Build and flash

PlatformIO does everything. If you don't have it, a project-local venv avoids
fighting your system Python (macOS ships an externally-managed one that
refuses `pip install`):

```bash
python3 -m venv .pio-venv && .pio-venv/bin/pip install platformio
```

Then configure and flash:

```bash
cp include/secrets.h.example include/secrets.h
```

Fill in your WiFi, your receiver's host, and the antenna's latitude/longitude —
range and bearing are computed from that position, so an approximate one gives
approximate answers.

```bash
.pio-venv/bin/platformio run -t upload
```

`include/secrets.h` is gitignored. Keep it that way.

## The host renderer

`sim/` compiles the real LVGL against a memory framebuffer, runs the same
`src/ui.c` that ships to the device, and writes PNGs at exactly 240×135 in
RGB565 — same library, same fonts, same colour depth as the panel:

```bash
./sim/build.sh
```

Output lands in `sim/out/`: each page on its own at 1:1, plus a 3× contact
sheet of all three side by side. About four seconds, no board required.

This exists because mocking up an LVGL UI in HTML validates nothing — it only
encodes what you assumed the library does. Rendering it for real caught several
bugs that reasoning had not, and cut the design loop from a flash cycle to a
few seconds. `src/ui.c` is deliberately free of Arduino headers so both targets
compile it unchanged; `include/lv_conf.h` switches its tick source on
`SQUAWK_SIM`.

To try an edge case — no traffic, an emergency squawk, an over-long callsign —
edit the seed table in `sim/main.c` and re-render.

## LVGL 8.4 notes

Things worth knowing, all of them found the hard way:

- **Never call `lv_obj_remove_style_all()` on a tileview tile.**
  `lv_tileview_add_tile` positions tiles with `lv_obj_set_pos`/`set_size`,
  which are *local styles* in LVGL 8 — removing them stacks every page at
  x=0. The tileview's own geometry and padding must also be final *before*
  any tile is added, since the constructor reads its content size then.
- **`lv_meter` unconditionally draws a number at every major tick**, which is
  meaningless on a compass. Suppress with
  `lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS)` — ticks are
  drawn from a separate line descriptor and survive.
- **Major ticks must divide onto the cardinals.** With `tickCnt-1` intervals
  over 360°, the major interval is `(tickCnt-1)/4`. Reusing one constant
  across roses with different tick counts silently loses E and S.
- **`%f` in `lv_label_set_text_fmt` needs `LV_SPRINTF_USE_FLOAT 1`**, or the
  format string is emitted literally.
- **`pushColors(..., swap = true)`** when bridging LVGL to `TFT_eSPI`, or the
  RGB565 byte order is wrong and every colour is scrambled.
- **PlatformIO does not put the project's `include/` on the path for library
  sources.** `-D` flags reach them but `-I` does not, so `lv_conf.h` is
  invisible to LVGL's own files until you add `-I include` to `build_flags`.
- Montserrat line heights are 12→15px, 16→18px, 20→22px, 24→27px, and an
  uppercase glyph at 12px runs about 8px wide — which puts a hard ceiling of
  roughly 8 characters on a caption in a 120px column.

## Layout

```
src/ui.c        the whole UI, pure LVGL, no platform headers
include/ui.h    the model struct the UI renders from
src/main.cpp    WiFi, SBS-1 parsing, aircraft table, buttons, ESP32 glue
sim/            host renderer
include/lv_conf.h
platformio.ini
```

A 16px header sits on the screen *outside* the tileview, carrying the page
name, a link LED, the aircraft count and a page indicator. Keeping it there
rather than duplicating it into each tile means no tile is ever wide enough to
let its neighbour bleed in at the screen edge.
