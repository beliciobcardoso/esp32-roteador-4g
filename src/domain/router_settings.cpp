#include "router_settings.h"

namespace {
const int kMinPasswordLength = 8;
}

SettingsValidationError validate(const RouterSettings& settings) {
  if (settings.wifi_ssid.length() == 0) return SettingsValidationError::EmptySsid;
  if (settings.wifi_password.length() < kMinPasswordLength) return SettingsValidationError::WifiPasswordTooShort;
  if (settings.apn.length() == 0) return SettingsValidationError::EmptyApn;
  if (settings.admin_user.length() == 0) return SettingsValidationError::EmptyAdminUser;
  if (settings.admin_password.length() < kMinPasswordLength) return SettingsValidationError::AdminPasswordTooShort;
  return SettingsValidationError::None;
}

const char* to_string(SettingsValidationError error) {
  switch (error) {
    case SettingsValidationError::None: return "ok";
    case SettingsValidationError::EmptySsid: return "SSID nao pode ser vazio";
    case SettingsValidationError::WifiPasswordTooShort: return "senha WiFi precisa ter no minimo 8 caracteres";
    case SettingsValidationError::EmptyApn: return "APN nao pode ser vazio";
    case SettingsValidationError::EmptyAdminUser: return "usuario admin nao pode ser vazio";
    case SettingsValidationError::AdminPasswordTooShort: return "senha admin precisa ter no minimo 8 caracteres";
  }
  return "erro desconhecido";
}
