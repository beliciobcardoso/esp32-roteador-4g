#include <unity.h>

#include <cstring>

#include "domain/secret.h"

// A senha gerada aqui e lida de log serial ou de etiqueta e digitada a mao por uma pessoa.
// Por isso os testes cobrem o alfabeto e a uniformidade, nao so "gera alguma coisa":
// alfabeto ambiguo custa viagem ate o equipamento, e vies no mapeamento custa entropia
// sem que nada no comportamento denuncie.

namespace {

bool isInAlphabet(char c) {
  return std::strchr(kSecretAlphabet, c) != nullptr && c != '\0';
}

}  // namespace

void test_secret_has_one_character_per_byte() {
  const uint8_t bytes[] = {0, 1, 2, 3, 4};
  TEST_ASSERT_EQUAL_UINT(5, secretFromBytes(bytes, 5).length());
}

void test_no_bytes_produces_empty_secret() {
  const uint8_t bytes[] = {7};
  TEST_ASSERT_EQUAL_UINT(0, secretFromBytes(bytes, 0).length());
}

void test_every_possible_byte_maps_into_the_alphabet() {
  uint8_t all[256];
  for (int i = 0; i < 256; ++i) all[i] = static_cast<uint8_t>(i);

  String secret = secretFromBytes(all, 256);
  TEST_ASSERT_EQUAL_UINT(256, secret.length());
  for (size_t i = 0; i < secret.length(); ++i) {
    TEST_ASSERT_TRUE_MESSAGE(isInAlphabet(secret[i]), "caractere fora do alfabeto");
  }
}

// 256 bytes distribuidos em 32 simbolos: cada um tem que aparecer exatamente 8 vezes.
// Qualquer alfabeto cujo tamanho nao divida 256 viesa o mapeamento — o teste amarra a
// escolha do tamanho, que e o que evita o vies.
void test_mapping_of_all_bytes_is_uniform() {
  uint8_t all[256];
  for (int i = 0; i < 256; ++i) all[i] = static_cast<uint8_t>(i);

  String secret = secretFromBytes(all, 256);
  for (const char* symbol = kSecretAlphabet; *symbol != '\0'; ++symbol) {
    int occurrences = 0;
    for (size_t i = 0; i < secret.length(); ++i) {
      if (secret[i] == *symbol) ++occurrences;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, occurrences, "simbolo com frequencia desigual");
  }
}

void test_alphabet_has_no_ambiguous_digits() {
  // 0/O, 1/I, 5/S e 8/B sao os pares que erram na transcricao a mao. A escolha foi manter
  // a letra e descartar o digito, entao a leitura nunca fica em duvida nesses quatro.
  TEST_ASSERT_NULL(std::strchr(kSecretAlphabet, '0'));
  TEST_ASSERT_NULL(std::strchr(kSecretAlphabet, '1'));
  TEST_ASSERT_NULL(std::strchr(kSecretAlphabet, '5'));
  TEST_ASSERT_NULL(std::strchr(kSecretAlphabet, '8'));
  TEST_ASSERT_NOT_NULL(std::strchr(kSecretAlphabet, 'O'));
  TEST_ASSERT_NOT_NULL(std::strchr(kSecretAlphabet, 'I'));
  TEST_ASSERT_NOT_NULL(std::strchr(kSecretAlphabet, 'S'));
  TEST_ASSERT_NOT_NULL(std::strchr(kSecretAlphabet, 'B'));
}

void test_alphabet_has_no_lowercase() {
  for (const char* symbol = kSecretAlphabet; *symbol != '\0'; ++symbol) {
    TEST_ASSERT_FALSE_MESSAGE(*symbol >= 'a' && *symbol <= 'z', "minuscula no alfabeto");
  }
}

void test_same_bytes_produce_the_same_secret() {
  const uint8_t bytes[] = {200, 13, 42, 99, 7, 255};
  TEST_ASSERT_TRUE(secretFromBytes(bytes, 6) == secretFromBytes(bytes, 6));
}

void test_different_bytes_produce_different_secrets() {
  const uint8_t one[] = {1, 2, 3, 4};
  const uint8_t other[] = {1, 2, 3, 5};
  TEST_ASSERT_FALSE(secretFromBytes(one, 4) == secretFromBytes(other, 4));
}

// O comprimento default precisa caber na passphrase WPA2 (8 a 63) e passar no minimo que
// o validate() exige para a senha de admin. Se alguem baixar o valor, isto quebra.
void test_default_length_is_a_valid_password_length() {
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(8, kSecretLength);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(63, kSecretLength);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_secret_has_one_character_per_byte);
  RUN_TEST(test_no_bytes_produces_empty_secret);
  RUN_TEST(test_every_possible_byte_maps_into_the_alphabet);
  RUN_TEST(test_mapping_of_all_bytes_is_uniform);
  RUN_TEST(test_alphabet_has_no_ambiguous_digits);
  RUN_TEST(test_alphabet_has_no_lowercase);
  RUN_TEST(test_same_bytes_produce_the_same_secret);
  RUN_TEST(test_different_bytes_produce_different_secrets);
  RUN_TEST(test_default_length_is_a_valid_password_length);
  return UNITY_END();
}
