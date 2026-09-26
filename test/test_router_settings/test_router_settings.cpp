#include <unity.h>

#include <cstring>

#include "domain/router_settings.h"
#include "domain/timezone.h"

namespace {

// Unity compara inteiros; SettingsValidationError e enum class e nao converte sozinho.
int code(SettingsValidationError error) {
  return static_cast<int>(error);
}

// Cadeia de `size` caracteres. Usa o construtor (n, char) do std::string: no host String
// e std::string (ver router_settings.h), e este arquivo so compila no env native.
String repeat(size_t size) {
  return String(size, 'a');
}

// Base valida: cada teste parte daqui e estraga um campo so, pra garantir que o erro
// observado veio do campo mexido e nao de outro que ja estava invalido.
RouterSettings validSettings() {
  RouterSettings settings;
  settings.wifi_ssid = "esp32-roteador-4g";
  settings.wifi_password = "roteador4g";
  settings.apn = "zap.vivo.com.br";
  settings.apn_user = "vivo";
  settings.apn_password = "vivo";
  settings.admin_user = "admin";
  settings.admin_password = "admin1234";
  settings.timezone = kDefaultTimezone;
  settings.battery_divider_ratio = 2.19f;
  return settings;
}

}  // namespace

void test_base_settings_are_valid() {
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(validSettings())));
}

void test_empty_ssid_is_rejected() {
  RouterSettings settings = validSettings();
  settings.wifi_ssid = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptySsid), code(validate(settings)));
}

void test_wifi_password_below_minimum_is_rejected() {
  RouterSettings settings = validSettings();
  settings.wifi_password = "1234567";  // 7
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::WifiPasswordTooShort), code(validate(settings)));
}

// O minimo do WPA2 e 8; o teste fixa a fronteira pra que mexer no limite quebre aqui.
void test_wifi_password_at_minimum_is_accepted() {
  RouterSettings settings = validSettings();
  settings.wifi_password = "12345678";  // 8
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// 802.11 limita SSID a 32 bytes. Acima disso o core Arduino nao reclama: copia 32 bytes
// sem terminador e manda ssid_len com o comprimento inteiro, config internamente
// inconsistente (debito 15). Barrar aqui e o que garante que o driver nunca ve isso.
void test_ssid_at_maximum_is_accepted() {
  RouterSettings settings = validSettings();
  settings.wifi_ssid = repeat(32);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_ssid_above_maximum_is_rejected() {
  RouterSettings settings = validSettings();
  settings.wifi_ssid = repeat(33);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::SsidTooLong), code(validate(settings)));
}

// Passphrase WPA2 vai ate 63 caracteres — 64 e o tamanho do buffer, o 64o byte e o NUL.
void test_wifi_password_at_maximum_is_accepted() {
  RouterSettings settings = validSettings();
  settings.wifi_password = repeat(63);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_wifi_password_above_maximum_is_rejected() {
  RouterSettings settings = validSettings();
  settings.wifi_password = repeat(64);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::WifiPasswordTooLong), code(validate(settings)));
}

void test_empty_apn_is_rejected() {
  RouterSettings settings = validSettings();
  settings.apn = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptyApn), code(validate(settings)));
}

void test_empty_admin_user_is_rejected() {
  RouterSettings settings = validSettings();
  settings.admin_user = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptyAdminUser), code(validate(settings)));
}

void test_admin_password_below_minimum_is_rejected() {
  RouterSettings settings = validSettings();
  settings.admin_password = "1234567";  // 7
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::AdminPasswordTooShort), code(validate(settings)));
}

void test_admin_password_at_minimum_is_accepted() {
  RouterSettings settings = validSettings();
  settings.admin_password = "12345678";  // 8
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// Credenciais de APN sao opcionais de proposito (router_settings.h): varias operadoras
// aceitam APN sem autenticacao. Exigir os campos quebraria esses casos, entao o teste
// trava a decisao — se alguem adicionar um EmptyApnUser, quebra aqui e tem que justificar.
void test_empty_apn_credentials_are_accepted() {
  RouterSettings settings = validSettings();
  settings.apn_user = "";
  settings.apn_password = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// A ordem importa pro usuario: com dois campos ruins ele so ve o primeiro erro, corrige,
// reenvia e ve o segundo. Fixar a ordem evita que uma reordenacao acidental mude a
// mensagem sem ninguem perceber.
void test_ssid_error_wins_over_later_fields() {
  RouterSettings settings = validSettings();
  settings.wifi_ssid = "";
  settings.apn = "";
  settings.admin_user = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptySsid), code(validate(settings)));
}

