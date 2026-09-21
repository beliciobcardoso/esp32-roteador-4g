#include "router_settings.h"

#include "timezone.h"

namespace {
const size_t kMinPasswordLength = 8;
// Limites do 802.11, nao escolha nossa. O core Arduino nao os impoe: softAP() so rejeita
// SSID vazio e senha de 1 a 7; acima do maximo ele copia 32/64 bytes para o
// wifi_ap_config_t e manda ssid_len com o comprimento inteiro, config internamente
// inconsistente entregue a um driver que e blob. Barrar antes de gravar na NVS e o que
// impede o caso ruim: SSID invalido persistido, placa reiniciada, AP nao sobe — e sem AP
// nao ha pagina de configuracao para desfazer (debito 15).
const size_t kMaxSsidLength = 32;
// 63 e o maximo da passphrase WPA2; o 64o byte do buffer e o terminador.
const size_t kMaxWifiPasswordLength = 63;

// Faixa do divisor da bateria. ratio = Vbateria / Vpino, entao o piso sai da fisica: uma
// LiPo 1S cheia chega a ~4.4 V e a referencia do ADC e 3.3 V, logo abaixo de 4.4/3.3 =
// 1.33 a leitura satura e a placa reporta tensao MENOR justamente quando esta carregada —
// falha silenciosa, porque o ADC nao avisa que grampeou. 1.4 arredonda isso pra cima.
//
// O teto e largo de proposito: nao existe divisor util perto dele (com 10 o pino ve 0.42 V
// e a resolucao vira ruido), e o que ele barra e dedo trocado — 21.9 no lugar de 2.19.
const float kMinBatteryDividerRatio = 1.4f;
const float kMaxBatteryDividerRatio = 10.0f;
}

SettingsValidationError validate(const RouterSettings& settings) {
  if (settings.wifi_ssid.length() == 0) return SettingsValidationError::EmptySsid;
  if (settings.wifi_ssid.length() > kMaxSsidLength) return SettingsValidationError::SsidTooLong;
  if (settings.wifi_password.length() < kMinPasswordLength) return SettingsValidationError::WifiPasswordTooShort;
  if (settings.wifi_password.length() > kMaxWifiPasswordLength) return SettingsValidationError::WifiPasswordTooLong;
  if (settings.apn.length() == 0) return SettingsValidationError::EmptyApn;
  if (settings.admin_user.length() == 0) return SettingsValidationError::EmptyAdminUser;
  if (settings.admin_password.length() < kMinPasswordLength) return SettingsValidationError::AdminPasswordTooShort;
  if (!isKnownTimezone(settings.timezone)) return SettingsValidationError::UnknownTimezone;
  if (settings.battery_divider_ratio < kMinBatteryDividerRatio ||
      settings.battery_divider_ratio > kMaxBatteryDividerRatio) {
    return SettingsValidationError::BatteryDividerOutOfRange;
  }
  return SettingsValidationError::None;
}

const char* to_string(SettingsValidationError error) {
  switch (error) {
    case SettingsValidationError::None: return "ok";
    case SettingsValidationError::EmptySsid: return "SSID não pode ser vazio";
    case SettingsValidationError::SsidTooLong: return "SSID não pode passar de 32 caracteres";
    case SettingsValidationError::WifiPasswordTooShort: return "senha Wi-Fi precisa ter no mínimo 8 caracteres";
    case SettingsValidationError::WifiPasswordTooLong: return "senha Wi-Fi não pode passar de 63 caracteres";
    case SettingsValidationError::EmptyApn: return "APN não pode ser vazio";
    case SettingsValidationError::EmptyAdminUser: return "usuário admin não pode ser vazio";
    case SettingsValidationError::AdminPasswordTooShort: return "senha admin precisa ter no mínimo 8 caracteres";
    case SettingsValidationError::AdminPasswordMustChange: return "troque a senha de admin sorteada no primeiro boot antes de salvar qualquer configuração";
    case SettingsValidationError::UnknownTimezone: return "fuso horário precisa ser um dos da lista";
    case SettingsValidationError::BatteryDividerOutOfRange: return "divisor da bateria precisa ficar entre 1.4 e 10.0";
  }
  return "erro desconhecido";
}

bool adminPasswordChangeStillRequired(const RouterSettings& current,
                                      const RouterSettings& updated) {
  return current.admin_password_pending && updated.admin_password == current.admin_password;
}
