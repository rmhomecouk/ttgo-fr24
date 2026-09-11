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

/* Whether the sun-based schedule currently says it's night (regardless of
 * whether a wake override is forcing day brightness anyway). Used to decide
 * which of dimDayPct/dimNightPct a live brightness change should edit. */
bool suntime_is_night();

/* A button was pressed. If it's currently dark, force day brightness for
 * five minutes so the screen is readable, then let the schedule take back
 * over. Does nothing if dimming is off or it's already day. Call from
 * main.cpp's button handler. */
void suntime_wake();

/* Sets the backlight to `pct` immediately and writes it into whichever of
 * dimDayPct/dimNightPct is currently active, persisting the change -- the
 * control panel's live brightness slider. */
void suntime_set_live_pct(uint8_t pct);

#endif /* SQUAWK_SUNTIME_H */