// Nenhum codigo pode cair no fallback "erro desconhecido": a pagina de config mostra este
// texto cru pro usuario, entao um enum novo sem mensagem vira erro mudo na tela.
// --- Fuso horario ---

void test_a_known_timezone_is_accepted() {
  RouterSettings settings = validSettings();
  settings.timezone = timezoneOptions()[timezoneOptionCount() - 1].posix;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// Fora da tabela e recusado aqui, e nao no adaptador, porque o <select> nao protege nada:
// um POST direto manda a string que quiser, e setenv("TZ", lixo) nao devolve erro — so
// produz hora errada em silencio, que e o tipo de defeito que aparece semanas depois.
void test_a_timezone_outside_the_table_is_rejected() {
  RouterSettings settings = validSettings();
  settings.timezone = "America/Sao_Paulo";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::UnknownTimezone), code(validate(settings)));
}

void test_an_empty_timezone_is_rejected() {
  RouterSettings settings = validSettings();
  settings.timezone = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::UnknownTimezone), code(validate(settings)));
}

// --- Divisor da bateria ---

// O piso nao e estetico. ratio = Vbateria / Vpino, entao abaixo de 4.4/3.3 = 1.33 uma
// bateria cheia entrega mais que a referencia do ADC: a leitura satura e a placa reporta
// tensao MENOR justamente quando esta carregada. 1.4 arredonda isso pra cima.
void test_a_divider_ratio_below_the_floor_is_rejected() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = 1.39f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::BatteryDividerOutOfRange),
                        code(validate(settings)));
}

void test_a_divider_ratio_at_the_floor_is_accepted() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = 1.4f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_a_divider_ratio_above_the_ceiling_is_rejected() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = 10.01f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::BatteryDividerOutOfRange),
                        code(validate(settings)));
}

void test_a_divider_ratio_at_the_ceiling_is_accepted() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = 10.0f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// Campo nao preenchido chega como 0.0 pelo toFloat() do adaptador. Sem o piso isso viraria
// uma bateria lida como 0 V, ou seja 0%, sem nenhum erro no caminho.
void test_a_zero_divider_ratio_is_rejected() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = 0.0f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::BatteryDividerOutOfRange),
                        code(validate(settings)));
}

void test_a_negative_divider_ratio_is_rejected() {
  RouterSettings settings = validSettings();
  settings.battery_divider_ratio = -2.19f;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::BatteryDividerOutOfRange),
                        code(validate(settings)));
}

void test_every_error_code_has_its_own_message() {
  const SettingsValidationError all[] = {
      SettingsValidationError::None,
      SettingsValidationError::EmptySsid,
      SettingsValidationError::SsidTooLong,
      SettingsValidationError::WifiPasswordTooShort,
      SettingsValidationError::WifiPasswordTooLong,
      SettingsValidationError::EmptyApn,
      SettingsValidationError::EmptyAdminUser,
      SettingsValidationError::AdminPasswordTooShort,
      SettingsValidationError::AdminPasswordMustChange,
      SettingsValidationError::UnknownTimezone,
      SettingsValidationError::BatteryDividerOutOfRange,
      SettingsValidationError::EmptyMqttHost,
      SettingsValidationError::MqttHostInvalid,
      SettingsValidationError::MqttPortOutOfRange,
      SettingsValidationError::EmptyMqttUser,
      SettingsValidationError::MqttUserTooLong,
      SettingsValidationError::MqttPasswordTooShort,
      SettingsValidationError::MqttPasswordTooLong,
      SettingsValidationError::TelemetryIntervalOutOfRange,
  };

  for (SettingsValidationError error : all) {
    const char* message = to_string(error);
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_TRUE_MESSAGE(std::strcmp(message, "erro desconhecido") != 0,
                             "codigo do enum sem mensagem propria em to_string()");
  }
}

// --- Telemetria MQTT (PRD 14) ---------------------------------------------------------

namespace {

// Base com a telemetria ligada e completa. Os testes de telemetria partem daqui pelo mesmo
// motivo do validSettings(): estragar um campo so.
RouterSettings telemetryOn() {
  RouterSettings settings = validSettings();
  settings.telemetry_enabled = true;
  settings.mqtt_host = "broker.exemplo.com.br";
  settings.mqtt_user = "unidade-01";
  settings.mqtt_password = "senha-do-broker";
  return settings;
}

}  // namespace

