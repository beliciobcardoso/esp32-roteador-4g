#include <unity.h>

#include <cstring>

#include "domain/router_settings.h"

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
  };

  for (SettingsValidationError error : all) {
    const char* message = to_string(error);
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_TRUE_MESSAGE(std::strcmp(message, "erro desconhecido") != 0,
                             "codigo do enum sem mensagem propria em to_string()");
  }
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
  RUN_TEST(test_every_error_code_has_its_own_message);
  return UNITY_END();
}
