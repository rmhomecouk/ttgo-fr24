/*
 * settings.h — runtime copies of the config.h tunables, persisted in flash
 * (NVS via Preferences) so the HTTP control panel can change them without a
 * reflash. config.h remains the fallback used on first boot and on "reset to
 * defaults" -- it is not read again after that.
 */
#ifndef SQUAWK_SETTINGS_H
#define SQUAWK_SETTINGS_H

#include <stdint.h>

struct Settings {
  uint32_t feedSilenceMs;
  uint32_t feedReconnectMs;
  uint32_t aircraftStaleMs;

  bool     autoCycle;
  uint32_t pageDwellMs;
  uint32_t manualHoldMs;

  bool     priorityEnable;
  float    priorityRangeNm;
  float    priorityHystNm;

  uint16_t scopeRangeNm;

  bool     dimEnable;
  uint8_t  dimDayPct;
  uint8_t  dimNightPct;
};

extern Settings settings;

/* Populate `settings` from NVS, falling back to config.h for anything not
 * yet stored (first boot, or a field added since). Call once from setup(). */
void settings_load();

/* Persist the current `settings` to NVS. */
void settings_save();

/* Reset `settings` (in RAM only) to the config.h defaults. Caller decides
 * whether/when to settings_save() afterwards. */
void settings_reset_defaults();

#endif /* SQUAWK_SETTINGS_H */
