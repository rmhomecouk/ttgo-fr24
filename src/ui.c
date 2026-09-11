/*
 * ui.c — Squawk display, LVGL 8.4, 240x135.
 *
 * Palette and symbology follow the Boeing/Airbus Navigation Display
 * convention the hardware is imitating: cyan for data and scale annotation,
 * magenta for the selected target, green for other traffic, amber for
 * cautions, white for headline values.
 *
 * Geometry is fixed rather than computed: the panel is 240x135 and always
 * will be, so every coordinate below is a real pixel on a real screen.
 * Font line heights are the true LVGL Montserrat metrics --
 * 12->15px, 16->18px, 20->22px, 24->27px.
 */
#include "ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ theme */

#define C_BG      lv_color_hex(0x05090C)
#define C_LINE    lv_color_hex(0x1E2E3A)
#define C_TICK    lv_color_hex(0x1C4450)
#define C_DIM     lv_color_hex(0x64808F)
#define C_TEXT    lv_color_hex(0xE4EEF5)
#define C_CYAN    lv_color_hex(0x2FD4E8)
#define C_MAGENTA lv_color_hex(0xF05BC8)
#define C_GREEN   lv_color_hex(0x57E07A)
#define C_AMBER   lv_color_hex(0xF5A524)

#define F12 &lv_font_montserrat_12
#define F16 &lv_font_montserrat_16
#define F20 &lv_font_montserrat_20
#define F24 &lv_font_montserrat_24

#define HDR_H   16
#define TILE_W  240
#define TILE_H  (UI_H - HDR_H)   /* 119 */

/* ---------------------------------------------------------------- widgets */

static lv_obj_t *header, *hdrTitle, *hdrLed, *hdrCount, *hdrDot[UI_PAGE_COUNT];
static lv_obj_t *tv, *tile[UI_PAGE_COUNT];
static int curPage = 0;

/* overview page */
static lv_obj_t *ovArc, *ovCount, *ovRate, *ovPos, *ovUp;

/* radar page */
static lv_obj_t *scDot[UI_MAX_AC];
static lv_obj_t *scTraffic, *scNearVal, *scRange;
static int scCx, scCy, scR;          /* rose centre and outer-ring radius */

/* nearest page */
static lv_obj_t *nrBody, *nrEmpty;
static lv_obj_t *nrCall, *nrHex, *nrSqk;
static lv_obj_t *nrAlt, *nrSpd, *nrVsi, *nrRng;
static lv_obj_t *nrRose, *nrTrkVal;
static lv_meter_indicator_t *nrNeedle;

static const char *PAGE_NAME[UI_PAGE_COUNT] = { "OVERVIEW", "NEAREST", "RADAR" };

/* ---------------------------------------------------------------- helpers */

/* A bare container: no background, no border, no padding, no scrolling.
 * Children positioned inside land on exactly the pixel you ask for. */
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const lv_font_t *font,
                       lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* Small-caps style caption: dim, 12px, letter-spaced. */
