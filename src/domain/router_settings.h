#pragma once

#include <Arduino.h>

// ENTIDADE — regras puras, sem dependencia de hardware/framework de rede/storage.
struct RouterSettings {
  String wifi_ssid;
  String wifi_password;
  String apn;
  String admin_user;
  String admin_password;
};

enum class SettingsValidationError {
  None,
  EmptySsid,
  WifiPasswordTooShort,
  EmptyApn,
  EmptyAdminUser,
  AdminPasswordTooShort,
};

SettingsValidationError validate(const RouterSettings& settings);
const char* to_string(SettingsValidationError error);
