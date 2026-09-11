#include "suntime.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include "config.h"
#include "settings.h"
#include "secrets.h"

static const uint8_t  BL_CHANNEL  = 0;
static const uint32_t BL_FREQ_HZ  = 5000;
static const uint8_t  BL_RES_BITS = 8;

static bool    ntpStarted = false;
static uint8_t currentPct = 100;

static double sinD(double deg) { return sin(radians(deg)); }
static double cosD(double deg) { return cos(radians(deg)); }
static double tanD(double deg) { return tan(radians(deg)); }
static double asinD(double x)  { return degrees(asin(x)); }
static double acosD(double x)  { return degrees(acos(x)); }
static double atanD(double x)  { return degrees(atan(x)); }

/* Sunrise/Sunset Algorithm, Almanac for Computers 1990 -- the standard
 * closed-form approximation, accurate to a couple of minutes away from the
 * poles. Returns UTC decimal hours, or NAN if the sun does not cross the
 * horizon that day at this latitude (high-latitude midsummer/midwinter). */
static double sunEventUTC(int dayOfYear, double lat, double lon, bool sunrise) {
  double lngHour = lon / 15.0;
  double t = dayOfYear + ((sunrise ? 6.0 : 18.0) - lngHour) / 24.0;

  double M = (0.9856 * t) - 3.289;
  double L = M + (1.916 * sinD(M)) + (0.020 * sinD(2 * M)) + 282.634;
  L = fmod(L + 360.0, 360.0);

  double RA = atanD(0.91764 * tanD(L));
  RA = fmod(RA + 360.0, 360.0);
  double Lquad  = floor(L  / 90.0) * 90.0;
  double RAquad = floor(RA / 90.0) * 90.0;
  RA = (RA + (Lquad - RAquad)) / 15.0;

  double sinDec = 0.39782 * sinD(L);
  double cosDec = cosD(asinD(sinDec));

  double cosH = (cosD(90.833) - (sinDec * sinD(lat))) / (cosDec * cosD(lat));
  if (cosH > 1.0 || cosH < -1.0) return NAN;

  double H = sunrise ? (360.0 - acosD(cosH)) : acosD(cosH);
  H /= 15.0;

  double T = H + RA - (0.06571 * t) - 6.622;
  return fmod(T - lngHour + 24.0, 24.0);
}

static void applyBacklightPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  currentPct = pct;
  ledcWrite(BL_CHANNEL, (255UL * pct) / 100);
}

void suntime_init() {
  ledcSetup(BL_CHANNEL, BL_FREQ_HZ, BL_RES_BITS);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  applyBacklightPct(100);   // full brightness until the first update() decides
}

bool suntime_time_synced() {
  time_t now;
  time(&now);
  return now > 1700000000;   // past 2023-11-14 -- well before this was written
}

uint8_t suntime_backlight_pct() { return currentPct; }

void suntime_update() {
  static uint32_t lastCheck = 0;
  uint32_t now = millis();
  if (lastCheck != 0 && now - lastCheck < 30000UL) return;
  lastCheck = now;

  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
  }

  if (!settings.dimEnable) { applyBacklightPct(100); return; }
  if (!suntime_time_synced()) { applyBacklightPct(settings.dimDayPct); return; }

  time_t rawNow;
  time(&rawNow);
  struct tm utc;
  gmtime_r(&rawNow, &utc);
  double nowUTC = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;

  double sunrise = sunEventUTC(utc.tm_yday + 1, RECEIVER_LAT, RECEIVER_LON, true);
  double sunset  = sunEventUTC(utc.tm_yday + 1, RECEIVER_LAT, RECEIVER_LON, false);

  bool night;
  if (isnan(sunrise) || isnan(sunset)) {
    // Sun never sets or never rises today at this latitude -- rather than
    // guess, default to full brightness.
    night = false;
  } else if (sunset > sunrise) {
    night = (nowUTC >= sunset) || (nowUTC < sunrise);
  } else {
    night = (nowUTC >= sunset) && (nowUTC < sunrise);
  }

  applyBacklightPct(night ? settings.dimNightPct : settings.dimDayPct);
}
