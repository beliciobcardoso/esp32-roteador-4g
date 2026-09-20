#include <unity.h>

#include "domain/router_settings.h"
#include "domain/settings_migration.h"
#include "domain/timezone.h"

namespace {

// Unity compara inteiros; SchemaVerdict e enum class e nao converte sozinho.
int code(SchemaVerdict verdict) {
  return static_cast<int>(verdict);
}

MigrationDefaults defaults() {
  MigrationDefaults values;
  values.apn = "zap.vivo.com.br";
  values.apn_user = "vivo";
  values.apn_password = "vivo";
  values.battery_divider_ratio = 2.19f;
  return values;
}

// Como um registro do schema 1 chega ao dominio: o adaptador le as chaves que existem e
// as que nao existem caem no default vazio — no schema 1 nao havia credencial de APN.
RouterSettings schema1Record() {
  RouterSettings settings;
  settings.wifi_ssid = "unidade-07";
  settings.wifi_password = "senha-da-unidade";
  settings.apn = "internet";
  settings.apn_user = "";
  settings.apn_password = "";
  settings.admin_user = "admin";
  settings.admin_password = "senha-do-admin";
  return settings;
}

// Como um registro do schema 2 chega ao dominio: tem as credenciais do APN, e os campos
// que o schema 3 acrescentou caem nos defaults vazios do struct.
RouterSettings schema2Record() {
  RouterSettings settings = schema1Record();
  settings.apn = "apn.escolhido.pela.pessoa";
  settings.apn_user = "usuario";
  settings.apn_password = "senha-do-apn";
  settings.timezone = "";
  settings.battery_divider_ratio = 0.0f;
  return settings;
}

void test_the_current_schema_needs_no_migration() {
  RouterSettings settings = schema1Record();
  settings.apn = "outra.operadora";

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Current),
                        code(migrateSettings(kCurrentSchema, defaults(), settings)));
  TEST_ASSERT_EQUAL_STRING("outra.operadora", settings.apn.c_str());
}

void test_schema_one_is_migrated_instead_of_discarded() {
  RouterSettings settings = schema1Record();

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Migrated),
                        code(migrateSettings(1, defaults(), settings)));
}

void test_migrating_keeps_the_credentials_of_the_unit() {
  RouterSettings settings = schema1Record();
  migrateSettings(1, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING("unidade-07", settings.wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("senha-da-unidade", settings.wifi_password.c_str());
  TEST_ASSERT_EQUAL_STRING("admin", settings.admin_user.c_str());
  TEST_ASSERT_EQUAL_STRING("senha-do-admin", settings.admin_password.c_str());
}

void test_the_unusable_legacy_apn_is_replaced_by_the_current_default() {
  RouterSettings settings = schema1Record();
  migrateSettings(1, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING("zap.vivo.com.br", settings.apn.c_str());
  TEST_ASSERT_EQUAL_STRING("vivo", settings.apn_user.c_str());
  TEST_ASSERT_EQUAL_STRING("vivo", settings.apn_password.c_str());
}

void test_an_apn_the_operator_chose_survives_the_migration() {
  RouterSettings settings = schema1Record();
  settings.apn = "apn.escolhido.pela.pessoa";
  migrateSettings(1, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING("apn.escolhido.pela.pessoa", settings.apn.c_str());
  // Schema 1 nao guardava credencial de APN, entao nao ha o que preservar — e repor o
  // usuario e a senha da Vivo num APN de outra operadora seria inventar dado.
  TEST_ASSERT_EQUAL_STRING("", settings.apn_user.c_str());
  TEST_ASSERT_EQUAL_STRING("", settings.apn_password.c_str());
}

void test_migrating_does_not_make_the_admin_password_pending() {
  RouterSettings settings = schema1Record();
  migrateSettings(1, defaults(), settings);

  // A senha de admin do registro antigo e a que a pessoa escolheu, nao uma sorteada.
  TEST_ASSERT_FALSE(settings.admin_password_pending);
}

void test_schema_zero_is_rejected() {
  RouterSettings settings = schema1Record();

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Rejected),
                        code(migrateSettings(0, defaults(), settings)));
}

void test_a_negative_schema_is_rejected() {
  RouterSettings settings = schema1Record();

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Rejected),
                        code(migrateSettings(-1, defaults(), settings)));
}

