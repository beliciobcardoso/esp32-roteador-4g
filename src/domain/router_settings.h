#pragma once

// O dominio precisa de String, e na placa String e a do core Arduino. Fora dela o
// Arduino.h nao existe, e sem este desvio a camada "pura" nao compila no host — que era
// justamente o que impedia testar validate() sem hardware (debito 19).
//
// std::string no lugar, e nao um shim escrito a mao: validate() so chama length(), e nos
// dois tipos length() conta bytes do buffer. Para esta regra os dois sao equivalentes.
// O que o teste nativo NAO cobre e qualquer comportamento em que os tipos divirjam
// (conversao implicita, semantica de copia em falta de memoria) — nada disso aparece aqui,
// mas aparece se o dominio crescer.
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

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
