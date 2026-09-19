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
  // True enquanto a senha de admin ainda e a sorteada no provisionamento do primeiro boot.
  // Nao e preferencia de UI: e o que faz a senha da etiqueta valer para um acesso so.
  bool admin_password_pending = false;
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
  // Nao sai de validate(): depende de comparar a gravacao com a config atual, e validate()
  // ve uma sozinha. Mora no mesmo enum porque a mensagem para o usuario sai do mesmo
  // to_string(), e duas fontes de texto de erro divergem com o tempo.
  AdminPasswordMustChange,
};

SettingsValidationError validate(const RouterSettings& settings);

// Regra da troca obrigatoria: enquanto a pendencia estiver de pe, gravar sem mexer na senha
// de admin e recusado. Recusar so o campo em branco nao bastaria — reenviar a mesma senha
// sorteada tambem mantem o segredo que foi impresso no serial.
bool adminPasswordChangeStillRequired(const RouterSettings& current,
                                      const RouterSettings& updated);
const char* to_string(SettingsValidationError error);
