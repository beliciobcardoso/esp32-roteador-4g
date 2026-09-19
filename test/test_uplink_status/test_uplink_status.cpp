#include <unity.h>

#include "domain/uplink_status.h"

// A pessoa que abre esta pagina esta sem internet e quer saber uma coisa so: esperar
// resolve? Os testes amarram essa distincao, nao a redacao exata — cada um verifica o
// trecho que muda a decisao de quem le.

namespace {

bool mentions(const String& text, const char* fragment) {
  return text.find(fragment) != String::npos;
}

UplinkStatus online() {
  UplinkStatus status;
  status.state = UplinkState::Online;
  return status;
}

UplinkStatus backoff(uint32_t failures, bool exhausted) {
  UplinkStatus status;
  status.state = UplinkState::Backoff;
  status.consecutive_failures = failures;
  status.reboot_budget_exhausted = exhausted;
  return status;
}

}  // namespace

void test_online_says_it_is_connected() {
  const String text = describeUplinkStatus(online());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "onectado"), text.c_str());
}

// Online nao pode arrastar contagem de falha nenhuma: o campo continua preenchido com o
// valor da ultima queda ate a proxima falha, e mostra-lo aqui diria que algo esta errado
// quando nao esta.
void test_online_does_not_mention_failures() {
  UplinkStatus status = online();
  status.consecutive_failures = 7;
  const String text = describeUplinkStatus(status);
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "7"), text.c_str());
}

void test_first_attempt_says_it_is_connecting() {
  UplinkStatus status;
  status.state = UplinkState::Connecting;
  const String text = describeUplinkStatus(status);
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "onectando"), text.c_str());
}

// Esperar resolve: houve falha, mas a placa ainda vai tentar de novo sozinha.
void test_backoff_reports_the_failure_count_and_a_retry() {
  const String text = describeUplinkStatus(backoff(3, false));
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "3"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "tentativa"), text.c_str());
}

// Esperar NAO resolve. E o estado mais util de mostrar e o unico que hoje nao sai de
// lugar nenhum a nao ser do serial, que em campo nao existe (debito 22).
void test_exhausted_budget_points_at_an_external_cause() {
  const String text = describeUplinkStatus(backoff(24, true));
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "SIM"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "cobertura"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "credito"), text.c_str());
}

// Sem orcamento de reinicio a placa segue tentando, entao o estado corrente alterna entre
// Connecting e Backoff. Nos dois a mensagem tem que ser a de causa externa: quem le
// precisa saber que esperar nao adianta, e nao pegar "conectando" num refresh e achar que
// esta perto de resolver.
void test_exhausted_budget_outranks_the_current_state() {
  UplinkStatus status = backoff(24, true);
  status.state = UplinkState::Connecting;
  const String connecting = describeUplinkStatus(status);
  status.state = UplinkState::Backoff;
  const String waiting = describeUplinkStatus(status);
  TEST_ASSERT_EQUAL_STRING(waiting.c_str(), connecting.c_str());
}

// Reinicio pendente ainda e "esperar resolve": o orcamento nao acabou, a placa vai
// reiniciar e tentar de novo. Nao pode cair na mensagem de causa externa.
void test_many_failures_with_budget_left_is_not_an_external_cause() {
  const String text = describeUplinkStatus(backoff(9, false));
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "SIM"), text.c_str());
}

// Depois de um reinicio disparado pelo supervisor, consecutive_failures volta a zero: o
// contador e membro da classe e nao atravessa o restart, enquanto o de reinicios atravessa
// (RTC RAM). Sem separar os dois casos a pagina diria "conectando pela primeira vez" logo
// depois de a placa ter reiniciado por falta de uplink — progresso que nao existe, que e
// o mesmo defeito que o debito 22 aponta no caso de orcamento esgotado.
void test_connecting_after_a_reboot_is_not_the_first_attempt() {
  UplinkStatus status;
  status.state = UplinkState::Connecting;
  status.rebooted_for_uplink = true;
  const String text = describeUplinkStatus(status);
  TEST_ASSERT_FALSE_MESSAGE(mentions(text, "primeira vez"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(mentions(text, "reinici"), text.c_str());
}

void test_every_state_produces_a_non_empty_message() {
  const UplinkState states[] = {UplinkState::Connecting, UplinkState::Online, UplinkState::Backoff};
  for (UplinkState state : states) {
    UplinkStatus status;
    status.state = state;
    TEST_ASSERT_TRUE(describeUplinkStatus(status).length() > 0);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_online_says_it_is_connected);
  RUN_TEST(test_online_does_not_mention_failures);
  RUN_TEST(test_first_attempt_says_it_is_connecting);
  RUN_TEST(test_backoff_reports_the_failure_count_and_a_retry);
  RUN_TEST(test_exhausted_budget_points_at_an_external_cause);
  RUN_TEST(test_exhausted_budget_outranks_the_current_state);
  RUN_TEST(test_many_failures_with_budget_left_is_not_an_external_cause);
  RUN_TEST(test_connecting_after_a_reboot_is_not_the_first_attempt);
  RUN_TEST(test_every_state_produces_a_non_empty_message);
  return UNITY_END();
}
