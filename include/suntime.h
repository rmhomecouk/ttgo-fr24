/*
 * suntime.h — NTP time sync and sunrise/sunset backlight dimming.
 */
#ifndef SQUAWK_SUNTIME_H
#define SQUAWK_SUNTIME_H

#include <stdint.h>
#include <stdbool.h>

/* Attaches the backlight PWM channel and sets full brightness. Call once
 * from setup(), in place of the old pinMode/digitalWrite backlight lines. */
void suntime_init();

/* Starts NTP on first call, then re-evaluates sunrise/sunset against the
 * antenna position and adjusts the backlight, at most once every 30s. Safe
 * to call every loop() pass. */
void suntime_update();

/* Current backlight level, 0-100. For the stats panel. */
uint8_t suntime_backlight_pct();

/* Whether the system clock has synced over NTP yet. Until it has, the
 * backlight sits at DIM_DAY_PCT because sunrise/sunset can't be computed. */
bool suntime_time_synced();

#endif /* SQUAWK_SUNTIME_H */
