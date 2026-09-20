#include <unity.h>

#include "domain/link_diagnostics.h"

namespace {

// Mesma forma do test_uplink_status: o teste amarra o trecho que muda a decisao de quem le,
// nao a redacao exata.
bool mentions(const String& text, const char* fragment) {
  return text.find(fragment) != String::npos;
}

// --- cadencia do relatorio ---

void test_nothing_is_reported_before_the_window_closes() {
  TEST_ASSERT_FALSE(dropReportDue(kDropReportIntervalMs - 1, 0, 1));
}

void test_the_window_closing_releases_the_report() {
  TEST_ASSERT_TRUE(dropReportDue(kDropReportIntervalMs, 0, 1));
}

void test_a_window_without_drops_stays_quiet() {
  // Silencio quando nao ha descarte: uma linha a cada 30 s dizendo "zero" seria a mesma
  // poluicao que este debito existe para remover, so mais lenta.
  TEST_ASSERT_FALSE(dropReportDue(kDropReportIntervalMs, 0, 0));
}

void test_the_window_is_measured_from_the_last_report() {
  // Marco em 100 s, agora em 100 s + intervalo - 1: ainda nao fechou.
  unsigned long last = 100000;
  TEST_ASSERT_FALSE(dropReportDue(last + kDropReportIntervalMs - 1, last, 5));
  TEST_ASSERT_TRUE(dropReportDue(last + kDropReportIntervalMs, last, 5));
}

void test_the_millis_rollover_does_not_hold_the_report_forever() {
  // millis() vira a cada ~49 dias. Com subtracao de unsigned a conta atravessa a virada;
  // com comparacao direta de instantes o relatorio ficaria preso ate a proxima volta.
  unsigned long last = 0xFFFFFFFFUL - 1000UL;
  unsigned long now = last + kDropReportIntervalMs;  // ja deu a volta
  TEST_ASSERT_TRUE(dropReportDue(now, last, 5));
}

// --- leitura do heap ---

void test_a_comfortable_heap_is_not_flagged() {
  TEST_ASSERT_FALSE(heapLooksTight(kTightInternalHeapBytes + 1));
}

void test_a_heap_at_the_floor_is_flagged() {
  TEST_ASSERT_TRUE(heapLooksTight(kTightInternalHeapBytes));
}

void test_a_heap_below_the_floor_is_flagged() {
  TEST_ASSERT_TRUE(heapLooksTight(kTightInternalHeapBytes / 2));
}

// --- a frase ---

void test_the_report_carries_the_count_and_the_heap() {
  String text = describeDropWindow(2847, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "2847"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "96000"), text.c_str());
}

void test_a_comfortable_heap_reads_as_queue_pressure() {
  // A frase precisa separar as duas hipoteses do debito 13: fila cheia ou heap esgotado.
  // Com heap folgado, descarte e fila — e e isso que a linha tem que dizer.
  String text = describeDropWindow(2847, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "fila"), text.c_str());
}

void test_a_tight_heap_says_so_instead() {
  String text = describeDropWindow(2847, kTightInternalHeapBytes - 1);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "heap"), text.c_str());
}

void test_a_single_drop_is_not_written_in_plural() {
  String text = describeDropWindow(1, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "1 pacote "), text.c_str());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_reported_before_the_window_closes);
  RUN_TEST(test_the_window_closing_releases_the_report);
  RUN_TEST(test_a_window_without_drops_stays_quiet);
  RUN_TEST(test_the_window_is_measured_from_the_last_report);
  RUN_TEST(test_the_millis_rollover_does_not_hold_the_report_forever);
  RUN_TEST(test_a_comfortable_heap_is_not_flagged);
  RUN_TEST(test_a_heap_at_the_floor_is_flagged);
  RUN_TEST(test_a_heap_below_the_floor_is_flagged);
  RUN_TEST(test_the_report_carries_the_count_and_the_heap);
  RUN_TEST(test_a_comfortable_heap_reads_as_queue_pressure);
  RUN_TEST(test_a_tight_heap_says_so_instead);
  RUN_TEST(test_a_single_drop_is_not_written_in_plural);
  return UNITY_END();
}
