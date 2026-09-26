#include <unity.h>

#include <cstring>

#include "domain/telemetry_buffer.h"

namespace {

TelemetryRing gRing;

TelemetrySample sampleWithTs(uint32_t ts) {
  TelemetrySample sample{};
  sample.ts = ts;
  sample.battery_mv = 3840;
  sample.uplink_state = encodeUplinkState(UplinkState::Online);
  return sample;
}

// Simula o que a RTC RAM entrega num power-on: bytes quaisquer, inclusive um cabecalho
// que por acaso tem numeros plausiveis.
void fillWithGarbage(TelemetryRing& ring, uint8_t pattern) {
  std::memset(&ring, pattern, sizeof(ring));
}

}  // namespace

void setUp() { ringReset(gRing); }

void test_reset_leaves_a_valid_and_empty_ring() {
  TEST_ASSERT_TRUE(ringHeaderIsValid(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
  TEST_ASSERT_EQUAL_UINT16(0, ringSize(gRing));
}

// ~4 KB e o orcamento. Passar disso nao da erro de compilacao — da erro em campo, quando a
// variavel nao couber na RTC slow RAM junto com o que ja mora la.
void test_ring_fits_the_rtc_budget() {
  TEST_ASSERT_TRUE_MESSAGE(sizeof(TelemetryRing) <= 4096, "anel passou de 4 KB de RTC");
}

void test_push_and_peek_return_the_same_sample() {
  ringPush(gRing, sampleWithTs(1000));
  TelemetrySample out{};
  TEST_ASSERT_TRUE(ringPeekOldest(gRing, out));
  TEST_ASSERT_EQUAL_UINT32(1000, out.ts);
  TEST_ASSERT_EQUAL_UINT16(1, ringSize(gRing));
}

void test_peek_on_an_empty_ring_reports_failure() {
  TelemetrySample out{};
  TEST_ASSERT_FALSE(ringPeekOldest(gRing, out));
  TEST_ASSERT_FALSE(ringPopOldest(gRing, out));
}

// Ordem cronologica na saida. Um anel que devolvesse fora de ordem produziria degrau falso
// no grafico, e grafico errado e pior que grafico ausente porque produz decisao.
void test_drains_in_chronological_order() {
  for (uint32_t ts = 1; ts <= 5; ++ts) ringPush(gRing, sampleWithTs(ts));

  for (uint32_t expected = 1; expected <= 5; ++expected) {
    TelemetrySample out{};
    TEST_ASSERT_TRUE(ringPopOldest(gRing, out));
    TEST_ASSERT_EQUAL_UINT32(expected, out.ts);
  }
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

// Peek nao pode consumir: a amostra so sai do anel depois que a publicacao foi aceita.
// Remover antes perderia o dado numa reconexao que falhou no meio.
void test_peek_does_not_consume() {
  ringPush(gRing, sampleWithTs(7));
  TelemetrySample first{};
  TelemetrySample second{};
  ringPeekOldest(gRing, first);
  ringPeekOldest(gRing, second);
  TEST_ASSERT_EQUAL_UINT16(1, ringSize(gRing));
  TEST_ASSERT_EQUAL_UINT32(first.ts, second.ts);
}

void test_capacity_is_respected() {
  for (uint32_t ts = 1; ts <= kTelemetryRingCapacity + 50u; ++ts) ringPush(gRing, sampleWithTs(ts));
  TEST_ASSERT_EQUAL_UINT16(kTelemetryRingCapacity, ringSize(gRing));
}

// Cheio, a mais velha cai. Com o enlace fora por muito tempo, o trecho proximo da volta e
// o que interessa — descartar a mais nova inverteria isso.
void test_overflow_drops_the_oldest_not_the_newest() {
  const uint32_t total = kTelemetryRingCapacity + 10u;
  for (uint32_t ts = 1; ts <= total; ++ts) ringPush(gRing, sampleWithTs(ts));

  TelemetrySample oldest{};
  TEST_ASSERT_TRUE(ringPeekOldest(gRing, oldest));
  TEST_ASSERT_EQUAL_UINT32(11, oldest.ts);

  uint32_t last = 0;
  TelemetrySample out{};
  while (ringPopOldest(gRing, out)) last = out.ts;
  TEST_ASSERT_EQUAL_UINT32(total, last);
}

// A volta do indice e onde anel escrito a mao erra. Enche, drena e enche de novo, varias
// vezes, para o head passar pelo fim do array mais de uma vez.
void test_survives_many_wraps() {
  for (int round = 0; round < 3; ++round) {
    for (uint32_t i = 1; i <= kTelemetryRingCapacity + 7u; ++i) ringPush(gRing, sampleWithTs(i));
    uint32_t drained = 0;
    TelemetrySample out{};
    uint32_t previous = 0;
    while (ringPopOldest(gRing, out)) {
      TEST_ASSERT_TRUE_MESSAGE(out.ts > previous, "amostra fora de ordem depois da volta");
      previous = out.ts;
      ++drained;
    }
    TEST_ASSERT_EQUAL_UINT32(kTelemetryRingCapacity, drained);
  }
}

// Power-on entrega RTC RAM com lixo. Sem o magic, o firmware leria isso como amostra e
// publicaria valores inventados.
void test_garbage_memory_is_rejected() {
  fillWithGarbage(gRing, 0xA5);
  TEST_ASSERT_FALSE(ringHeaderIsValid(gRing));
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));
  TEST_ASSERT_TRUE(ringHeaderIsValid(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

void test_zeroed_memory_is_rejected() {
  fillWithGarbage(gRing, 0x00);
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

// Depois de um OTA com a amostra mudada, o cabecalho antigo tem magic certo e versao
// errada. Aproveitar isso leria o layout velho deslocado.
void test_a_different_layout_version_is_rejected() {
  ringPush(gRing, sampleWithTs(1));
  gRing.layout = kTelemetryLayoutVersion + 1;
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

// O erro realista: alguem acrescenta um campo e esquece de subir a versao. O magic bate, a
// versao bate, e so o tamanho denuncia.
void test_a_different_sample_size_is_rejected() {
  ringPush(gRing, sampleWithTs(1));
  gRing.sample_size = sizeof(TelemetrySample) + 4;
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

// Contagem corrompida com cabecalho intacto levaria a leitura para fora do array — pior
// que perder o buffer, porque e leitura de memoria alheia.
void test_out_of_range_indices_are_rejected() {
  ringPush(gRing, sampleWithTs(1));
  gRing.count = kTelemetryRingCapacity + 1;
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));

  ringReset(gRing);
  ringPush(gRing, sampleWithTs(1));
  gRing.head = kTelemetryRingCapacity;
  TEST_ASSERT_FALSE(ringValidateOrReset(gRing));
}

// Cabecalho intacto e conteudo da mesma versao: o anel atravessa o reset, que e o motivo
// de ele morar em RTC_NOINIT.
void test_a_valid_ring_survives_validation_untouched() {
  ringPush(gRing, sampleWithTs(4242));
  TEST_ASSERT_TRUE(ringValidateOrReset(gRing));
  TEST_ASSERT_EQUAL_UINT16(1, ringSize(gRing));

  TelemetrySample out{};
  ringPeekOldest(gRing, out);
  TEST_ASSERT_EQUAL_UINT32(4242, out.ts);
}

// Amostra colhida 90 s antes de agora, com o relogio chegando so agora: o carimbo tem que
// virar epoch de 90 s atras, nao o epoch de agora.
void test_timestamp_correction_walks_back_from_now() {
  TelemetrySample sample = sampleWithTs(30);  // 30 s de uptime
  sample.flags |= kTelemetryFlagClockUnsynced;

  TEST_ASSERT_TRUE(correctSampleTimestamp(sample, 120, 1758585600));
  TEST_ASSERT_EQUAL_UINT32(1758585600 - 90, sample.ts);
  TEST_ASSERT_TRUE(sampleIsPublishable(sample));
}

void test_a_synced_sample_is_left_alone() {
  TelemetrySample sample = sampleWithTs(1758585600);
  TEST_ASSERT_FALSE(correctSampleTimestamp(sample, 120, 1758585900));
  TEST_ASSERT_EQUAL_UINT32(1758585600, sample.ts);
}

// Amostra com monotonico maior que agora e anel corrompido ou relogio que andou para tras.
// Inventar um instante seria pior: o ponto entraria no grafico com hora plausivel e errada.
void test_a_sample_from_the_future_is_refused() {
  TelemetrySample sample = sampleWithTs(500);
  sample.flags |= kTelemetryFlagClockUnsynced;

  TEST_ASSERT_FALSE(correctSampleTimestamp(sample, 120, 1758585600));
  TEST_ASSERT_EQUAL_UINT32(500, sample.ts);
  TEST_ASSERT_FALSE(sampleIsPublishable(sample));
}

// Uptime maior que o epoch nao existe, mas cabeceria numa subtracao de unsigned e viraria
// um instante gigante. Recusar e a unica resposta honesta.
void test_correction_never_underflows() {
  TelemetrySample sample = sampleWithTs(10);
  sample.flags |= kTelemetryFlagClockUnsynced;
  TEST_ASSERT_FALSE(correctSampleTimestamp(sample, 1000, 500));
  TEST_ASSERT_FALSE(sampleIsPublishable(sample));
}

// --- Drenagem (PRD 14, "Buffer que mente") ---------------------------------------------

namespace {

TelemetrySample unsyncedAt(uint32_t monotonicS) {
  TelemetrySample sample = sampleWithTs(monotonicS);
  sample.flags |= kTelemetryFlagClockUnsynced;
  return sample;
}

}  // namespace

void test_a_synced_sample_is_published_as_is() {
  TelemetrySample sample = sampleWithTs(1758585600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DrainAction::Publish),
                        static_cast<int>(decideDrain(sample, false, 100, 0)));
  TEST_ASSERT_EQUAL_UINT32(1758585600, sample.ts);
}

// Sem relogio ainda, a amostra sem hora espera: ela e corrigivel assim que o SNTP chegar.
void test_an_unsynced_sample_waits_for_the_clock() {
  TelemetrySample sample = unsyncedAt(40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DrainAction::Wait),
                        static_cast<int>(decideDrain(sample, false, 100, 0)));
  TEST_ASSERT_FALSE(sampleIsPublishable(sample));
}

void test_an_unsynced_sample_is_corrected_once_the_clock_arrives() {
  TelemetrySample sample = unsyncedAt(40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DrainAction::Publish),
                        static_cast<int>(decideDrain(sample, true, 100, 1758585600)));
  TEST_ASSERT_EQUAL_UINT32(1758585600 - 60, sample.ts);
  TEST_ASSERT_TRUE(sampleIsPublishable(sample));
}

// Correcao que nao fecha nao pode travar a fila: a amostra ficaria na cabeca do anel para
// sempre e nada atras dela sairia. Descartar uma amostra e melhor que parar a unidade.
void test_an_uncorrectable_sample_is_discarded_instead_of_blocking() {
  TelemetrySample fromTheFuture = unsyncedAt(500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DrainAction::Discard),
                        static_cast<int>(decideDrain(fromTheFuture, true, 100, 1758585600)));
}

// Carimbo monotonico so vale no boot em que foi colhido: o contador volta a zero no reset,
// e a conta de correcao com o monotonico de outro boot da uma hora plausivel e errada.
void test_boot_discards_unsynced_samples_and_keeps_the_rest_in_order() {
  ringPush(gRing, sampleWithTs(1000));
  ringPush(gRing, unsyncedAt(10));
  ringPush(gRing, sampleWithTs(2000));
  ringPush(gRing, unsyncedAt(20));
  ringPush(gRing, sampleWithTs(3000));

  TEST_ASSERT_EQUAL_UINT16(2, ringDiscardUnsynced(gRing));
  TEST_ASSERT_EQUAL_UINT16(3, ringSize(gRing));

  TelemetrySample out{};
  for (uint32_t expected : {1000u, 2000u, 3000u}) {
    TEST_ASSERT_TRUE(ringPopOldest(gRing, out));
    TEST_ASSERT_EQUAL_UINT32(expected, out.ts);
  }
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
}

// O descarte tambem tem que funcionar com o anel dado a volta, que e o caso de uma unidade
// que ficou fora mais de 3 h: a mais velha nao esta no indice zero.
void test_boot_discard_works_across_the_wrap() {
  for (uint32_t i = 0; i < kTelemetryRingCapacity + 7; ++i) {
    ringPush(gRing, (i % 2 == 0) ? sampleWithTs(10000 + i) : unsyncedAt(i));
  }
  const uint16_t before = ringSize(gRing);
  const uint16_t dropped = ringDiscardUnsynced(gRing);
  TEST_ASSERT_EQUAL_UINT16(before - dropped, ringSize(gRing));
  TEST_ASSERT_TRUE(ringHeaderIsValid(gRing));

  TelemetrySample out{};
  uint32_t previous = 0;
  while (ringPopOldest(gRing, out)) {
    TEST_ASSERT_TRUE(sampleIsPublishable(out));
    TEST_ASSERT_TRUE(out.ts > previous);
    previous = out.ts;
  }
}

void test_boot_discard_on_an_empty_ring_is_a_no_op() {
  TEST_ASSERT_EQUAL_UINT16(0, ringDiscardUnsynced(gRing));
  TEST_ASSERT_TRUE(ringIsEmpty(gRing));
  TEST_ASSERT_TRUE(ringHeaderIsValid(gRing));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_leaves_a_valid_and_empty_ring);
  RUN_TEST(test_ring_fits_the_rtc_budget);
  RUN_TEST(test_push_and_peek_return_the_same_sample);
  RUN_TEST(test_peek_on_an_empty_ring_reports_failure);
  RUN_TEST(test_drains_in_chronological_order);
  RUN_TEST(test_peek_does_not_consume);
  RUN_TEST(test_capacity_is_respected);
  RUN_TEST(test_overflow_drops_the_oldest_not_the_newest);
  RUN_TEST(test_survives_many_wraps);
  RUN_TEST(test_garbage_memory_is_rejected);
  RUN_TEST(test_zeroed_memory_is_rejected);
  RUN_TEST(test_a_different_layout_version_is_rejected);
  RUN_TEST(test_a_different_sample_size_is_rejected);
  RUN_TEST(test_out_of_range_indices_are_rejected);
  RUN_TEST(test_a_valid_ring_survives_validation_untouched);
  RUN_TEST(test_timestamp_correction_walks_back_from_now);
  RUN_TEST(test_a_synced_sample_is_left_alone);
  RUN_TEST(test_a_sample_from_the_future_is_refused);
  RUN_TEST(test_correction_never_underflows);
  RUN_TEST(test_a_synced_sample_is_published_as_is);
  RUN_TEST(test_an_unsynced_sample_waits_for_the_clock);
  RUN_TEST(test_an_unsynced_sample_is_corrected_once_the_clock_arrives);
  RUN_TEST(test_an_uncorrectable_sample_is_discarded_instead_of_blocking);
  RUN_TEST(test_boot_discards_unsynced_samples_and_keeps_the_rest_in_order);
  RUN_TEST(test_boot_discard_works_across_the_wrap);
  RUN_TEST(test_boot_discard_on_an_empty_ring_is_a_no_op);
  return UNITY_END();
}
