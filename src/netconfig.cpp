#include "netconfig.h"
#include <Preferences.h>
#include "secrets.h"

NetConfig netConfig;

static Preferences prefs;
static const char *NS = "netcfg";

void netconfig_load() {
  netConfig.hostname = "squawk";
  netConfig.ssid = WIFI_SSID;
  netConfig.pass = WIFI_PASS;

  if (!prefs.begin(NS, /*readOnly=*/true)) {
    // First boot: the "netcfg" namespace doesn't exist in NVS yet. Persist
    // the secrets.h defaults now rather than logging "not found" on every
    // boot until the network section is saved once.
    netconfig_save();
    return;
  }
  netConfig.hostname = prefs.getString("hostname", netConfig.hostname);
  netConfig.ssid     = prefs.getString("ssid",     netConfig.ssid);
  netConfig.pass     = prefs.getString("pass",     netConfig.pass);
  prefs.end();
}

void netconfig_save() {
  prefs.begin(NS, /*readOnly=*/false);
  prefs.putString("hostname", netConfig.hostname);
  prefs.putString("ssid",     netConfig.ssid);
  prefs.putString("pass",     netConfig.pass);
  prefs.end();
}