static lv_obj_t *caption(lv_obj_t *parent, int x, int y, const char *txt)
{
    lv_obj_t *l = label(parent, x, y, F12, C_DIM, txt);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static lv_obj_t *rule(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *o = box(parent, x, y, w, 1);
    lv_obj_set_style_bg_color(o, C_LINE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* A caption over a value -- the repeating unit of every readout on the
 * device. Returns the value label so the caller can update it. */
static lv_obj_t *field(lv_obj_t *parent, int x, int y, const char *cap,
                       const lv_font_t *font, lv_color_t color, const char *init)
{
    caption(parent, x, y, cap);
    return label(parent, x, y + 15, font, color, init);
}

/* An unfilled circle -- used for the scope range rings. */
static lv_obj_t *ring(lv_obj_t *parent, int cx, int cy, int r, lv_color_t c)
{
    lv_obj_t *o = box(parent, cx - r, cy - r, r * 2, r * 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *dot(lv_obj_t *parent, int d, lv_color_t c)
{
    lv_obj_t *o = box(parent, 0, 0, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* A compass rose. lv_meter always prints a number at each major tick
 * (lv_meter.c draws a label from the scale value), which is meaningless on a
 * compass -- zeroing text_opa on LV_PART_TICKS suppresses the labels while
 * leaving the tick lines, which are drawn from line_dsc, untouched. */
/* majorNth must be chosen so majors land on the cardinals: with tickCnt-1
 * intervals over 360 degrees, that is (tickCnt - 1) / 4. */
static lv_obj_t *rose(lv_obj_t *parent, int x, int y, int d,
                      int tickCnt, int majorNth, int minorLen, int majorLen,
                      lv_meter_scale_t **outScale)
{
    lv_obj_t *m = lv_meter_create(parent);
    lv_obj_remove_style_all(m);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, d, d);
    lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);

    lv_meter_scale_t *s = lv_meter_add_scale(m);
    lv_meter_set_scale_ticks(m, s, tickCnt, 1, minorLen, C_TICK);
    lv_meter_set_scale_major_ticks(m, s, majorNth, 2, majorLen, C_CYAN, 0);
    /* 0..360 over a full turn starting at 12 o'clock. Using 360 rather than
     * 350 as the max keeps bearing-to-angle a 1:1 map, so headings in the
     * 350s don't get clamped onto north. */
    lv_meter_set_scale_range(m, s, 0, 360, 360, 270);
    if (outScale) *outScale = s;
    return m;
}

/* 37000 -> "37,000" */
static void groupInt(char *out, size_t n, long v)
{
    char raw[16];
    int len, i, j = 0, lead;
    if (v < 0) { snprintf(out, n, "%ld", v); return; }
    len = snprintf(raw, sizeof(raw), "%ld", v);
    lead = len % 3;
    for (i = 0; i < len && j < (int)n - 2; i++) {
        if (i && (i - lead) % 3 == 0) out[j++] = ',';
        out[j++] = raw[i];
    }
    out[j] = '\0';
}

/* ----------------------------------------------------------------- header */

static void buildHeader(void)
{
    header = box(lv_scr_act(), 0, 0, UI_W, HDR_H);

    hdrTitle = label(header, 6, 0, F12, C_CYAN, PAGE_NAME[0]);
    lv_obj_set_style_text_letter_space(hdrTitle, 2, 0);
    lv_obj_align(hdrTitle, LV_ALIGN_LEFT_MID, 6, 0);

    /* page position, centred */
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        hdrDot[i] = dot(header, 4, i == 0 ? C_CYAN : C_LINE);
        lv_obj_align(hdrDot[i], LV_ALIGN_CENTER, (i - 1) * 8, 0);
    }

    /* link state + traffic count, right */
    hdrCount = label(header, 0, 0, F12, C_TEXT, "0");
    lv_obj_t *ac = label(header, 0, 0, F12, C_DIM, "AC");
    lv_obj_align(ac, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_align_to(hdrCount, ac, LV_ALIGN_OUT_LEFT_MID, -4, 0);

    hdrLed = dot(header, 5, C_AMBER);
    lv_obj_align_to(hdrLed, hdrCount, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    rule(lv_scr_act(), 0, HDR_H - 1, UI_W);
}

/* ------------------------------------------------------------- page: radar */

static void buildRadar(lv_obj_t *t)
{
    const int d = 112;                 /* rose diameter */
    scCx = 2 + d / 2;                  /* 58 */
    scCy = (TILE_H - d) / 2 + d / 2;   /* 59 */
    scR  = 44;                         /* outer range ring, inside the ticks */

    rose(t, 2, (TILE_H - d) / 2, d, 37, 9, 4, 9, NULL);   /* 10 deg ticks */
    ring(t, scCx, scCy, scR, C_LINE);
    ring(t, scCx, scCy, scR / 2, C_LINE);

    /* heading index -- the lubber line at the top of the rose */
    lv_obj_t *idx = box(t, scCx - 1, scCy - d / 2, 2, 7);
    lv_obj_set_style_bg_color(idx, C_TEXT, 0);
    lv_obj_set_style_bg_opa(idx, LV_OPA_COVER, 0);

    /* traffic, drawn over the rose */
    for (int i = 0; i < UI_MAX_AC; i++) {
        scDot[i] = dot(t, 5, C_GREEN);
        lv_obj_add_flag(scDot[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* own ship last, so the centre cross stays readable when a close
     * contact plots on top of it */
    lv_obj_t *h = box(t, scCx - 4, scCy, 9, 1);
    lv_obj_set_style_bg_color(h, C_TEXT, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_t *v = box(t, scCx, scCy - 4, 1, 9);
    lv_obj_set_style_bg_color(v, C_TEXT, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);

    /* right column */
    const int cx = 122, cw = 112;
    scTraffic = field(t, cx, 4,  "TRAFFIC", F24, C_TEXT, "0");
    rule(t, cx, 48, cw);
    scNearVal = field(t, cx, 53, "NEAREST / NM", F24, C_MAGENTA, "--");
    scRange   = label(t, cx, 100, F12, C_CYAN, "RANGE 40NM");
    lv_obj_set_style_text_letter_space(scRange, 1, 0);
}

static void updateRadar(const ui_model_t *m)
{
    lv_label_set_text_fmt(scTraffic, "%d", m->count);
    lv_label_set_text_fmt(scRange, "RANGE %dNM", m->scopeNm);

    if (m->nearest >= 0)
        lv_label_set_text_fmt(scNearVal, "%d.%d",
                              (int)m->ac[m->nearest].nm,
                              (int)(m->ac[m->nearest].nm * 10) % 10);
    else
        lv_label_set_text(scNearVal, "--");

    const float scale = (float)scR / (float)(m->scopeNm ? m->scopeNm : 1);
    int slot = 0;
    for (int i = 0; i < m->count && slot < UI_MAX_AC; i++) {
        const ui_ac_t *a = &m->ac[i];
        if (!a->havePos || a->nm > m->scopeNm) continue;

        /* bearing is true, 0 = north = up, clockwise */
        const float rad = a->brg * 0.01745329f;
        const float rr  = a->nm * scale;
        const int px = scCx + (int)(rr * sinf(rad));
        const int py = scCy - (int)(rr * cosf(rad));

        const bool sel = (i == m->nearest);
        const int  dd  = sel ? 7 : 5;
        lv_obj_t *o = scDot[slot++];
        lv_obj_set_size(o, dd, dd);
        lv_obj_set_style_bg_color(o, sel ? C_MAGENTA : C_GREEN, 0);
        lv_obj_set_pos(o, px - dd / 2, py - dd / 2);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    for (; slot < UI_MAX_AC; slot++)
        lv_obj_add_flag(scDot[slot], LV_OBJ_FLAG_HIDDEN);
}

/* ----------------------------------------------------------- page: nearest */

static void buildNearest(lv_obj_t *t)
{
    nrEmpty = label(t, 0, 0, F16, C_DIM, "NO TRAFFIC IN RANGE");
    lv_obj_set_width(nrEmpty, TILE_W);
    lv_obj_set_style_text_align(nrEmpty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nrEmpty, LV_ALIGN_CENTER, 0, 0);

    nrBody = box(t, 0, 0, TILE_W, TILE_H);

    /* identity block */
    nrCall = label(nrBody, 6, 0, F24, C_TEXT, "--------");
    lv_label_set_long_mode(nrCall, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(nrCall, 156);

    nrHex = label(nrBody, 6, 29, F12, C_DIM, "------");
    lv_obj_set_style_text_letter_space(nrHex, 1, 0);

    nrSqk = label(nrBody, 74, 29, F12, C_CYAN, "");
    lv_obj_set_style_text_letter_space(nrSqk, 1, 0);

    rule(nrBody, 6, 46, 156);

    /* 2x2 readout grid: caption 15px over value 18px = 33px per cell.
     * Rows at 50 and 84 leave the last value ending on y=117, clear of the
     * 119px tile edge. */
    nrAlt = field(nrBody, 6,  50, "ALT / FT",   F16, C_TEXT, "--");
    nrSpd = field(nrBody, 86, 50, "SPD / KT",   F16, C_TEXT, "--");
    nrVsi = field(nrBody, 6,  84, "V/S / FPM",  F16, C_CYAN, "--");
    nrRng = field(nrBody, 86, 84, "RNG / NM",   F16, C_MAGENTA, "--");

    /* divider, then the track rose */
    lv_obj_t *vr = box(nrBody, 166, 6, 1, 107);
    lv_obj_set_style_bg_color(vr, C_LINE, 0);
    lv_obj_set_style_bg_opa(vr, LV_OPA_COVER, 0);

    lv_meter_scale_t *s = NULL;
    nrRose = rose(nrBody, 172, 4, 64, 13, 3, 3, 6, &s);   /* 30 deg ticks */
    nrNeedle = lv_meter_add_needle_line(nrRose, s, 2, C_TEXT, -4);

    /* hub, so the needle reads as an instrument rather than a stray line */
    lv_obj_t *hub = dot(nrBody, 5, C_TEXT);
    lv_obj_set_pos(hub, 172 + 32 - 2, 4 + 32 - 2);

    lv_obj_t *c = caption(nrBody, 172, 74, "TRACK");
    lv_obj_set_width(c, 64);
    lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);

    nrTrkVal = label(nrBody, 172, 89, F16, C_TEXT, "---");
    lv_obj_set_width(nrTrkVal, 64);
    lv_obj_set_style_text_align(nrTrkVal, LV_TEXT_ALIGN_CENTER, 0);
}

static void updateNearest(const ui_model_t *m)
{
    if (m->nearest < 0) {
        lv_obj_add_flag(nrBody, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(nrEmpty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(nrBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(nrEmpty, LV_OBJ_FLAG_HIDDEN);

    const ui_ac_t *a = &m->ac[m->nearest];
    char buf[16];

    lv_label_set_text(nrCall, a->call[0] ? a->call : a->hex);
    lv_label_set_text(nrHex, a->hex);

    if (a->squawk[0]) {
        /* 7500 hijack, 7600 radio failure, 7700 general emergency -- the only
         * codes that warrant the caution colour */
        const bool emg = !strcmp(a->squawk, "7500") ||
                         !strcmp(a->squawk, "7600") ||
                         !strcmp(a->squawk, "7700");
        lv_label_set_text_fmt(nrSqk, "SQ %s", a->squawk);
        lv_obj_set_style_text_color(nrSqk, emg ? C_AMBER : C_CYAN, 0);
    } else {
        lv_label_set_text(nrSqk, "");
    }

    if (a->alt >= 0) { groupInt(buf, sizeof(buf), a->alt); lv_label_set_text(nrAlt, buf); }
    else             lv_label_set_text(nrAlt, "--");

    if (a->gs >= 0) lv_label_set_text_fmt(nrSpd, "%d", a->gs);
    else            lv_label_set_text(nrSpd, "--");

    if (a->vs) {
        lv_label_set_text_fmt(nrVsi, "%+d", a->vs);
        lv_obj_set_style_text_color(nrVsi, a->vs > 0 ? C_CYAN : C_AMBER, 0);
    } else {
        lv_label_set_text(nrVsi, "0");
        lv_obj_set_style_text_color(nrVsi, C_DIM, 0);
    }

    lv_label_set_text_fmt(nrRng, "%d.%d", (int)a->nm, (int)(a->nm * 10) % 10);

    if (a->trk >= 0) {
        lv_label_set_text_fmt(nrTrkVal, "%03d", a->trk);
        lv_meter_set_indicator_value(nrRose, nrNeedle, a->trk);
        lv_obj_clear_flag(nrRose, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(nrTrkVal, "---");
        lv_meter_set_indicator_value(nrRose, nrNeedle, 0);
    }
}

/* ---------------------------------------------------------- page: overview */

static void buildOverview(lv_obj_t *t)
{
    const int d  = 100;
    const int ay = (TILE_H - d) / 2;    /* 9 */

    /* Aircraft count in the ring. 270 degrees of sweep opening at the
     * bottom, which leaves the gap under the readout rather than beside it. */
    ovArc = lv_arc_create(t);
    lv_obj_set_pos(ovArc, 6, ay);
    lv_obj_set_size(ovArc, d, d);
    lv_obj_remove_style(ovArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ovArc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(ovArc, 135);
    lv_arc_set_bg_angles(ovArc, 0, 270);
    lv_arc_set_range(ovArc, 0, 30);
    lv_arc_set_value(ovArc, 0);
    lv_obj_set_style_bg_opa(ovArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ovArc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ovArc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ovArc, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ovArc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ovArc, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ovArc, 6, LV_PART_INDICATOR);

    ovCount = label(t, 6, ay + 30, F24, C_TEXT, "0");
    lv_obj_set_width(ovCount, d);
    lv_obj_set_style_text_align(ovCount, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *cap = caption(t, 6, ay + 58, "AIRCRAFT");
    lv_obj_set_width(cap, d);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);

    /* Right-hand stat column: three rows of caption over value. */
    const int cx = 118, cw = 116;
    ovRate = field(t, cx, 5,  "MSG / SEC", F16, C_CYAN, "0");
    rule(t, cx, 40, cw);
    ovPos  = field(t, cx, 43, "POSITIONS", F16, C_TEXT, "0");
    rule(t, cx, 78, cw);
    ovUp   = field(t, cx, 81, "UPTIME",    F16, C_TEXT, "00:00:00");
}

static void updateOverview(const ui_model_t *m)
{
    lv_label_set_text_fmt(ovCount, "%d", m->count);
    lv_arc_set_value(ovArc, m->count);

    lv_label_set_text_fmt(ovRate, "%d", (int)(m->msgRate + 0.5f));
    lv_obj_set_style_text_color(ovRate, m->linked ? C_CYAN : C_AMBER, 0);

    lv_label_set_text_fmt(ovPos, "%d", m->posCount);

    lv_label_set_text_fmt(ovUp, "%02d:%02d:%02d",
                          (int)(m->uptimeS / 3600),
                          (int)((m->uptimeS / 60) % 60),
                          (int)(m->uptimeS % 60));
}

/* -------------------------------------------------------------- public API */

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    buildHeader();

    /* The tileview holds only the pages. Keeping the header outside it means
     * a tile can never be wide enough to let its neighbour show at the edge.
     *
     * Note: lv_tileview_add_tile positions each tile from the tileview's
     * *content* size at the moment of creation, and does it with
     * lv_obj_set_pos/set_size -- which are local styles. So the tileview's
     * geometry and padding must be final before any tile is added, and a tile
     * must never be passed through lv_obj_remove_style_all(), which would
     * strip the very position that lays the pages out side by side. */
    tv = lv_tileview_create(scr);
    lv_obj_set_pos(tv, 0, HDR_H);
    lv_obj_set_size(tv, TILE_W, TILE_H);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_style_radius(tv, 0, 0);
    lv_obj_set_style_pad_all(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(tv);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        tile[i] = lv_tileview_add_tile(tv, i, 0, LV_DIR_HOR);
        lv_obj_set_style_bg_opa(tile[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile[i], 0, 0);
        lv_obj_set_style_radius(tile[i], 0, 0);
        lv_obj_set_style_pad_all(tile[i], 0, 0);
        lv_obj_clear_flag(tile[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(tile[i], LV_SCROLLBAR_MODE_OFF);
    }

    buildOverview(tile[0]);
    buildNearest(tile[1]);
    buildRadar(tile[2]);
}

void ui_update(const ui_model_t *m)
{
    lv_label_set_text_fmt(hdrCount, "%d", m->count);
    lv_obj_set_style_bg_color(hdrLed, m->linked ? C_GREEN : C_AMBER, 0);

    updateOverview(m);
    updateNearest(m);
    updateRadar(m);
}

void ui_page_set(int page, bool animate)
{
    if (page < 0) page = UI_PAGE_COUNT - 1;
    if (page >= UI_PAGE_COUNT) page = 0;
    curPage = page;
    lv_obj_set_tile_id(tv, page, 0, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    lv_label_set_text(hdrTitle, PAGE_NAME[page]);
    for (int i = 0; i < UI_PAGE_COUNT; i++)
        lv_obj_set_style_bg_color(hdrDot[i], i == page ? C_CYAN : C_LINE, 0);
}

void ui_page_next(void) { ui_page_set(curPage + 1, true); }
void ui_page_prev(void) { ui_page_set(curPage - 1, true); }
int  ui_page_get(void)  { return curPage; }
