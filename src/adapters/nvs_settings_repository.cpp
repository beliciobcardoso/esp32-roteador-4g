#include "nvs_settings_repository.h"

#include <Preferences.h>

#include "../../include/config.h"

namespace {
const char* kKeyConfigured = "configured";
const char* kKeySsid = "ssid";
const char* kKeyWifiPass = "wifi_pass";
const char* kKeyApn = "apn";
const char* kKeyApnUser = "apn_user";
const char* kKeyApnPass = "apn_pass";
// Versao do formato gravado. O schema 1 nao tinha as credenciais do APN e guardava
// um APN default que nao existe na rede da Vivo; tratar esse registro como ausente
// e o que faz o dispositivo cair nos defaults novos em vez de insistir no antigo.
const char* kKeySchema = "schema";
const int kCurrentSchema = 2;
const char* kKeyAdminUser = "admin_user";
const char* kKeyAdminPass = "admin_pass";
}  // namespace

bool NvsSettingsRepository::load(RouterSettings& out) {
  Preferences prefs;
  prefs.begin(SETTINGS_NVS_NAMESPACE, /*readOnly=*/true);

  bool configured = prefs.getBool(kKeyConfigured, false);
  if (!configured || prefs.getInt(kKeySchema, 1) < kCurrentSchema) {
    prefs.end();
    return false;
  }

  out.wifi_ssid = prefs.getString(kKeySsid, "");
  out.wifi_password = prefs.getString(kKeyWifiPass, "");
  out.apn = prefs.getString(kKeyApn, "");
  out.apn_user = prefs.getString(kKeyApnUser, "");
  out.apn_password = prefs.getString(kKeyApnPass, "");
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
  prefs.putString(kKeyApnUser, settings.apn_user);
  prefs.putString(kKeyApnPass, settings.apn_password);
  prefs.putString(kKeyAdminUser, settings.admin_user);
  prefs.putString(kKeyAdminPass, settings.admin_password);
  prefs.putInt(kKeySchema, kCurrentSchema);
  prefs.putBool(kKeyConfigured, true);

  prefs.end();
  return true;
}
