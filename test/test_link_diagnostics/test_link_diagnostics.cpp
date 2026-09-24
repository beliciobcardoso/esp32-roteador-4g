#include <unity.h>

#include "domain/link_diagnostics.h"

namespace {

// Mesma forma do test_uplink_status: o teste amarra o trecho que muda a decisao de quem le,
// nao a redacao exata.
bool mentions(const String& text, const char* fragment) {
  return text.find(fragment) != String::npos;
}

// --- cadencia da janela ---

void test_the_window_is_not_closed_before_the_interval() {
  TEST_ASSERT_FALSE(dropWindowClosed(kDropReportIntervalMs - 1, 0));
}

void test_the_interval_closes_the_window() {
  TEST_ASSERT_TRUE(dropWindowClosed(kDropReportIntervalMs, 0));
}

void test_the_window_is_measured_from_the_last_one() {
  const uint32_t last = 100000;
  TEST_ASSERT_FALSE(dropWindowClosed(last + kDropReportIntervalMs - 1, last));
  TEST_ASSERT_TRUE(dropWindowClosed(last + kDropReportIntervalMs, last));
}

void test_the_millis_rollover_does_not_hold_the_window_forever() {
  // millis() vira a cada ~49 dias. Com subtracao de unsigned a conta atravessa a virada;
  // com comparacao direta de instantes a janela ficaria presa ate a proxima volta.
  //
  // Este teste so exercita a virada porque os instantes sao uint32_t. Enquanto eram
  // `unsigned long` ele passava sem provar nada: no host sao 8 bytes, entao a soma nunca
  // dava a volta e a subtracao acertava por nao ter sido testada. E a razao da regra do
  // AGENTS.md sobre instante de tempo no dominio.
  const uint32_t last = 0xFFFFFFFFu - 1000u;
  const uint32_t now = last + kDropReportIntervalMs;  // ja deu a volta
  TEST_ASSERT_TRUE(now < last);                       // a virada aconteceu mesmo
  TEST_ASSERT_TRUE(dropWindowClosed(now, last));
}

// --- a janela silenciosa ---
//
// Ate 23/09/2026 janela sem descarte nao imprimia nada, e "zero descartes" ficava
// indistinguivel de "o relatorio nao esta saindo": as duas coisas apareciam como ausencia de
// linha. O debito 13 precisa provar zero sob carga, e prova que se parece com falha nao
// serve.

void test_a_single_quiet_window_does_not_speak_yet() {
  // Silencio curto continua calado: uma linha a cada 30 s dizendo zero seria a poluicao que
  // este arquivo existe para remover, so mais lenta.
  TEST_ASSERT_FALSE(quietReportDue(1));
}

void test_quiet_windows_below_the_threshold_stay_quiet() {
  TEST_ASSERT_FALSE(quietReportDue(kQuietWindowsPerReport - 1));
}

void test_enough_quiet_windows_force_a_line() {
  TEST_ASSERT_TRUE(quietReportDue(kQuietWindowsPerReport));
}

void test_quiet_windows_past_the_threshold_still_speak() {
  // Defensivo: se o contador passar do limite por qualquer motivo, a linha tem que sair
  // assim mesmo — calar acima do limite recriaria o silencio ambiguo.
  TEST_ASSERT_TRUE(quietReportDue(kQuietWindowsPerReport + 1));
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
  // A primeira versao deste teste olhava so o "1 pacote " e passava enquanto a frase dizia
  // "1 pacote descartados" — o verbo ficava de fora da asserção. Apareceu no log de campo,
  // nao aqui. Agora cobre a frase ate o verbo.
  String text = describeDropWindow(1, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "1 pacote descartado na"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "descartados"), text.c_str());
}

void test_more_than_one_drop_is_written_in_plural() {
  String text = describeDropWindow(2, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "2 pacotes descartados na"), text.c_str());
}

void test_the_quiet_line_states_zero_with_a_digit() {
  // "0" escrito, e nao "sem descartes": numero e mais dificil de confundir com ausencia de
  // medicao do que uma negativa.
  String text = describeQuietWindows(4, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "0 pacotes descartados"), text.c_str());
}

void test_the_quiet_line_says_how_much_time_it_covers() {
  // Sem o tempo coberto a linha diria "nao vi nada" sem dizer por quanto tempo olhou, que e
  // metade da afirmacao.
  String text = describeQuietWindows(4, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "120 s"), text.c_str());
}

void test_the_quiet_line_carries_the_heap_too() {
  // A hipotese alternativa do debito 13 (heap esgotado) tem que continuar verificavel nas
  // janelas quietas, senao a serie tem buraco justamente no trecho que se quer provar.
  String text = describeQuietWindows(4, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "96000"), text.c_str());
}

void test_the_quiet_line_does_not_diagnose_a_drop_that_did_not_happen() {
  // A primeira versao desta linha terminava em "descarte e pressao de fila" mesmo com zero
  // descartes: diagnosticava a causa de um evento que nao ocorreu. O heap fica, o veredito
  // sai.
  String text = describeQuietWindows(4, 96000);
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "pressao de fila"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "descarte e"), text.c_str());
}

void test_a_tight_heap_is_still_flagged_on_a_quiet_line() {
  // Silencio com heap no talo continua merecendo aviso: a hipotese alternativa do debito 13
  // nao depende de ter havido descarte naquela janela.
  String text = describeQuietWindows(4, kTightInternalHeapBytes - 1);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "heap no talo"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "nao ser da fila"), text.c_str());
}

void test_a_single_quiet_window_is_not_written_in_plural() {
  String text = describeQuietWindows(1, 96000);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "1 janela de"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "janelas"), text.c_str());
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_window_is_not_closed_before_the_interval);
  RUN_TEST(test_the_interval_closes_the_window);
  RUN_TEST(test_the_window_is_measured_from_the_last_one);
  RUN_TEST(test_the_millis_rollover_does_not_hold_the_window_forever);
  RUN_TEST(test_a_single_quiet_window_does_not_speak_yet);
  RUN_TEST(test_quiet_windows_below_the_threshold_stay_quiet);
  RUN_TEST(test_enough_quiet_windows_force_a_line);
  RUN_TEST(test_quiet_windows_past_the_threshold_still_speak);
  RUN_TEST(test_a_comfortable_heap_is_not_flagged);
  RUN_TEST(test_a_heap_at_the_floor_is_flagged);
  RUN_TEST(test_a_heap_below_the_floor_is_flagged);
  RUN_TEST(test_the_report_carries_the_count_and_the_heap);
  RUN_TEST(test_a_comfortable_heap_reads_as_queue_pressure);
  RUN_TEST(test_a_tight_heap_says_so_instead);
  RUN_TEST(test_a_single_drop_is_not_written_in_plural);
  RUN_TEST(test_more_than_one_drop_is_written_in_plural);
  RUN_TEST(test_the_quiet_line_states_zero_with_a_digit);
  RUN_TEST(test_the_quiet_line_says_how_much_time_it_covers);
  RUN_TEST(test_the_quiet_line_carries_the_heap_too);
  RUN_TEST(test_the_quiet_line_does_not_diagnose_a_drop_that_did_not_happen);
  RUN_TEST(test_a_tight_heap_is_still_flagged_on_a_quiet_line);
  RUN_TEST(test_a_single_quiet_window_is_not_written_in_plural);
  return UNITY_END();
}
