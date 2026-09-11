#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "secrets.h"
#include "config.h"
#include "ui.h"

// TTGO T-Display onboard buttons
#define BTN_NEXT 0   // top button, GPIO0
#define BTN_PREV 35  // bottom button, GPIO35 (input-only, has on-board pull-up)

#define MAX_AC UI_MAX_AC


struct Aircraft {
  char hex[7] = "";
  char flight[9] = "";
  bool haveFlight = false;
  int32_t altitude = INT32_MIN;
  float gs = NAN;
  float track = NAN;
  double lat = NAN, lon = NAN;
  bool havePos = false;
  int32_t vrate = INT32_MIN;
  char squawk[5] = "";
  uint32_t lastSeen = 0;
  bool used = false;
};

static Aircraft fleet[MAX_AC];
static uint32_t msgCount = 0;
static uint32_t posCount = 0;
static uint32_t bootMs = 0;

static TFT_eSPI tft = TFT_eSPI();
static WiFiClient feed;
static uint32_t lastConnectAttempt = 0;
static String lineBuf;

// ---------- aircraft table (unchanged logic from the TFT_eSPI build) ----------

static Aircraft *findOrCreate(const char *hex) {
  Aircraft *oldest = nullptr;
  for (int i = 0; i < MAX_AC; i++) {
    if (fleet[i].used && strcmp(fleet[i].hex, hex) == 0) return &fleet[i];
    if (!fleet[i].used) return &fleet[i];
    if (!oldest || fleet[i].lastSeen < oldest->lastSeen) oldest = &fleet[i];
  }
  return oldest;
}

static void pruneStale() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_AC; i++) {
    if (fleet[i].used && now - fleet[i].lastSeen > AIRCRAFT_STALE_MS) fleet[i] = Aircraft();
  }
}

