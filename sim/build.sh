#!/bin/sh
# Build and run the host renderer, then convert the BMPs to PNG.
#
# Renders src/ui.c through the real LVGL at 240x135 in RGB565, so the output
# is what the ST7789 panel shows rather than an impression of it.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LVGL="$ROOT/.pio/libdeps/ttgo-t-display/lvgl"
OUT="${1:-$ROOT/sim/out}"

mkdir -p "$OUT"

cc -std=c11 -O1 -w \
   -DSQUAWK_SIM -DLV_CONF_INCLUDE_SIMPLE=1 \
   -I "$ROOT/include" -I "$ROOT/sim" -I "$LVGL" \
   "$ROOT/sim/main.c" "$ROOT/src/ui.c" \
   $(find "$LVGL/src" -name '*.c') \
   -lm -o "$OUT/squawk-sim"

"$OUT/squawk-sim" "$OUT"

for f in "$OUT"/*.bmp; do
    sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null
    rm -f "$f"
done

echo "frames in $OUT"
