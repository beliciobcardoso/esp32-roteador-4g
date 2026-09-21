#include <unity.h>

#include "domain/json.h"

// O que estes testes protegem nao e a redacao do JSON, e a promessa de que o corpo sempre
// carrega: JSON invalido nao degrada, ele derruba o JSON.parse() e a pagina fica em branco
// sem dizer por que. Por isso o peso esta no escape, e nao nos getters.

namespace {

String escaped(const char* raw) { return escapeForJsonString(String(raw)); }

}  // namespace

void test_plain_text_passes_through_untouched() {
  TEST_ASSERT_EQUAL_STRING("esp32-roteador-4g", escaped("esp32-roteador-4g").c_str());
}

void test_quote_and_backslash_are_escaped() {
  TEST_ASSERT_EQUAL_STRING("a\\\"b", escaped("a\"b").c_str());
  TEST_ASSERT_EQUAL_STRING("a\\\\b", escaped("a\\b").c_str());
}

void test_common_control_characters_use_their_short_form() {
  TEST_ASSERT_EQUAL_STRING("a\\nb", escaped("a\nb").c_str());
  TEST_ASSERT_EQUAL_STRING("a\\tb", escaped("a\tb").c_str());
  TEST_ASSERT_EQUAL_STRING("a\\rb", escaped("a\rb").c_str());
}

void test_other_control_characters_become_unicode_escapes() {
  String value;
  value += 'a';
  value += static_cast<char>(0x01);
  TEST_ASSERT_EQUAL_STRING("a\\u0001", escapeForJsonString(value).c_str());
}

// O caractere que o replace de placeholder obrigava a escapar no HTML nao tem significado
// nenhum em JSON. Este teste existe para que a gambiarra nao volte por imitacao.
void test_braces_are_not_special() {
  TEST_ASSERT_EQUAL_STRING("{{APN}}", escaped("{{APN}}").c_str());
}

void test_valid_utf8_survives_byte_for_byte() {
  TEST_ASSERT_EQUAL_STRING("Configuração", escaped("Configuração").c_str());
  TEST_ASSERT_EQUAL_STRING("日本", escaped("日本").c_str());
}

// SSID em 802.11 e sequencia de bytes arbitraria. Um byte que nao forma UTF-8 nao pode
// atravessar: o corpo inteiro seria recusado pelo navegador.
void test_invalid_utf8_byte_becomes_the_replacement_character() {
  String value;
  value += 'a';
  value += static_cast<char>(0xFF);
  value += 'b';
  TEST_ASSERT_EQUAL_STRING("a\\ufffdb", escapeForJsonString(value).c_str());
}

void test_truncated_sequence_does_not_eat_the_next_character() {
  // 0xC3 anuncia dois bytes e vem sozinho; o 'b' seguinte e texto legitimo e tem que sair.
  String value;
  value += static_cast<char>(0xC3);
  value += 'b';
  TEST_ASSERT_EQUAL_STRING("\\ufffdb", escapeForJsonString(value).c_str());
}

void test_overlong_encoding_is_rejected() {
  // 0xC0 0xAF e a codificacao longa de '/', classica de bypass de filtro.
  String value;
  value += static_cast<char>(0xC0);
  value += static_cast<char>(0xAF);
  TEST_ASSERT_EQUAL_STRING("\\ufffd\\ufffd", escapeForJsonString(value).c_str());
}

void test_surrogate_range_is_rejected() {
  // ED A0 80 e U+D800, que so existe em UTF-16 e e invalido em UTF-8.
  String value;
  value += static_cast<char>(0xED);
  value += static_cast<char>(0xA0);
  value += static_cast<char>(0x80);
  TEST_ASSERT_EQUAL_STRING("\\ufffd\\ufffd\\ufffd", escapeForJsonString(value).c_str());
}

void test_empty_object_is_still_valid_json() {
  JsonObject object;
  TEST_ASSERT_EQUAL_STRING("{}", object.finish().c_str());
}

void test_fields_are_separated_by_commas() {
  JsonObject object;
  object.text("ssid", String("casa")).boolean("online", true).number("failures", 3u);
  TEST_ASSERT_EQUAL_STRING("{\"ssid\":\"casa\",\"online\":true,\"failures\":3}",
                           object.finish().c_str());
}

void test_float_uses_the_requested_precision() {
  JsonObject object;
  object.number("volts", 3.87654f, 2);
  TEST_ASSERT_EQUAL_STRING("{\"volts\":3.88}", object.finish().c_str());
}

void test_raw_field_is_not_escaped() {
  JsonObject object;
  object.raw("clock", "null");
  TEST_ASSERT_EQUAL_STRING("{\"clock\":null}", object.finish().c_str());
}

void test_a_hostile_value_cannot_break_out_of_its_field() {
  JsonObject object;
  object.text("ssid", String("\",\"admin_password\":\"vazou"));
  TEST_ASSERT_EQUAL_STRING(
      "{\"ssid\":\"\\\",\\\"admin_password\\\":\\\"vazou\"}", object.finish().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plain_text_passes_through_untouched);
  RUN_TEST(test_quote_and_backslash_are_escaped);
  RUN_TEST(test_common_control_characters_use_their_short_form);
  RUN_TEST(test_other_control_characters_become_unicode_escapes);
  RUN_TEST(test_braces_are_not_special);
  RUN_TEST(test_valid_utf8_survives_byte_for_byte);
  RUN_TEST(test_invalid_utf8_byte_becomes_the_replacement_character);
  RUN_TEST(test_truncated_sequence_does_not_eat_the_next_character);
  RUN_TEST(test_overlong_encoding_is_rejected);
  RUN_TEST(test_surrogate_range_is_rejected);
  RUN_TEST(test_empty_object_is_still_valid_json);
  RUN_TEST(test_fields_are_separated_by_commas);
  RUN_TEST(test_float_uses_the_requested_precision);
  RUN_TEST(test_raw_field_is_not_escaped);
  RUN_TEST(test_a_hostile_value_cannot_break_out_of_its_field);
  return UNITY_END();
}
