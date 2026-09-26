#pragma once

#include <cstdint>

#include "string_type.h"

// Defaults da telemetria MQTT (PRD 14). Moram aqui, e nao so no inicializador do struct,
// porque a NVS le as mesmas chaves com default quando o registro e anterior a elas — duas
// redacoes do mesmo numero divergem na primeira vez que uma mudar.
//
// 8883 e a porta registrada do MQTT sobre TLS, e so TLS e aceito (PRD 14, "Seguranca").
// 60 s e a cadencia que o PRD dimensionou para o piloto (~20-25 MB/mes por unidade).
extern const uint32_t kDefaultMqttPort;
extern const uint32_t kDefaultTelemetryIntervalS;

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
  // String POSIX TZ, sempre uma das entradas de domain/timezone.h. Nasce vazia de
  // proposito: quem constroi a configuracao tem que escolher, e o validate() pega quem
  // esqueceu. Silenciar isso com um default aqui esconderia o campo nao preenchido.
  String timezone;
  // Ratio do divisor resistivo da bateria, calibrado por placa. Zero e invalido pelo
  // mesmo motivo: campo em branco no formulario chega como 0.0 pelo toFloat().
  float battery_divider_ratio = 0.0f;

  // Telemetria MQTT (PRD 14). Ao contrario do fuso e do divisor, estes campos TEM default
  // seguro, e e isso que os deixa entrar na NVS lidos com default, sem subir o schema: nasce
  // desligada, e desligada nao precisa de broker nenhum.
  bool telemetry_enabled = false;
  // Nome DNS ou IPv4, sem esquema e sem porta: o firmware monta a URI.
  String mqtt_host;
  // uint32_t e nao uint16_t: o formulario manda qualquer numero, e o validate() so pega
  // 65536 se o valor chegar ate ele sem ter dado a volta num cast.
  uint32_t mqtt_port = kDefaultMqttPort;
  String mqtt_user;
  // Digitada pelo operador, que a cadastrou no broker (PRD 14, "Seguranca", item 3). Nunca
  // sai da placa: nem no GET da pagina, nem no serial — criterio 7.
  String mqtt_password;
  uint32_t telemetry_interval_s = kDefaultTelemetryIntervalS;
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
  UnknownTimezone,
  BatteryDividerOutOfRange,
  EmptyMqttHost,
  MqttHostInvalid,
  MqttPortOutOfRange,
  EmptyMqttUser,
  MqttUserTooLong,
  MqttPasswordTooShort,
  MqttPasswordTooLong,
  TelemetryIntervalOutOfRange,
};

SettingsValidationError validate(const RouterSettings& settings);

// Regra da troca obrigatoria: enquanto a pendencia estiver de pe, gravar sem mexer na senha
// de admin e recusado. Recusar so o campo em branco nao bastaria — reenviar a mesma senha
// sorteada tambem mantem o segredo que foi impresso no serial.
bool adminPasswordChangeStillRequired(const RouterSettings& current,
                                      const RouterSettings& updated);
const char* to_string(SettingsValidationError error);
