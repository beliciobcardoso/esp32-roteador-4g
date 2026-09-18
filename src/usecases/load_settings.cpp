#include "load_settings.h"

#include "../../include/config.h"

RouterSettings LoadSettingsUseCase::execute() {
  RouterSettings settings;
  if (repository_.load(settings)) {
    return settings;
  }

  // Primeiro boot / NVS vazia — aplica defaults de fabrica.
  settings.wifi_ssid = DEFAULT_AP_SSID;
  settings.wifi_password = DEFAULT_AP_PASSWORD;
  settings.apn = DEFAULT_APN;
  settings.apn_user = DEFAULT_APN_USER;
  settings.apn_password = DEFAULT_APN_PASSWORD;
  settings.admin_user = DEFAULT_ADMIN_USER;
  settings.admin_password = DEFAULT_ADMIN_PASSWORD;
  return settings;
}