// Default de fabrica desligado: unidade gravada sem configurar nao pode ficar tentando
// conectar num host que nao existe, gastando dado movel (PRD 14, "Configuracao nova").
void test_telemetry_defaults_are_off_on_port_443_every_minute() {
  RouterSettings settings;
  TEST_ASSERT_FALSE(settings.telemetry_enabled);
  TEST_ASSERT_EQUAL_UINT32(kDefaultMqttPort, settings.mqtt_port);
  TEST_ASSERT_EQUAL_UINT32(443, kDefaultMqttPort);
  TEST_ASSERT_EQUAL_UINT32(kDefaultTelemetryIntervalS, settings.telemetry_interval_s);
  TEST_ASSERT_EQUAL_UINT32(60, kDefaultTelemetryIntervalS);
}

// Desligada, broker em branco e o estado normal de uma unidade nova — nao e erro.
void test_disabled_telemetry_needs_no_broker() {
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(validSettings())));
}

void test_complete_enabled_telemetry_is_accepted() {
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(telemetryOn())));
}

void test_enabled_telemetry_without_host_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptyMqttHost), code(validate(settings)));
}

// O host vira parte de uma URI montada pelo firmware (`mqtts://<host>:<porta>`). Esquema ou
// porta dentro dele produziriam `mqtts://mqtts://…` ou duas portas — e aceitar qualquer
// caractere seria deixar o formulario escrever a URI.
void test_host_with_a_scheme_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = "mqtts://broker.exemplo.com.br";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttHostInvalid), code(validate(settings)));
}

void test_host_with_a_port_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = "broker.exemplo.com.br:443";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttHostInvalid), code(validate(settings)));
}

// Lixo no host e recusado mesmo com a telemetria desligada: senao ele fica gravado e so
// aparece como erro no dia em que alguem ligar a chave.
void test_malformed_host_is_rejected_even_when_disabled() {
  RouterSettings settings = validSettings();
  settings.mqtt_host = "broker exemplo";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttHostInvalid), code(validate(settings)));
}

void test_an_ipv4_host_is_accepted() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = "203.0.113.10";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

// 253 e o maximo de um nome DNS completo.
void test_host_at_the_dns_limit_is_accepted() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = repeat(253);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_host_above_the_dns_limit_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_host = repeat(254);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttHostInvalid), code(validate(settings)));
}

// Porta zero e o que o toInt() devolve para campo vazio ou nao numerico.
void test_port_zero_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_port = 0;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttPortOutOfRange), code(validate(settings)));
}

void test_port_above_the_tcp_range_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_port = 65536;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttPortOutOfRange), code(validate(settings)));
}

void test_port_at_the_top_of_the_tcp_range_is_accepted() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_port = 65535;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_enabled_telemetry_without_user_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_user = "";
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::EmptyMqttUser), code(validate(settings)));
}

void test_user_above_the_limit_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_user = repeat(65);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttUserTooLong), code(validate(settings)));
}

void test_enabled_telemetry_with_short_password_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_password = repeat(7);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttPasswordTooShort),
                        code(validate(settings)));
}

void test_password_above_the_limit_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.mqtt_password = repeat(65);
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::MqttPasswordTooLong),
                        code(validate(settings)));
}

// 30 s de piso: o dobro do dado da cadencia padrao, e abaixo disso o custo por unidade
// passa de ~50 MB/mes (PRD 14, "Custo de dado"). 3600 s de teto: mais que isso e o painel
// deixa de responder "como esta agora", que e o primeiro objetivo da fase.
void test_interval_below_the_floor_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.telemetry_interval_s = 29;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::TelemetryIntervalOutOfRange),
                        code(validate(settings)));
}

void test_interval_at_the_floor_is_accepted() {
  RouterSettings settings = telemetryOn();
  settings.telemetry_interval_s = 30;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_interval_at_the_ceiling_is_accepted() {
  RouterSettings settings = telemetryOn();
  settings.telemetry_interval_s = 3600;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::None), code(validate(settings)));
}

void test_interval_above_the_ceiling_is_rejected() {
  RouterSettings settings = telemetryOn();
  settings.telemetry_interval_s = 3601;
  TEST_ASSERT_EQUAL_INT(code(SettingsValidationError::TelemetryIntervalOutOfRange),
                        code(validate(settings)));
}

// A pendencia de troca nao passa por validate(): validate() ve uma configuracao sozinha e
// esta regra precisa das duas. Os testes abaixo cobrem a funcao que faz a comparacao.

void test_pending_admin_password_blocks_a_save_that_keeps_it() {
  RouterSettings current = validSettings();
  current.admin_password = "XQKM479BTWPD";
  current.admin_password_pending = true;

  RouterSettings updated = current;
  updated.wifi_ssid = "outro-ssid";
  updated.admin_password_pending = false;

  TEST_ASSERT_TRUE(adminPasswordChangeStillRequired(current, updated));
}

