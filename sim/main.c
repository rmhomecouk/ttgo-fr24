/*
 * sim/main.c — host renderer for the Squawk display.
 *
 * Compiles the real LVGL and the real src/ui.c against a memory framebuffer,
 * renders each page at exactly 240x135 in RGB565, and writes the result out
 * as BMP. Whatever this produces is what the ST7789 shows, pixel for pixel --
 * same library, same fonts, same layout code, same colour depth.
 */
#include "lvgl.h"
#include "ui.h"
#include "sim_tick.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCALE 3
#define GAP   16

static uint32_t tick_ms = 0;
uint32_t sim_millis(void) { return tick_ms; }

static lv_color_t fb[UI_W * UI_H];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++) {
        for (int x = a->x1; x <= a->x2; x++) {
            if (x >= 0 && x < UI_W && y >= 0 && y < UI_H)
                fb[y * UI_W + x] = *px;
            px++;
        }
    }
    lv_disp_flush_ready(drv);
}

/* ---------------------------------------------------------------- BMP out */

static void put32(unsigned char *p, unsigned v)
{
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

/* rgb is a w*h array of packed 0xRRGGBB */
static int write_bmp(const char *path, const unsigned *rgb, int w, int h)
{
    const int rowRaw = w * 3;
    const int rowPad = (rowRaw + 3) & ~3;
    const unsigned dataSz = (unsigned)rowPad * h;
    unsigned char hdr[54];
    unsigned char *row;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    put32(hdr + 2, 54 + dataSz);
    put32(hdr + 10, 54);
    put32(hdr + 14, 40);
    put32(hdr + 18, (unsigned)w);
    put32(hdr + 22, (unsigned)h);
    hdr[26] = 1; hdr[28] = 24;
    put32(hdr + 34, dataSz);
    fwrite(hdr, 1, sizeof(hdr), f);

    row = calloc(1, rowPad);
    for (int y = h - 1; y >= 0; y--) {          /* BMP rows run bottom-up */
        for (int x = 0; x < w; x++) {
            const unsigned c = rgb[y * w + x];
            row[x * 3 + 0] = c & 0xFF;          /* B */
            row[x * 3 + 1] = (c >> 8) & 0xFF;   /* G */
            row[x * 3 + 2] = (c >> 16) & 0xFF;  /* R */
        }
        fwrite(row, 1, rowPad, f);
    }
    free(row);
    fclose(f);
    return 0;
}

/* The honest RGB565 -> RGB888 expansion the panel itself performs. */
static unsigned expand565(lv_color_t c)
{
    const unsigned v = c.full;
    unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    r = (r * 255 + 15) / 31;
    g = (g * 255 + 31) / 63;
    b = (b * 255 + 15) / 31;
    return (r << 16) | (g << 8) | b;
}

/* ------------------------------------------------------------- fake world */

typedef struct { const char *call, *hex; int alt, gs, trk, vs; const char *sq;
                 float nm, brg; } seed_t;

/* Plausible Glasgow-area traffic: Loganair and easyJet are the local
 * operators, plus north-atlantic overflights up at cruise. */
static const seed_t SEED[] = {
    { "LOG6R",   "4ca7f1", 11025,  284,  32,  1856, "4713",  4.2f,  41.0f },
    { "EZY149Y", "4cab22", 37000,  452, 247,     0, "7401", 11.8f, 213.0f },
    { "BAW2960", "406a3c", 24975,  398, 168, -1920, "5203",  7.4f, 126.0f },
    { "RYR84KP", "4ca2d3", 35000,  441, 305,     0, "3617", 16.1f, 298.0f },
    { "VIR9",    "4008f2", 39000,  478, 271,     0, "2045", 23.6f, 265.0f },
    { "LOG82",   "4ca901",  8300,  241, 351,  1408, "4655",  9.9f,   8.0f },
    { "EIN62T",  "4ca55b", 33000,  429, 194,     0, "6104", 18.3f, 187.0f },
    { "DLH8NX",  "3c66a1", 38000,  466,  88,     0, "1723", 27.9f,  74.0f },
    { "SHT4C",   "4064b8", 19000,  372, 155,  -960, "5511", 13.2f, 148.0f },
    { "N512DF",  "a6b3c1",  4500,  168,  62,     0, "1200",  6.7f,  63.0f },
    { "TOM71H",  "4cadd4", 36000,  455, 232,     0, "3350", 31.4f, 226.0f },
    { "WUK33",   "4cae09", 28000,  410, 119,  1216, "4402", 21.7f, 112.0f },
    { "AFR1180", "394c11", 41000,  489, 178,     0, "2610", 34.8f, 171.0f },
    { "LOG4N",   "4ca8ee",  2100,  152, 214,  -704, "4661",  2.8f, 331.0f },
};
#define SEED_N ((int)(sizeof(SEED) / sizeof(SEED[0])))

static void seed_model(ui_model_t *m)
{
    static const unsigned char RATE[UI_RATE_N] = {
        9, 11, 14, 12, 17, 21, 19, 16, 22, 26, 24, 20, 18, 23, 29,
        31, 27, 25, 30, 34, 28, 26, 33, 37, 32, 29, 35, 31, 27, 24
    };
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < SEED_N && i < UI_MAX_AC; i++) {
        ui_ac_t *a = &m->ac[i];
        snprintf(a->call, sizeof(a->call), "%s", SEED[i].call);
        snprintf(a->hex, sizeof(a->hex), "%s", SEED[i].hex);
        snprintf(a->squawk, sizeof(a->squawk), "%s", SEED[i].sq);
        a->havePos = true;
        a->alt = SEED[i].alt; a->gs = SEED[i].gs;
        a->trk = SEED[i].trk; a->vs = SEED[i].vs;
        a->nm = SEED[i].nm;   a->brg = SEED[i].brg;
        m->count++;
        m->posCount++;
    }
    m->nearest  = 13;          /* LOG4N at 2.8nm */
    m->wifi     = true;
    m->linked   = true;
    m->rssi     = -58;
    m->uptimeS  = 6127;
    m->msgTotal = 48213;
    m->msgRate  = 24.0f;
    m->scopeNm  = 40;
    memcpy(m->rateHist, RATE, UI_RATE_N);
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    static lv_disp_draw_buf_t db;
    static lv_color_t buf[UI_W * UI_H];
    static lv_disp_drv_t drv;
    ui_model_t m;
    const char *outdir = argc > 1 ? argv[1] : ".";
    char path[512];

    lv_init();
    lv_disp_draw_buf_init(&db, buf, NULL, UI_W * UI_H);
    lv_disp_drv_init(&drv);
    drv.draw_buf  = &db;
    drv.flush_cb  = flush_cb;
    drv.hor_res   = UI_W;
    drv.ver_res   = UI_H;
    lv_disp_drv_register(&drv);

    ui_create();
    seed_model(&m);
    ui_update(&m);

    /* Panels sit side by side: the screen is landscape and the sheet has to
     * read that way too. Stacking them vertically makes a 240x135 display
     * look like a portrait strip. */
    const int pw = UI_W * SCALE;
    const int ph = UI_H * SCALE;
    const int cw = pw * UI_PAGE_COUNT + GAP * (UI_PAGE_COUNT - 1);
    const int th = ph;
    unsigned *sheet = calloc((size_t)cw * th, sizeof(unsigned));
    for (int i = 0; i < cw * th; i++) sheet[i] = 0x141A1F;   /* contact sheet ground */

    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        ui_page_set(p, false);
        /* settle any layout/animation work, then force a full repaint */
        for (int k = 0; k < 6; k++) { tick_ms += 40; lv_timer_handler(); }
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);

        unsigned one[UI_W * UI_H];
        for (int i = 0; i < UI_W * UI_H; i++) one[i] = expand565(fb[i]);

        /* each page on its own, at 1:1 with the panel */
        snprintf(path, sizeof(path), "%s/page%d.bmp", outdir, p);
        write_bmp(path, one, UI_W, UI_H);

        const int ox = p * (pw + GAP);
        for (int y = 0; y < ph; y++)
            for (int x = 0; x < pw; x++)
                sheet[y * cw + ox + x] = one[(y / SCALE) * UI_W + (x / SCALE)];
    }

    snprintf(path, sizeof(path), "%s/sheet.bmp", outdir);
    write_bmp(path, sheet, cw, th);
    free(sheet);

    printf("rendered %d pages to %s\n", UI_PAGE_COUNT, outdir);
    return 0;
}
