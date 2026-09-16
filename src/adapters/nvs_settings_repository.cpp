#include "nvs_settings_repository.h"

#include <Preferences.h>

#include "../../include/config.h"

namespace {
const char* kKeyConfigured = "configured";
const char* kKeySsid = "ssid";
const char* kKeyWifiPass = "wifi_pass";
const char* kKeyApn = "apn";
const char* kKeyAdminUser = "admin_user";
const char* kKeyAdminPass = "admin_pass";
}  // namespace

bool NvsSettingsRepository::load(RouterSettings& out) {
  Preferences prefs;
  prefs.begin(SETTINGS_NVS_NAMESPACE, /*readOnly=*/true);

  bool configured = prefs.getBool(kKeyConfigured, false);
  if (!configured) {
    prefs.end();
    return false;
  }

  out.wifi_ssid = prefs.getString(kKeySsid, "");
  out.wifi_password = prefs.getString(kKeyWifiPass, "");
  out.apn = prefs.getString(kKeyApn, "");
  out.admin_user = prefs.getString(kKeyAdminUser, "");
  out.admin_password = prefs.getString(kKeyAdminPass, "");

  prefs.end();
  return true;
}

bool NvsSettingsRepository::save(const RouterSettings& settings) {
  Preferences prefs;
  prefs.begin(SETTINGS_NVS_NAMESPACE, /*readOnly=*/false);

  prefs.putString(kKeySsid, settings.wifi_ssid);
  prefs.putString(kKeyWifiPass, settings.wifi_password);
  prefs.putString(kKeyApn, settings.apn);
  prefs.putString(kKeyAdminUser, settings.admin_user);
  prefs.putString(kKeyAdminPass, settings.admin_password);
  prefs.putBool(kKeyConfigured, true);

  prefs.end();
  return true;
}
