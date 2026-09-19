#pragma once

#include "string_type.h"

// ENTIDADE — regras puras, sem dependencia de hardware/framework de rede/storage.
struct RouterSettings {
  String wifi_ssid;
  String wifi_password;
  String apn;
  // Credenciais PAP do APN. Opcionais de proposito: varias operadoras aceitam APN
  // sem autenticacao, e exigir os campos quebraria esses casos.
  String apn_user;
  String apn_password;
  String admin_user;
  String admin_password;
};

enum class SettingsValidationError {
  None,
  EmptySsid,
  SsidTooLong,
  WifiPasswordTooShort,
  WifiPasswordTooLong,
  EmptyApn,
  EmptyAdminUser,
  AdminPasswordTooShort,
};

SettingsValidationError validate(const RouterSettings& settings);
const char* to_string(SettingsValidationError error);
