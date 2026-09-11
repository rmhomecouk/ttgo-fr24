#include "settings.h"
#include <Preferences.h>
#include "config.h"

Settings settings;

static Preferences prefs;
static const char *NS = "squawk";

void settings_reset_defaults() {
  settings.feedSilenceMs   = FEED_SILENCE_MS;
  settings.feedReconnectMs = FEED_RECONNECT_MS;
  settings.aircraftStaleMs = AIRCRAFT_STALE_MS;

  settings.autoCycle    = AUTO_CYCLE;
  settings.pageDwellMs  = PAGE_DWELL_MS;
  settings.manualHoldMs = MANUAL_HOLD_MS;

  settings.priorityEnable  = PRIORITY_ENABLE;
  settings.priorityRangeNm = PRIORITY_RANGE_NM;
  settings.priorityHystNm  = PRIORITY_HYST_NM;

  settings.scopeRangeNm = SCOPE_RANGE_NM;

  settings.dimEnable   = DIM_ENABLE;
  settings.dimDayPct   = DIM_DAY_PCT;
  settings.dimNightPct = DIM_NIGHT_PCT;
}

void settings_load() {
  settings_reset_defaults();   // fills anything a fresh NVS namespace is missing

  if (!prefs.begin(NS, /*readOnly=*/true)) {
    // First boot: the "squawk" namespace doesn't exist in NVS yet. Persist
    // the defaults now rather than reading them back (and logging the same
    // "not found") on every boot until the panel is used once.
    settings_save();
    return;
  }
  settings.feedSilenceMs   = prefs.getULong("feedSilMs",   settings.feedSilenceMs);
  settings.feedReconnectMs = prefs.getULong("feedReconMs", settings.feedReconnectMs);
  settings.aircraftStaleMs = prefs.getULong("acStaleMs",   settings.aircraftStaleMs);

  settings.autoCycle    = prefs.getBool("autoCycle", settings.autoCycle);
  settings.pageDwellMs  = prefs.getULong("dwellMs",  settings.pageDwellMs);
  settings.manualHoldMs = prefs.getULong("holdMs",   settings.manualHoldMs);

  settings.priorityEnable  = prefs.getBool("priEnable",   settings.priorityEnable);
  settings.priorityRangeNm = prefs.getFloat("priRangeNm", settings.priorityRangeNm);
  settings.priorityHystNm  = prefs.getFloat("priHystNm",  settings.priorityHystNm);

  settings.scopeRangeNm = prefs.getUShort("scopeNm", settings.scopeRangeNm);

  settings.dimEnable   = prefs.getBool("dimEnable",   settings.dimEnable);
  settings.dimDayPct   = prefs.getUChar("dimDayPct",  settings.dimDayPct);
  settings.dimNightPct = prefs.getUChar("dimNightPct",settings.dimNightPct);
  prefs.end();
}

void settings_save() {
  prefs.begin(NS, /*readOnly=*/false);
  prefs.putULong("feedSilMs",   settings.feedSilenceMs);
  prefs.putULong("feedReconMs", settings.feedReconnectMs);
  prefs.putULong("acStaleMs",   settings.aircraftStaleMs);

  prefs.putBool("autoCycle", settings.autoCycle);
  prefs.putULong("dwellMs",  settings.pageDwellMs);
  prefs.putULong("holdMs",   settings.manualHoldMs);

  prefs.putBool("priEnable",   settings.priorityEnable);
  prefs.putFloat("priRangeNm", settings.priorityRangeNm);
  prefs.putFloat("priHystNm",  settings.priorityHystNm);

  prefs.putUShort("scopeNm", settings.scopeRangeNm);

  prefs.putBool("dimEnable",    settings.dimEnable);
  prefs.putUChar("dimDayPct",   settings.dimDayPct);
  prefs.putUChar("dimNightPct", settings.dimNightPct);
  prefs.end();
}
