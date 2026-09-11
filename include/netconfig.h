/*
 * netconfig.h — WiFi identity (hostname/SSID/password), persisted separately
 * from settings.h so that "reset to defaults" on the behaviour tunables can
 * never silently knock the device off its network.
 */
#ifndef SQUAWK_NETCONFIG_H
#define SQUAWK_NETCONFIG_H

#include <Arduino.h>

struct NetConfig {
  String hostname;
  String ssid;
  String pass;
};

extern NetConfig netConfig;

/* Populate `netConfig` from NVS, falling back to secrets.h (WIFI_SSID,
 * WIFI_PASS) and the hostname "squawk" on first boot. Call once from
 * setup(), before WiFi.begin(). */
void netconfig_load();

/* Persist the current `netConfig` to NVS. Does not itself reconnect --
 * changing SSID/hostname needs a restart to take effect cleanly, which is
 * the caller's decision. */
void netconfig_save();

#endif /* SQUAWK_NETCONFIG_H */