void test_a_rejected_record_is_left_untouched() {
  RouterSettings settings = schema1Record();
  migrateSettings(0, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING("unidade-07", settings.wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("internet", settings.apn.c_str());
}

void test_a_record_from_a_newer_firmware_is_read_as_it_is() {
  RouterSettings settings = schema1Record();
  settings.apn = "outra.operadora";

  // Downgrade de firmware: o registro tem campos que esta versao nao conhece. Ler o que
  // se entende mantem a unidade no ar; recusar faria o provisionamento sortear senha nova
  // e derrubar todo mundo que esta associado.
  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Current),
                        code(migrateSettings(kCurrentSchema + 1, defaults(), settings)));
  TEST_ASSERT_EQUAL_STRING("outra.operadora", settings.apn.c_str());
}

// --- Degrau 2 -> 3: fuso horario e divisor da bateria ---

void test_schema_two_is_migrated() {
  RouterSettings settings = schema2Record();

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Migrated),
                        code(migrateSettings(2, defaults(), settings)));
}

void test_the_step_to_three_fills_the_timezone_with_the_default() {
  RouterSettings settings = schema2Record();
  migrateSettings(2, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING(kDefaultTimezone, settings.timezone.c_str());
}

void test_the_step_to_three_fills_the_divider_ratio_with_the_default() {
  RouterSettings settings = schema2Record();
  migrateSettings(2, defaults(), settings);

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.19f, settings.battery_divider_ratio);
}

// O degrau novo nao pode desfazer o que o registro do schema 2 ja tinha — era exatamente
// esse o defeito que a migracao existe para corrigir.
void test_the_step_to_three_keeps_everything_the_record_already_had() {
  RouterSettings settings = schema2Record();
  migrateSettings(2, defaults(), settings);

  TEST_ASSERT_EQUAL_STRING("unidade-07", settings.wifi_ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("senha-da-unidade", settings.wifi_password.c_str());
  TEST_ASSERT_EQUAL_STRING("apn.escolhido.pela.pessoa", settings.apn.c_str());
  TEST_ASSERT_EQUAL_STRING("usuario", settings.apn_user.c_str());
  TEST_ASSERT_EQUAL_STRING("senha-do-apn", settings.apn_password.c_str());
  TEST_ASSERT_EQUAL_STRING("senha-do-admin", settings.admin_password.c_str());
}

// Unidade que nunca foi atualizada desde o schema 1 pula direto para o 3. Os degraus tem
// que encadear: parar no 2 deixaria o fuso vazio, que o validate() reprova.
void test_a_schema_one_record_goes_all_the_way_to_the_current_format() {
  RouterSettings settings = schema1Record();

  TEST_ASSERT_EQUAL_INT(code(SchemaVerdict::Migrated),
                        code(migrateSettings(1, defaults(), settings)));
  TEST_ASSERT_EQUAL_STRING("zap.vivo.com.br", settings.apn.c_str());
  TEST_ASSERT_EQUAL_STRING(kDefaultTimezone, settings.timezone.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.19f, settings.battery_divider_ratio);
}

// Migrar duas vezes tem que dar o mesmo resultado: o registro nao e regravado, entao o
// degrau roda de novo a cada boot ate a primeira gravacao pela pagina.
void test_migrating_twice_changes_nothing() {
  RouterSettings once = schema1Record();
  migrateSettings(1, defaults(), once);

  RouterSettings twice = once;
  migrateSettings(1, defaults(), twice);

  TEST_ASSERT_EQUAL_STRING(once.apn.c_str(), twice.apn.c_str());
  TEST_ASSERT_EQUAL_STRING(once.timezone.c_str(), twice.timezone.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, once.battery_divider_ratio, twice.battery_divider_ratio);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_current_schema_needs_no_migration);
  RUN_TEST(test_schema_one_is_migrated_instead_of_discarded);
  RUN_TEST(test_migrating_keeps_the_credentials_of_the_unit);
  RUN_TEST(test_the_unusable_legacy_apn_is_replaced_by_the_current_default);
  RUN_TEST(test_an_apn_the_operator_chose_survives_the_migration);
  RUN_TEST(test_migrating_does_not_make_the_admin_password_pending);
  RUN_TEST(test_schema_zero_is_rejected);
  RUN_TEST(test_a_negative_schema_is_rejected);
  RUN_TEST(test_a_rejected_record_is_left_untouched);
  RUN_TEST(test_a_record_from_a_newer_firmware_is_read_as_it_is);
  RUN_TEST(test_schema_two_is_migrated);
  RUN_TEST(test_the_step_to_three_fills_the_timezone_with_the_default);
  RUN_TEST(test_the_step_to_three_fills_the_divider_ratio_with_the_default);
  RUN_TEST(test_the_step_to_three_keeps_everything_the_record_already_had);
  RUN_TEST(test_a_schema_one_record_goes_all_the_way_to_the_current_format);
  RUN_TEST(test_migrating_twice_changes_nothing);
  return UNITY_END();
}
