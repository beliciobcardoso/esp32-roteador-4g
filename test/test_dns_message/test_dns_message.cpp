#include <unity.h>

#include <cstring>

#include "domain/dns_message.h"

// Este arquivo trata datagrama vindo da rede, entao todo teste aqui e sobre entrada
// hostil: cabecalho curto, label que promete mais bytes do que o pacote tem, ponteiro de
// compressao onde ele nao pode estar. O forwarder roda numa task com buffer fixo — um
// passo alem do fim do buffer aqui vira leitura de memoria alheia na placa.

namespace {

// Query minima: header de 12 bytes + "a.bc" + QTYPE A + QCLASS IN.
// id = 0x1234, flags = 0x0100 (RD ligado), QDCOUNT = 1.
const uint8_t kQuery[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 'a',  0x02, 'b',  'c',  0x00, 0x00, 0x01, 0x00, 0x01};
const size_t kQueryLength = sizeof(kQuery);

}  // namespace

void test_transaction_id_comes_from_the_first_two_bytes() {
  uint16_t id = 0;
  TEST_ASSERT_TRUE(dnsReadTransactionId(kQuery, kQueryLength, id));
  TEST_ASSERT_EQUAL_HEX16(0x1234, id);
}

void test_datagram_shorter_than_the_header_has_no_transaction_id() {
  const uint8_t truncated[] = {0x12, 0x34, 0x01, 0x00};
  uint16_t id = 0;
  TEST_ASSERT_FALSE(dnsReadTransactionId(truncated, sizeof(truncated), id));
}

void test_a_query_is_recognized() {
  TEST_ASSERT_TRUE(dnsIsQuery(kQuery, kQueryLength));
}

// QR ligado significa resposta. Encaminhar resposta como se fosse pergunta transforma o
// forwarder em refletor de trafego para quem mandar o pacote certo.
void test_a_response_is_not_a_query() {
  uint8_t response[kQueryLength];
  std::memcpy(response, kQuery, kQueryLength);
  response[2] |= 0x80;
  TEST_ASSERT_FALSE(dnsIsQuery(response, kQueryLength));
}

void test_a_datagram_without_question_is_not_a_query() {
  uint8_t empty[kQueryLength];
  std::memcpy(empty, kQuery, kQueryLength);
  empty[4] = 0x00;
  empty[5] = 0x00;
  TEST_ASSERT_FALSE(dnsIsQuery(empty, kQueryLength));
}

void test_servfail_keeps_the_id_and_the_question() {
  uint8_t out[64] = {};
  size_t length = dnsBuildServfail(kQuery, kQueryLength, out, sizeof(out));

  TEST_ASSERT_EQUAL_UINT(kQueryLength, length);
  TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
  TEST_ASSERT_EQUAL_MEMORY(kQuery + 12, out + 12, kQueryLength - 12);
}

void test_servfail_answers_with_qr_set_and_rcode_two() {
  uint8_t out[64] = {};
  TEST_ASSERT_TRUE(dnsBuildServfail(kQuery, kQueryLength, out, sizeof(out)) > 0);

  TEST_ASSERT_EQUAL_HEX8(0x80, out[2] & 0x80);  // QR = resposta
  TEST_ASSERT_EQUAL_HEX8(0x01, out[2] & 0x01);  // RD preservado do cliente
  TEST_ASSERT_EQUAL_HEX8(0x02, out[3] & 0x0F);  // RCODE = SERVFAIL
}

// Sem contagem zerada o cliente le ANCOUNT do pedido e procura resposta que nao existe.
void test_servfail_reports_no_records() {
  uint8_t out[64] = {};
  TEST_ASSERT_TRUE(dnsBuildServfail(kQuery, kQueryLength, out, sizeof(out)) > 0);

  TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[7]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[8]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[9]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[10]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[11]);
  TEST_ASSERT_EQUAL_HEX8(0x01, out[5]);  // QDCOUNT continua 1
}

// O label diz 9 bytes mas o datagrama acaba antes. Copiar "ate o fim da pergunta" sem
// conferir isso le fora do buffer.
void test_question_that_runs_past_the_datagram_is_refused() {
  const uint8_t malformed[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x09, 'a',  'b',  'c'};
  uint8_t out[64] = {};
  TEST_ASSERT_EQUAL_UINT(0, dnsBuildServfail(malformed, sizeof(malformed), out, sizeof(out)));
}

// Ponteiro de compressao (0xC0) nao tem o que comprimir numa pergunta: nao ha nome
// anterior no datagrama. Aceitar abre caminho para ponteiro que aponta para si mesmo.
void test_compression_pointer_in_the_question_is_refused() {
  const uint8_t compressed[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0xC0, 0x0C, 0x00, 0x01,
                                0x00, 0x01};
  uint8_t out[64] = {};
  TEST_ASSERT_EQUAL_UINT(0, dnsBuildServfail(compressed, sizeof(compressed), out, sizeof(out)));
}

void test_servfail_refuses_to_overflow_the_output_buffer() {
  uint8_t out[16] = {};
  TEST_ASSERT_EQUAL_UINT(0, dnsBuildServfail(kQuery, kQueryLength, out, sizeof(out)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_transaction_id_comes_from_the_first_two_bytes);
  RUN_TEST(test_datagram_shorter_than_the_header_has_no_transaction_id);
  RUN_TEST(test_a_query_is_recognized);
  RUN_TEST(test_a_response_is_not_a_query);
  RUN_TEST(test_a_datagram_without_question_is_not_a_query);
  RUN_TEST(test_servfail_keeps_the_id_and_the_question);
  RUN_TEST(test_servfail_answers_with_qr_set_and_rcode_two);
  RUN_TEST(test_servfail_reports_no_records);
  RUN_TEST(test_question_that_runs_past_the_datagram_is_refused);
  RUN_TEST(test_compression_pointer_in_the_question_is_refused);
  RUN_TEST(test_servfail_refuses_to_overflow_the_output_buffer);
  return UNITY_END();
}
