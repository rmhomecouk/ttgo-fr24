/*
 * config.h — behaviour tunables.
 *
 * Everything here is safe to commit. Credentials and the antenna position
 * live in secrets.h, which is not.
 */
#ifndef SQUAWK_CONFIG_H
#define SQUAWK_CONFIG_H

/* ---------------------------------------------------------------- feed --- */

/* A TCP session to the receiver can sit "connected" indefinitely after the
 * far end has gone away: the socket only reports failure when you write to
 * it, and this device never writes. Silence is the only symptom available,
 * so treat a long enough gap as a dead link and reconnect.
 *
 * Note the trade-off: dump1090 sends nothing at all when it is tracking no
 * aircraft, so on a genuinely quiet night this will reconnect on a cycle.
 * That is harmless but chatty -- raise it if it bothers you. */
#define FEED_SILENCE_MS      30000UL

/* Minimum gap between connection attempts, so a receiver that is down does
 * not get hammered. */
#define FEED_RECONNECT_MS     5000UL

/* Drop an aircraft that has not been heard from for this long. */
#define AIRCRAFT_STALE_MS    60000UL

/* -------------------------------------------------------------- paging --- */

/* Cycle through the pages on a timer. Set to 0 for manual paging only. */
#define AUTO_CYCLE                1
#define PAGE_DWELL_MS        10000UL

/* Pressing a button hands control back to you: automatic paging — both the
 * cycle and the proximity override — stands down for this long afterwards,
 * so the page cannot be yanked away while you are reading it. */
#define MANUAL_HOLD_MS       30000UL

/* ------------------------------------------------------ nearest priority - */

/* When a contact comes inside PRIORITY_RANGE_NM, pin the display to the
 * Nearest page until it leaves again. */
#define PRIORITY_ENABLE           1
#define PRIORITY_RANGE_NM      5.0f

/* Release the pin only once the contact is this much further out again.
 * Without the dead band an aircraft loitering on the boundary flaps the
 * page back and forth. */
#define PRIORITY_HYST_NM       0.5f

/* ------------------------------------------------------------- display --- */

/* Outer range ring on the radar page, nautical miles. Drop to 20 if most of
 * what you see is close traffic -- at 40 everything inside 5nm plots within
 * a few pixels of the centre. */
#define SCOPE_RANGE_NM           40

/* Dim the backlight after dark. The T-Display's panel is plenty bright for a
 * dark room, and full brightness overnight is glare rather than information.
 * Sunrise/sunset are computed from the antenna position in secrets.h against
 * NTP time; until the clock has synced over WiFi, DIM_DAY_PCT is used. */
#define DIM_ENABLE                 1
#define DIM_DAY_PCT              100
#define DIM_NIGHT_PCT              50

/* ---------------------------------------------------- everything above is -
 * also editable at runtime from the HTTP control panel at http://<device
 * ip>/, which persists changes to flash (NVS) and survives reflashing. These
 * #defines are only the fallback used the first time the device boots, or
 * after "reset to defaults" in the panel. See settings.h. */

#endif /* SQUAWK_CONFIG_H */