void test_changing_the_admin_password_settles_the_pendency() {
  RouterSettings current = validSettings();
  current.admin_password = "XQKM479BTWPD";
  current.admin_password_pending = true;

  RouterSettings updated = current;
  updated.admin_password = "senha-escolhida";

  TEST_ASSERT_FALSE(adminPasswordChangeStillRequired(current, updated));
}

// Sem pendencia a regra some: quem ja trocou pode salvar o SSID sem redigitar a senha, que
// e justamente o que o campo em branco do formulario existe para permitir.
void test_without_pendency_keeping_the_same_password_is_allowed() {
  RouterSettings current = validSettings();
  RouterSettings updated = current;
  updated.wifi_ssid = "outro-ssid";

  TEST_ASSERT_FALSE(adminPasswordChangeStillRequired(current, updated));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_base_settings_are_valid);
  RUN_TEST(test_empty_ssid_is_rejected);
  RUN_TEST(test_wifi_password_below_minimum_is_rejected);
  RUN_TEST(test_wifi_password_at_minimum_is_accepted);
  RUN_TEST(test_ssid_at_maximum_is_accepted);
  RUN_TEST(test_ssid_above_maximum_is_rejected);
  RUN_TEST(test_wifi_password_at_maximum_is_accepted);
  RUN_TEST(test_wifi_password_above_maximum_is_rejected);
  RUN_TEST(test_empty_apn_is_rejected);
  RUN_TEST(test_empty_admin_user_is_rejected);
  RUN_TEST(test_admin_password_below_minimum_is_rejected);
  RUN_TEST(test_admin_password_at_minimum_is_accepted);
  RUN_TEST(test_empty_apn_credentials_are_accepted);
  RUN_TEST(test_ssid_error_wins_over_later_fields);
  RUN_TEST(test_a_known_timezone_is_accepted);
  RUN_TEST(test_a_timezone_outside_the_table_is_rejected);
  RUN_TEST(test_an_empty_timezone_is_rejected);
  RUN_TEST(test_a_divider_ratio_below_the_floor_is_rejected);
  RUN_TEST(test_a_divider_ratio_at_the_floor_is_accepted);
  RUN_TEST(test_a_divider_ratio_above_the_ceiling_is_rejected);
  RUN_TEST(test_a_divider_ratio_at_the_ceiling_is_accepted);
  RUN_TEST(test_a_zero_divider_ratio_is_rejected);
  RUN_TEST(test_a_negative_divider_ratio_is_rejected);
  RUN_TEST(test_every_error_code_has_its_own_message);
  RUN_TEST(test_telemetry_defaults_are_off_on_port_443_every_minute);
  RUN_TEST(test_disabled_telemetry_needs_no_broker);
  RUN_TEST(test_complete_enabled_telemetry_is_accepted);
  RUN_TEST(test_enabled_telemetry_without_host_is_rejected);
  RUN_TEST(test_host_with_a_scheme_is_rejected);
  RUN_TEST(test_host_with_a_port_is_rejected);
  RUN_TEST(test_malformed_host_is_rejected_even_when_disabled);
  RUN_TEST(test_an_ipv4_host_is_accepted);
  RUN_TEST(test_host_at_the_dns_limit_is_accepted);
  RUN_TEST(test_host_above_the_dns_limit_is_rejected);
  RUN_TEST(test_port_zero_is_rejected);
  RUN_TEST(test_port_above_the_tcp_range_is_rejected);
  RUN_TEST(test_port_at_the_top_of_the_tcp_range_is_accepted);
  RUN_TEST(test_enabled_telemetry_without_user_is_rejected);
  RUN_TEST(test_user_above_the_limit_is_rejected);
  RUN_TEST(test_enabled_telemetry_with_short_password_is_rejected);
  RUN_TEST(test_password_above_the_limit_is_rejected);
  RUN_TEST(test_interval_below_the_floor_is_rejected);
  RUN_TEST(test_interval_at_the_floor_is_accepted);
  RUN_TEST(test_interval_at_the_ceiling_is_accepted);
  RUN_TEST(test_interval_above_the_ceiling_is_rejected);
  RUN_TEST(test_pending_admin_password_blocks_a_save_that_keeps_it);
  RUN_TEST(test_changing_the_admin_password_settles_the_pendency);
  RUN_TEST(test_without_pendency_keeping_the_same_password_is_allowed);
  return UNITY_END();
}
