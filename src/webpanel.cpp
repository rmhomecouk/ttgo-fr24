#include "webpanel.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "settings.h"
#include "suntime.h"
#include "ui.h"

extern ui_model_t model;   // owned by main.cpp, built every 250ms

static WebServer server(80);

static const char *pageName(int p) {
  switch (p) {
    case UI_PAGE_OVERVIEW: return "Overview";
    case UI_PAGE_NEAREST:  return "Nearest";
    case UI_PAGE_RADAR:    return "Radar";
    default:               return "?";
  }
}

// ---------- page chrome ----------

static const char PAGE_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ttgo-fr24</title><style>"
  ":root{--bg:#0b1116;--panel:#121a21;--line:#22303a;--cyan:#4fd8e6;"
  "--green:#7ee787;--amber:#e6b34f;--fg:#dbe6ec;--muted:#7d939f}"
  "*{box-sizing:border-box}"
  "body{margin:0;background:var(--bg);color:var(--fg);"
  "font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;padding:20px}"
  "h1{color:var(--cyan);font-size:20px;margin:0 0 4px;letter-spacing:.03em}"
  "h2{color:var(--muted);font-size:12px;text-transform:uppercase;"
  "letter-spacing:.08em;margin:28px 0 10px;border-bottom:1px solid var(--line);"
  "padding-bottom:6px}"
  ".sub{color:var(--muted);margin:0 0 20px}"
  ".stats{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));"
  "gap:10px;max-width:900px}"
  ".card{background:var(--panel);border:1px solid var(--line);border-radius:6px;"
  "padding:10px 12px}"
  ".card .v{font-size:18px;color:var(--cyan);font-variant-numeric:tabular-nums}"
  ".card .k{color:var(--muted);font-size:11px;text-transform:uppercase;"
  "letter-spacing:.05em;margin-top:2px}"
  ".ok{color:var(--green)}.bad{color:var(--amber)}"
  "form{max-width:520px}"
  ".row{display:flex;align-items:center;justify-content:space-between;"
  "gap:12px;padding:7px 0;border-bottom:1px solid var(--line)}"
  ".row label{color:var(--fg)}"
  ".row .hint{display:block;color:var(--muted);font-size:11px;margin-top:1px}"
  ".row input[type=number]{width:100px;background:var(--panel);color:var(--fg);"
  "border:1px solid var(--line);border-radius:4px;padding:5px 7px;"
  "font-variant-numeric:tabular-nums}"
  ".row input[type=checkbox]{width:18px;height:18px}"
  "button{background:var(--cyan);color:#06222b;border:0;border-radius:5px;"
  "padding:10px 18px;font-weight:600;font-size:14px;cursor:pointer;margin-top:18px}"
  "button.reset{background:transparent;color:var(--amber);"
  "border:1px solid var(--amber);margin-left:10px}"
  "a{color:var(--cyan)}"
  "</style></head><body>"
  "<h1>ttgo-fr24</h1><p class='sub'>ADS-B display control panel</p>";

static const char PAGE_FOOT[] = "</body></html>";

static const char STATS_SCRIPT[] =
  "<script>"
  "function fmt(s,k,unit){var e=document.getElementById(k);"
  "if(!e)return;var v=s[k];e.textContent=(v===null||v===undefined)?'--':(v+(unit||''));}"
  "function poll(){fetch('/api/stats').then(r=>r.json()).then(s=>{"
  "fmt(s,'uptimeS','s');fmt(s,'rssi',' dBm');fmt(s,'count','');"
  "fmt(s,'posCount','');fmt(s,'msgTotal','');fmt(s,'msgRate','/s');"
  "fmt(s,'page','');fmt(s,'backlightPct','%');"
  "fmt(s,'nearestCall','');fmt(s,'nearestNm',' nm');"
  "var link=document.getElementById('linked');"
  "link.textContent=s.linked?'up':'down';"
  "link.className='v '+(s.linked?'ok':'bad');"
  "var wifi=document.getElementById('wifi');"
  "wifi.textContent=s.wifi?'up':'down';"
  "wifi.className='v '+(s.wifi?'ok':'bad');"
  "var clk=document.getElementById('clock');"
  "clk.textContent=s.timeSynced?'synced':'not synced';"
  "clk.className='v '+(s.timeSynced?'ok':'bad');"
  "}).catch(()=>{});}"
  "poll();setInterval(poll,2000);"
  "</script>";

