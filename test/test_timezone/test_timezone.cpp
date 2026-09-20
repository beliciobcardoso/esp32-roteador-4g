#include <unity.h>

#include <cstring>

#include "domain/timezone.h"

namespace {

// Mesma checagem que o adaptador faz ao montar o <select>: percorrer a tabela.
bool tableContains(const char* posix) {
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    if (std::strcmp(timezoneOptions()[i].posix, posix) == 0) return true;
  }
  return false;
}

}  // namespace

void test_the_table_is_not_empty() {
  TEST_ASSERT_TRUE(timezoneOptionCount() > 0);
  TEST_ASSERT_NOT_NULL(timezoneOptions());
}

// O default e o unico valor que o firmware grava sozinho, no provisionamento e no degrau
// de migracao. Se ele sair da tabela, validate() reprova a propria configuracao de
// fabrica e o AP nao sobe — sem AP nao ha pagina pra corrigir.
void test_the_default_is_one_of_the_options() {
  TEST_ASSERT_TRUE_MESSAGE(tableContains(kDefaultTimezone),
                           "kDefaultTimezone nao esta na tabela de fusos");
}

void test_every_option_is_accepted() {
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    TEST_ASSERT_TRUE(isKnownTimezone(String(timezoneOptions()[i].posix)));
  }
}

void test_every_option_has_a_label() {
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    const char* label = timezoneOptions()[i].label;
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_TRUE(std::strlen(label) > 0);
  }
}

// Duas entradas com o mesmo POSIX fariam o <select> mostrar opcoes indistinguiveis e a
// segunda nunca ser escolhida; dois rotulos iguais escondem qual fuso foi gravado.
void test_no_option_repeats() {
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    for (size_t j = i + 1; j < timezoneOptionCount(); ++j) {
      TEST_ASSERT_TRUE(std::strcmp(timezoneOptions()[i].posix, timezoneOptions()[j].posix) != 0);
      TEST_ASSERT_TRUE(std::strcmp(timezoneOptions()[i].label, timezoneOptions()[j].label) != 0);
    }
  }
}

void test_an_unknown_string_is_rejected() {
  TEST_ASSERT_FALSE(isKnownTimezone(String("America/Sao_Paulo")));
  TEST_ASSERT_FALSE(isKnownTimezone(String("UTC-3")));
}

// O <select> e dica de UI, nao garantia: um POST direto manda o que quiser, e
// setenv("TZ", lixo) nao falha — so produz hora errada em silencio.
void test_an_empty_string_is_rejected() {
  TEST_ASSERT_FALSE(isKnownTimezone(String("")));
}

// O formulario envia o valor, nunca o rotulo. Aceitar rotulo faria o firmware gravar
// "Brasilia (UTC-3)" na NVS e entregar isso ao setenv().
void test_a_label_is_not_a_timezone() {
  TEST_ASSERT_FALSE(isKnownTimezone(String(timezoneOptions()[0].label)));
}

void test_the_label_of_a_known_timezone_is_returned() {
  const char* label = timezoneLabel(String(kDefaultTimezone));
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_TRUE(std::strlen(label) > 0);
}

void test_an_unknown_timezone_has_no_label() {
  TEST_ASSERT_NULL(timezoneLabel(String("America/Sao_Paulo")));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_table_is_not_empty);
  RUN_TEST(test_the_default_is_one_of_the_options);
  RUN_TEST(test_every_option_is_accepted);
  RUN_TEST(test_every_option_has_a_label);
  RUN_TEST(test_no_option_repeats);
  RUN_TEST(test_an_unknown_string_is_rejected);
  RUN_TEST(test_an_empty_string_is_rejected);
  RUN_TEST(test_a_label_is_not_a_timezone);
  RUN_TEST(test_the_label_of_a_known_timezone_is_returned);
  RUN_TEST(test_an_unknown_timezone_has_no_label);
  return UNITY_END();
}