static void rangeBearing(double lat, double lon, float &nm, float &brg) {
  const double R_NM = 3440.065;
  double la1 = radians(RECEIVER_LAT), la2 = radians(lat);
  double dLa = radians(lat - RECEIVER_LAT);
  double dLo = radians(lon - RECEIVER_LON);
  double a = sin(dLa / 2) * sin(dLa / 2) + cos(la1) * cos(la2) * sin(dLo / 2) * sin(dLo / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  nm = R_NM * c;
  double y = sin(dLo) * cos(la2);
  double x = cos(la1) * sin(la2) - sin(la1) * cos(la2) * cos(dLo);
  double deg = degrees(atan2(y, x));
  brg = fmod(deg + 360.0, 360.0);
}

static int nearestIndex() {
  int best = -1;
  float bestNm = 1e9;
  for (int i = 0; i < MAX_AC; i++) {
    if (!fleet[i].used || !fleet[i].havePos) continue;
    float nm, brg;
    rangeBearing(fleet[i].lat, fleet[i].lon, nm, brg);
    if (nm < bestNm) { bestNm = nm; best = i; }
  }
  return best;
}

static int trackedCount() {
  int n = 0;
  for (int i = 0; i < MAX_AC; i++) if (fleet[i].used) n++;
  return n;
}

// ---------- SBS-1 BaseStation line parsing (unchanged) ----------

static String field(const String &line, int idx) {
  int start = 0, cur = 0;
  while (cur < idx) {
    start = line.indexOf(',', start);
    if (start < 0) return "";
    start++;
    cur++;
  }
  int end = line.indexOf(',', start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

static void handleLine(const String &line) {
  if (!line.startsWith("MSG,")) return;
  msgCount++;

  int type = field(line, 1).toInt();
  String hexS = field(line, 4);
  if (hexS.length() == 0 || hexS.length() > 6) return;

  Aircraft *ac = findOrCreate(hexS.c_str());
  if (!ac->used) *ac = Aircraft();
  strlcpy(ac->hex, hexS.c_str(), sizeof(ac->hex));
  ac->used = true;
  ac->lastSeen = millis();

  String cs = field(line, 10);
  cs.trim();
  if (cs.length() > 0) {
    strlcpy(ac->flight, cs.c_str(), sizeof(ac->flight));
    ac->haveFlight = true;
  }

  String altS = field(line, 11);
  if (altS.length() > 0) ac->altitude = altS.toInt();

  if (type == 4) {
    String gsS = field(line, 12), trkS = field(line, 13), vrS = field(line, 16);
    if (gsS.length() > 0) ac->gs = gsS.toFloat();
    if (trkS.length() > 0) ac->track = trkS.toFloat();
    if (vrS.length() > 0) ac->vrate = vrS.toInt();
  }

  if (type == 2 || type == 3) {
    String latS = field(line, 14), lonS = field(line, 15);
    if (latS.length() > 0 && lonS.length() > 0) {
      if (!ac->havePos) posCount++;
      ac->lat = latS.toDouble();
      ac->lon = lonS.toDouble();
      ac->havePos = true;
    }
  }

  String sqS = field(line, 17);
  if (sqS.length() > 0) strlcpy(ac->squawk, sqS.c_str(), sizeof(ac->squawk));
}

// ---------- networking ----------

static uint32_t lastRxMs = 0;

static void ensureFeed() {
  if (WiFi.status() != WL_CONNECTED) return;

  // A half-open socket is the failure mode that needs a manual reset: the
  // receiver goes away, but this end keeps reporting connected because it
  // only ever reads and a dead peer is invisible until you write. Silence
  // past the threshold is the one symptom we get, so act on it.
  if (feed.connected() && millis() - lastRxMs > FEED_SILENCE_MS) {
    feed.stop();
  }

  if (feed.connected()) return;

  uint32_t now = millis();
  if (now - lastConnectAttempt < FEED_RECONNECT_MS) return;
  lastConnectAttempt = now;

  if (feed.connect(RECEIVER_HOST, RECEIVER_PORT)) {
    lastRxMs = now;   // grace period, or the watchdog trips straight away
    lineBuf = "";     // a reconnect must not splice onto a half-read line
  }
}

static void pollFeed() {
  if (!feed.connected()) return;
  if (feed.available()) lastRxMs = millis();
  while (feed.available()) {
    char c = feed.read();
    if (c == '\n') {
      lineBuf.trim();
      if (lineBuf.length() > 0) handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      if (lineBuf.length() < 200) lineBuf += c;
    }
  }
}

// ---------- LVGL display driver ----------

static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf[240 * 40];

static void dispFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// ---------- message rate history ----------

static uint8_t rateHist[UI_RATE_N];
static uint32_t lastRateTick = 0;
static uint32_t lastMsgCount = 0;

static void tickRate() {
  uint32_t now = millis();
  if (now - lastRateTick < 1000) return;
  lastRateTick = now;
  uint32_t d = msgCount - lastMsgCount;
  lastMsgCount = msgCount;
  memmove(rateHist, rateHist + 1, UI_RATE_N - 1);
  rateHist[UI_RATE_N - 1] = d > 255 ? 255 : (uint8_t)d;
}

// ---------- fleet -> ui_model_t ----------

static ui_model_t model;

static void buildModel() {
  memset(&model, 0, sizeof(model));

  int nearestFleet = nearestIndex();
  int n = 0;

  for (int i = 0; i < MAX_AC && n < UI_MAX_AC; i++) {
    if (!fleet[i].used) continue;
    const Aircraft &a = fleet[i];
    ui_ac_t &o = model.ac[n];

    strlcpy(o.hex, a.hex, sizeof(o.hex));
    if (a.haveFlight) strlcpy(o.call, a.flight, sizeof(o.call));
    strlcpy(o.squawk, a.squawk, sizeof(o.squawk));

    o.alt = (a.altitude == INT32_MIN) ? -1 : a.altitude;
    o.gs  = isnan(a.gs)    ? -1 : (int16_t)a.gs;
    o.trk = isnan(a.track) ? -1 : (int16_t)a.track;
    o.vs  = (a.vrate == INT32_MIN) ? 0 : (int16_t)a.vrate;

    o.havePos = a.havePos;
    if (a.havePos) {
      rangeBearing(a.lat, a.lon, o.nm, o.brg);
      model.posCount++;
    }

    if (i == nearestFleet) model.nearest = n;
    n++;
  }

  model.count   = n;
  model.nearest = (nearestFleet >= 0) ? model.nearest : -1;

  model.wifi    = (WiFi.status() == WL_CONNECTED);
  model.linked  = feed.connected();
  model.rssi    = model.wifi ? WiFi.RSSI() : 0;
  model.uptimeS = (millis() - bootMs) / 1000;
  model.msgTotal = msgCount;
  model.msgRate = rateHist[UI_RATE_N - 1];
  model.scopeNm = SCOPE_RANGE_NM;
  memcpy(model.rateHist, rateHist, UI_RATE_N);
}

// ---------- buttons ----------
//
// Top button pages forward, bottom button pages back. GPIO35 is input-only
// and has no internal pull-up, so it relies on the T-Display's on-board one.

static bool lastNext = true, lastPrev = true;
static uint32_t lastBtnMs = 0;
static uint32_t lastPageChange = 0;
static uint32_t manualUntil = 0;

static void handleButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < 180) return;

  bool nextNow = digitalRead(BTN_NEXT);
  bool prevNow = digitalRead(BTN_PREV);
  bool pressed = false;

  if (lastNext && !nextNow) { ui_page_next(); pressed = true; }
  else if (lastPrev && !prevNow) { ui_page_prev(); pressed = true; }

  if (pressed) {
    lastBtnMs = now;
    lastPageChange = now;
    manualUntil = now + MANUAL_HOLD_MS;   // you drive for a while
  }

  lastNext = nextNow;
  lastPrev = prevNow;
}

// ---------- automatic paging ----------

// Reads the range buildModel() already computed rather than redoing 24
// haversines -- this is called on the UI cadence, not every loop pass.
static float nearestRangeNm() {
  return model.nearest >= 0 ? model.ac[model.nearest].nm : -1.0f;
}

static void updatePaging() {
  uint32_t now = millis();

  if ((int32_t)(manualUntil - now) > 0) return;   // hands off, user is driving

#if PRIORITY_ENABLE
  // Latch onto the Nearest page while something is close, and hold it there
  // until the contact has moved a clear margin back out -- otherwise an
  // aircraft sitting on the boundary flaps the page every update.
  static bool latched = false;
  float nm = nearestRangeNm();

  if (nm < 0) {
    latched = false;
  } else if (!latched && nm <= PRIORITY_RANGE_NM) {
    latched = true;
  } else if (latched && nm > PRIORITY_RANGE_NM + PRIORITY_HYST_NM) {
    latched = false;
  }

  if (latched) {
    if (ui_page_get() != UI_PAGE_NEAREST) {
      ui_page_set(UI_PAGE_NEAREST, true);
      lastPageChange = now;
    }
    return;
  }
#endif

#if AUTO_CYCLE
  if (now - lastPageChange >= PAGE_DWELL_MS) {
    ui_page_next();
    lastPageChange = now;
  }
#endif
}

// ---------- setup / loop ----------

void setup() {
  Serial.begin(115200);
  bootMs = millis();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT);

  tft.init();
  tft.setRotation(1);           // landscape, 240x135
  tft.fillScreen(TFT_BLACK);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, 240 * 40);

  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res  = 240;
  drv.ver_res  = 135;
  drv.flush_cb = dispFlush;
  drv.draw_buf = &drawBuf;
  lv_disp_drv_register(&drv);

  ui_create();
  ui_page_set(UI_PAGE_OVERVIEW, false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
  ensureFeed();
  pollFeed();
  pruneStale();
  tickRate();
  handleButtons();

  static uint32_t lastUi = 0;
  if (millis() - lastUi > 250) {
    lastUi = millis();
    buildModel();
    updatePaging();     // needs the freshly computed nearest range
    ui_update(&model);
  }

  lv_timer_handler();
  delay(5);
}