// ---------- form field helpers ----------

static String numRow(const char *id, const char *label, const char *hint,
                      double value, int decimals, const char *step) {
  String v = decimals ? String(value, decimals) : String((long)value);
  String s = "<div class='row'><label for='" + String(id) + "'>" + label;
  if (hint && hint[0]) s += "<span class='hint'>" + String(hint) + "</span>";
  s += "</label><input type='number' step='" + String(step) + "' id='" + id +
       "' name='" + id + "' value='" + v + "'></div>";
  return s;
}

static String boolRow(const char *id, const char *label, const char *hint, bool checked) {
  String s = "<div class='row'><label for='" + String(id) + "'>" + label;
  if (hint && hint[0]) s += "<span class='hint'>" + String(hint) + "</span>";
  s += "</label><input type='checkbox' id='" + String(id) + "' name='" + id + "'";
  if (checked) s += " checked";
  s += "></div>";
  return s;
}

static String statCard(const char *id, const char *label, const String &initial) {
  return "<div class='card'><div class='v' id='" + String(id) + "'>" + initial +
         "</div><div class='k'>" + label + "</div></div>";
}

// ---------- handlers ----------

static void handleIndex() {
  String h = PAGE_HEAD;

  h += "<h2>Live</h2><div class='stats'>";
  h += statCard("wifi", "WiFi", model.wifi ? "up" : "down");
  h += statCard("linked", "Feed", model.linked ? "up" : "down");
  h += statCard("rssi", "RSSI", String(model.rssi) + " dBm");
  h += statCard("count", "Aircraft", String(model.count));
  h += statCard("posCount", "Positioned", String(model.posCount));
  h += statCard("msgRate", "Msg rate", String(model.msgRate, 1) + "/s");
  h += statCard("msgTotal", "Msg total", String(model.msgTotal));
  h += statCard("uptimeS", "Uptime", String(model.uptimeS) + "s");
  h += statCard("page", "Page", pageName(ui_page_get()));
  h += statCard("backlightPct", "Backlight", String(suntime_backlight_pct()) + "%");
  h += statCard("clock", "NTP clock", suntime_time_synced() ? "synced" : "not synced");
  h += statCard("nearestCall", "Nearest",
                model.nearest >= 0
                  ? (strlen(model.ac[model.nearest].call) ? model.ac[model.nearest].call
                                                            : model.ac[model.nearest].hex)
                  : "--");
  h += statCard("nearestNm", "Nearest range", model.nearest >= 0
                  ? String(model.ac[model.nearest].nm, 1) + " nm" : "--");
  h += "</div>";

  h += "<form method='POST' action='/save'>";

  h += "<h2>Feed</h2>";
  h += numRow("feedSilenceMs", "Silence timeout",
              "Reconnect if nothing arrives for this long", settings.feedSilenceMs, 0, "1000");
  h += numRow("feedReconnectMs", "Reconnect interval",
              "Minimum gap between connection attempts", settings.feedReconnectMs, 0, "500");
  h += numRow("aircraftStaleMs", "Aircraft timeout",
              "Drop a contact not heard from for this long", settings.aircraftStaleMs, 0, "1000");

  h += "<h2>Paging</h2>";
  h += boolRow("autoCycle", "Auto-cycle pages", "", settings.autoCycle);
  h += numRow("pageDwellMs", "Dwell time", "ms per page while cycling",
              settings.pageDwellMs, 0, "1000");
  h += numRow("manualHoldMs", "Manual hold",
              "ms automation stands down after a button press",
              settings.manualHoldMs, 0, "1000");

  h += "<h2>Nearest priority</h2>";
  h += boolRow("priorityEnable", "Pin Nearest when close", "", settings.priorityEnable);
  h += numRow("priorityRangeNm", "Trigger range", "nautical miles",
              settings.priorityRangeNm, 1, "0.1");
  h += numRow("priorityHystNm", "Release margin",
              "nm further out before the pin releases",
              settings.priorityHystNm, 1, "0.1");

  h += "<h2>Radar</h2>";
  h += numRow("scopeRangeNm", "Scope range", "outer ring, nautical miles",
              settings.scopeRangeNm, 0, "5");

  h += "<h2>Backlight</h2>";
  h += boolRow("dimEnable", "Dim after dark", "sunrise/sunset via NTP + antenna position",
               settings.dimEnable);
  h += numRow("dimDayPct", "Day brightness", "%", settings.dimDayPct, 0, "5");
  h += numRow("dimNightPct", "Night brightness", "%", settings.dimNightPct, 0, "5");

  h += "<button type='submit'>Save</button>"
       "<button type='submit' formaction='/reset' class='reset' "
       "onclick=\"return confirm('Reset every setting to its config.h default?')\">"
       "Reset to defaults</button>";
  h += "</form>";

  h += STATS_SCRIPT;
  h += PAGE_FOOT;
  server.send(200, "text/html", h);
}

