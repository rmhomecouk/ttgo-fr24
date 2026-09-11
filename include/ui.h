/*
 * ui.h — Squawk display UI, pure LVGL.
 *
 * Deliberately free of Arduino/ESP32 headers so the exact same code compiles
 * for the device (via PlatformIO) and for the host renderer in sim/, which
 * produces pixel-accurate 240x135 frames.
 */
#ifndef SQUAWK_UI_H
#define SQUAWK_UI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MAX_AC    24
#define UI_RATE_N    30   /* seconds of message-rate history on the LINK page */

#define UI_W        240
#define UI_H        135

typedef struct {
    char     call[9];    /* callsign, "" if not yet seen                   */
    char     hex[7];     /* ICAO 24-bit address, lowercase hex             */
    bool     havePos;    /* a lat/lon has arrived                          */
    int16_t  trk;        /* track over ground, degrees true, -1 unknown    */
    int32_t  alt;        /* barometric altitude, feet, -1 unknown          */
    int16_t  gs;         /* ground speed, knots, -1 unknown                */
    int16_t  vs;         /* vertical rate, ft/min                          */
    char     squawk[5];  /* mode-A code, "" if unknown                     */
    float    nm;         /* range from receiver, nautical miles            */
    float    brg;        /* bearing from receiver, degrees true            */
} ui_ac_t;

typedef struct {
    ui_ac_t  ac[UI_MAX_AC];
    uint8_t  count;                /* aircraft currently tracked           */
    uint8_t  posCount;             /* of those, how many have a position   */
    int8_t   nearest;              /* index into ac[], -1 when none        */

    bool     wifi;                 /* associated to the AP                 */
    bool     linked;               /* TCP session to the feed is up        */
    int16_t  rssi;                 /* dBm                                  */
    uint32_t uptimeS;
    uint32_t msgTotal;
    float    msgRate;              /* messages/sec, smoothed               */
    uint8_t  rateHist[UI_RATE_N];  /* one sample per second, newest last   */

    uint8_t  scopeNm;              /* outer range ring, nautical miles     */
} ui_model_t;

/* Build the widget tree on the active screen. Call once, after lv_init(). */
void ui_create(void);

/* Push a snapshot of the world into the widgets. Safe to call every frame. */
void ui_update(const ui_model_t *m);

/* Paging. */
void ui_page_next(void);
void ui_page_prev(void);
int  ui_page_get(void);
void ui_page_set(int page, bool animate);

#define UI_PAGE_OVERVIEW 0
#define UI_PAGE_NEAREST  1
#define UI_PAGE_RADAR    2
#define UI_PAGE_COUNT    3

#ifdef __cplusplus
}
#endif

#endif /* SQUAWK_UI_H */
