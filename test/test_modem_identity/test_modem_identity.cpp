#include <unity.h>

#include "domain/modem_identity.h"

namespace {

bool mentions(const String& text, const char* fragment) {
  return text.find(fragment) != String::npos;
}

// Resposta no formato do manual AT do A76xx, com o eco que o esp_modem entrega junto: o
// callback do componente recebe o buffer acumulado desde o comando, nao so a parte nova.
// Modelo e firmware sao os lidos da placa da bancada em 24/09/2026; o IMEI e ficticio.
const char* kFaseResponse =
    "AT+SIMCOMATI\r\r\n"
    "Manufacturer: SIMCOM INCORPORATED\r\n"
    "Model: A7670E-FASE\r\n"
    "Revision: A110B01A7670M7_F\r\n"
    "IMEI: 000000000000000\r\n"
    "\r\n"
    "OK\r\n";

}  // namespace

// --- leitura da resposta ---

void test_the_model_is_read_from_its_line() {
  TEST_ASSERT_EQUAL_STRING("A7670E-FASE", parseSimcomati(kFaseResponse).model.c_str());
}

void test_the_revision_is_read_from_its_line() {
  TEST_ASSERT_EQUAL_STRING("A110B01A7670M7_F", parseSimcomati(kFaseResponse).revision.c_str());
}

void test_surrounding_blanks_are_not_part_of_the_value() {
  ModemIdentity identity = parseSimcomati("Model:   A7670E-LASE  \r\nRevision:\tX1 \r\n");
  TEST_ASSERT_EQUAL_STRING("A7670E-LASE", identity.model.c_str());
  TEST_ASSERT_EQUAL_STRING("X1", identity.revision.c_str());
}

void test_a_last_line_without_a_break_is_still_read() {
  TEST_ASSERT_EQUAL_STRING("A7670E-FASE", parseSimcomati("Model: A7670E-FASE").model.c_str());
}

void test_a_missing_field_stays_empty_instead_of_invented() {
  ModemIdentity identity = parseSimcomati("AT+SIMCOMATI\r\r\nERROR\r\n");
  TEST_ASSERT_EQUAL_UINT32(0, identity.model.length());
  TEST_ASSERT_EQUAL_UINT32(0, identity.revision.length());
}

void test_the_imei_is_not_kept_anywhere() {
  // O IMEI vem na mesma resposta. Nao e hardware, e nao tem por que sair daqui.
  ModemIdentity identity = parseSimcomati(kFaseResponse);
  TEST_ASSERT_FALSE(mentions(identity.model, "000000000000000"));
  TEST_ASSERT_FALSE(mentions(identity.revision, "000000000000000"));
  TEST_ASSERT_FALSE(mentions(describeModem(identity), "000000000000000"));
}

// --- GNSS pela variante ---

void test_fase_variants_have_gnss() {
  TEST_ASSERT_TRUE(modemHasGnss("A7670E-FASE"));
  TEST_ASSERT_TRUE(modemHasGnss("A7670SA-FASE"));
}

void test_other_variants_do_not() {
  TEST_ASSERT_FALSE(modemHasGnss("A7670E-LASE"));
  TEST_ASSERT_FALSE(modemHasGnss("A7670E-LNXY-UBL"));
  TEST_ASSERT_FALSE(modemHasGnss("A7670SA-LASC"));
  TEST_ASSERT_FALSE(modemHasGnss("A7670G-LLSE"));
}

void test_a_bare_a7670e_is_not_assumed_to_have_gnss() {
  // E o que a etiqueta desta bancada diz. Sem o sufixo nao da para afirmar nada.
  TEST_ASSERT_FALSE(modemHasGnss("A7670E"));
}

void test_an_unknown_model_does_not_claim_gnss() {
  TEST_ASSERT_FALSE(modemHasGnss(""));
}

// --- linha do serial ---

void test_the_line_names_model_firmware_and_gnss() {
  String text = describeModem(parseSimcomati(kFaseResponse));
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "A7670E-FASE"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "A110B01A7670M7_F"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "GNSS interno: sim"), text.c_str());
}

void test_a_model_without_gnss_says_so() {
  ModemIdentity identity;
  identity.model = "A7670E-LASE";
  String text = describeModem(identity);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "GNSS interno: nao"), text.c_str());
}

void test_an_unread_model_is_reported_as_unread_not_as_no_gnss() {
  // "nao identificado" e "sem GNSS" levam a decisoes diferentes no PRD 15: um pede nova
  // leitura, o outro muda o caminho da fase. A linha nao pode confundir os dois.
  String text = describeModem(ModemIdentity());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "nao identificado"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "GNSS interno"), text.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_model_is_read_from_its_line);
  RUN_TEST(test_the_revision_is_read_from_its_line);
  RUN_TEST(test_surrounding_blanks_are_not_part_of_the_value);
  RUN_TEST(test_a_last_line_without_a_break_is_still_read);
  RUN_TEST(test_a_missing_field_stays_empty_instead_of_invented);
  RUN_TEST(test_the_imei_is_not_kept_anywhere);
  RUN_TEST(test_fase_variants_have_gnss);
  RUN_TEST(test_other_variants_do_not);
  RUN_TEST(test_a_bare_a7670e_is_not_assumed_to_have_gnss);
  RUN_TEST(test_an_unknown_model_does_not_claim_gnss);
  RUN_TEST(test_the_line_names_model_firmware_and_gnss);
  RUN_TEST(test_a_model_without_gnss_says_so);
  RUN_TEST(test_an_unread_model_is_reported_as_unread_not_as_no_gnss);
  return UNITY_END();
}