static uint32_t argULong(const char *name, uint32_t fallback) {
  return server.hasArg(name) ? (uint32_t)server.arg(name).toInt() : fallback;
}

static float argFloat(const char *name, float fallback) {
  return server.hasArg(name) ? server.arg(name).toFloat() : fallback;
}

static void handleSave() {
  settings.feedSilenceMs   = argULong("feedSilenceMs",   settings.feedSilenceMs);
  settings.feedReconnectMs = argULong("feedReconnectMs", settings.feedReconnectMs);
  settings.aircraftStaleMs = argULong("aircraftStaleMs", settings.aircraftStaleMs);

  settings.autoCycle    = server.hasArg("autoCycle");
  settings.pageDwellMs  = argULong("pageDwellMs",  settings.pageDwellMs);
  settings.manualHoldMs = argULong("manualHoldMs", settings.manualHoldMs);

  settings.priorityEnable  = server.hasArg("priorityEnable");
  settings.priorityRangeNm = argFloat("priorityRangeNm", settings.priorityRangeNm);
  settings.priorityHystNm  = argFloat("priorityHystNm",  settings.priorityHystNm);

  settings.scopeRangeNm = (uint16_t)argULong("scopeRangeNm", settings.scopeRangeNm);

  settings.dimEnable   = server.hasArg("dimEnable");
  settings.dimDayPct   = (uint8_t)argULong("dimDayPct",   settings.dimDayPct);
  settings.dimNightPct = (uint8_t)argULong("dimNightPct", settings.dimNightPct);

  settings_save();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleReset() {
  settings_reset_defaults();
  settings_save();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleStats() {
  String j = "{";
  j += "\"uptimeS\":" + String(model.uptimeS) + ",";
  j += "\"wifi\":" + String(model.wifi ? "true" : "false") + ",";
  j += "\"linked\":" + String(model.linked ? "true" : "false") + ",";
  j += "\"rssi\":" + String(model.rssi) + ",";
  j += "\"count\":" + String(model.count) + ",";
  j += "\"posCount\":" + String(model.posCount) + ",";
  j += "\"msgTotal\":" + String(model.msgTotal) + ",";
  j += "\"msgRate\":" + String(model.msgRate, 1) + ",";
  j += "\"page\":\"" + String(pageName(ui_page_get())) + "\",";
  j += "\"backlightPct\":" + String(suntime_backlight_pct()) + ",";
  j += "\"timeSynced\":" + String(suntime_time_synced() ? "true" : "false") + ",";
  if (model.nearest >= 0) {
    const ui_ac_t &a = model.ac[model.nearest];
    String call = strlen(a.call) ? a.call : a.hex;
    j += "\"nearestCall\":\"" + call + "\",";
    j += "\"nearestNm\":" + String(a.nm, 1);
  } else {
    j += "\"nearestCall\":null,\"nearestNm\":null";
  }
  j += "}";
  server.send(200, "application/json", j);
}

void webpanel_begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/api/stats", HTTP_GET, handleStats);
  server.begin();
}

void webpanel_handle() {
  // mDNS needs an IP to bind to, and WiFi.begin() in setup() doesn't block
  // for one -- so start it lazily, the first time loop() sees an
  // association, rather than racing it at boot.
  static bool announced = false;
  if (!announced && WiFi.status() == WL_CONNECTED) {
    announced = true;
    if (MDNS.begin("squawk")) MDNS.addService("http", "tcp", 80);
    Serial.print("Control panel: http://");
    Serial.print(WiFi.localIP());
    Serial.println(" (or http://squawk.local/)");
  }
  server.handleClient();
}
