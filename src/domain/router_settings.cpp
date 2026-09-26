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

// Maximo de um nome DNS completo (RFC 1035). Um IPv4 cabe folgado.
const size_t kMaxMqttHostLength = 253;
const uint32_t kMaxTcpPort = 65535;
// Teto das credenciais do broker. Nao vem de norma: limita o que vai para a NVS e para o
// CONNECT, e 64 sobra para qualquer usuario e senha gerados por gerenciador.
const size_t kMaxMqttCredentialLength = 64;
// Faixa do intervalo. Piso de 30 s: o dobro do dado da cadencia padrao, e abaixo disso o
// custo passa de ~50 MB/mes por unidade (PRD 14, "Custo de dado"). Teto de 1 h: acima disso
// o painel deixa de responder "como esta agora", que e o primeiro objetivo da fase.
const uint32_t kMinTelemetryIntervalS = 30;
const uint32_t kMaxTelemetryIntervalS = 3600;

// So letra, digito, ponto e hifen — o alfabeto de um nome DNS e de um IPv4. Recusa esquema,
// porta, espaco e barra, que produziriam uma URI malformada ou escrita pelo formulario.
bool isHostnameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '.' || c == '-';
}

bool isWellFormedHost(const String& host) {
  if (host.length() > kMaxMqttHostLength) return false;
  for (size_t i = 0; i < host.length(); ++i) {
    if (!isHostnameChar(host[i])) return false;
  }
  return true;
}

// As regras da telemetria. O formato e checado sempre, a presenca so com a chave ligada:
// desligada, broker em branco e o estado normal de uma unidade nova, mas lixo gravado
// apareceria como erro so no dia em que alguem ligasse a chave.
SettingsValidationError validateTelemetry(const RouterSettings& settings) {
  if (!isWellFormedHost(settings.mqtt_host)) return SettingsValidationError::MqttHostInvalid;
  if (settings.mqtt_port == 0 || settings.mqtt_port > kMaxTcpPort) {
    return SettingsValidationError::MqttPortOutOfRange;
  }
  if (settings.mqtt_user.length() > kMaxMqttCredentialLength) {
    return SettingsValidationError::MqttUserTooLong;
  }
  if (settings.mqtt_password.length() > kMaxMqttCredentialLength) {
    return SettingsValidationError::MqttPasswordTooLong;
  }
  if (settings.telemetry_interval_s < kMinTelemetryIntervalS ||
      settings.telemetry_interval_s > kMaxTelemetryIntervalS) {
    return SettingsValidationError::TelemetryIntervalOutOfRange;
  }

  if (!settings.telemetry_enabled) return SettingsValidationError::None;
  if (settings.mqtt_host.length() == 0) return SettingsValidationError::EmptyMqttHost;
  if (settings.mqtt_user.length() == 0) return SettingsValidationError::EmptyMqttUser;
  if (settings.mqtt_password.length() < kMinPasswordLength) {
    return SettingsValidationError::MqttPasswordTooShort;
  }
  return SettingsValidationError::None;
}
}  // namespace

const uint32_t kDefaultMqttPort = 443;
const uint32_t kDefaultTelemetryIntervalS = 60;

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
  return validateTelemetry(settings);
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
    case SettingsValidationError::EmptyMqttHost: return "com a telemetria ligada, o host do broker não pode ser vazio";
    case SettingsValidationError::MqttHostInvalid: return "host do broker precisa ser um nome ou IP, sem mqtts:// e sem porta";
    case SettingsValidationError::MqttPortOutOfRange: return "porta do broker precisa ficar entre 1 e 65535";
    case SettingsValidationError::EmptyMqttUser: return "com a telemetria ligada, o usuário do broker não pode ser vazio";
    case SettingsValidationError::MqttUserTooLong: return "usuário do broker não pode passar de 64 caracteres";
    case SettingsValidationError::MqttPasswordTooShort: return "com a telemetria ligada, a senha do broker precisa ter no mínimo 8 caracteres";
    case SettingsValidationError::MqttPasswordTooLong: return "senha do broker não pode passar de 64 caracteres";
    case SettingsValidationError::TelemetryIntervalOutOfRange: return "intervalo da telemetria precisa ficar entre 30 e 3600 segundos";
  }
  return "erro desconhecido";
}

bool adminPasswordChangeStillRequired(const RouterSettings& current,
                                      const RouterSettings& updated) {
  return current.admin_password_pending && updated.admin_password == current.admin_password;
}
