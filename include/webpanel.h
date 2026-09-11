/*
 * webpanel.h — HTTP control panel: view/edit settings.h, poll live stats.
 *
 * Plain WebServer (already linked in via the WiFi framework) rather than
 * ESPAsyncWebServer, and the stats panel polls a JSON endpoint every couple
 * of seconds instead of pushing over a websocket -- indistinguishable at
 * this data rate, and it adds no library weight on a board that is already
 * at 80% flash.
 */
#ifndef SQUAWK_WEBPANEL_H
#define SQUAWK_WEBPANEL_H

/* Starts the HTTP server. Call once from setup(), after WiFi.begin(). */
void webpanel_begin();

/* Services pending HTTP requests. Call every loop() pass. */
void webpanel_handle();

#endif /* SQUAWK_WEBPANEL_H */
