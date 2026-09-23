#include <unity.h>

#include "domain/mqtt_backoff.h"

// A progressao e o contrato: 0 falhas nao espera, e cada falha dobra ate o teto. Os valores
// estao escritos a mao em vez de calculados com a mesma formula da implementacao — teste que
// repete a conta do codigo passa junto com o codigo errado.

void test_no_failure_does_not_wait() {
  TEST_ASSERT_EQUAL_UINT32(0, nextBackoffMs(0));
}

void test_backoff_doubles_each_failure() {
  TEST_ASSERT_EQUAL_UINT32(5000, nextBackoffMs(1));
  TEST_ASSERT_EQUAL_UINT32(10000, nextBackoffMs(2));
  TEST_ASSERT_EQUAL_UINT32(20000, nextBackoffMs(3));
  TEST_ASSERT_EQUAL_UINT32(40000, nextBackoffMs(4));
  TEST_ASSERT_EQUAL_UINT32(80000, nextBackoffMs(5));
  TEST_ASSERT_EQUAL_UINT32(160000, nextBackoffMs(6));
}

void test_backoff_saturates_at_the_ceiling() {
  TEST_ASSERT_EQUAL_UINT32(kMaxBackoffMs, nextBackoffMs(7));
  TEST_ASSERT_EQUAL_UINT32(kMaxBackoffMs, nextBackoffMs(20));
}

// Falha permanente (broker fora do ar por dias) leva o contador para valores em que um shift
// estouraria o uint32 e voltaria a dar espera curta — exatamente quando ela devia ser longa.
void test_huge_failure_count_never_wraps_to_a_short_wait() {
  for (uint32_t failures = 7; failures < 200; ++failures) {
    TEST_ASSERT_EQUAL_UINT32(kMaxBackoffMs, nextBackoffMs(failures));
  }
  TEST_ASSERT_EQUAL_UINT32(kMaxBackoffMs, nextBackoffMs(0xFFFFFFFFu));
}

// O jitter tem que caber na faixa para todo sorteio possivel, inclusive nos extremos: abaixo
// do piso ele atrasaria demais, acima do teto perderia o proposito do teto.
void test_jitter_stays_inside_the_band() {
  const uint32_t base = 100000;
  const uint32_t floorMs = base - base * kJitterPercent / 100;
  const uint32_t ceilingMs = base + base * kJitterPercent / 100;

  for (uint32_t random = 0; random < 5000; ++random) {
    const uint32_t jittered = applyJitter(base, random * 7919);
    TEST_ASSERT_TRUE_MESSAGE(jittered >= floorMs, "jitter abaixo do piso");
    TEST_ASSERT_TRUE_MESSAGE(jittered <= ceilingMs, "jitter acima do teto");
  }
}

// Espera zero nao ganha jitter: a primeira tentativa nao pode virar espera do nada.
void test_jitter_on_zero_stays_zero() {
  TEST_ASSERT_EQUAL_UINT32(0, applyJitter(0, 12345));
}

// Sorteios diferentes tem que produzir esperas diferentes — jitter constante nao espalha
// frota nenhuma, e passaria no teste de faixa sem cumprir o proposito.
void test_jitter_actually_varies() {
  const uint32_t base = 100000;
  TEST_ASSERT_NOT_EQUAL(applyJitter(base, 0), applyJitter(base, 999983));
}

// O teto do backoff com jitter ainda cabe no uint32 — 300 s mais 20% nao chega perto, mas a
// conta intermediaria (base * percentual) e onde um multiplicador descuidado estoura.
void test_jitter_at_the_ceiling_does_not_overflow() {
  const uint32_t jittered = applyJitter(kMaxBackoffMs, 0xFFFFFFFFu);
  TEST_ASSERT_TRUE(jittered >= kMaxBackoffMs - kMaxBackoffMs * kJitterPercent / 100);
  TEST_ASSERT_TRUE(jittered <= kMaxBackoffMs + kMaxBackoffMs * kJitterPercent / 100);
}

void test_never_attempts_without_uplink() {
  TEST_ASSERT_FALSE(shouldAttemptConnect(999999, 0, 0, false));
}

void test_attempts_when_the_wait_elapsed() {
  TEST_ASSERT_TRUE(shouldAttemptConnect(15000, 10000, 5000, true));
}

void test_waits_while_the_backoff_has_not_elapsed() {
  TEST_ASSERT_FALSE(shouldAttemptConnect(14999, 10000, 5000, true));
}

// Backoff zero e a primeira tentativa: tem que sair na hora, sem esperar volta de relogio.
void test_zero_backoff_attempts_immediately() {
  TEST_ASSERT_TRUE(shouldAttemptConnect(0, 0, 0, true));
}

// millis() vira em ~49 dias. Comparar instantes direto prenderia a reconexao ate a volta
// seguinte — uma unidade que ficasse esse tempo de pe perderia o broker e nao voltaria.
void test_survives_the_millis_rollover() {
  const uint32_t justBeforeWrap = 0xFFFFF000ul;
  const uint32_t justAfterWrap = 0x00001000ul;  // 8192 ms depois, ja com a virada
  TEST_ASSERT_TRUE(shouldAttemptConnect(justAfterWrap, justBeforeWrap, 5000, true));
  TEST_ASSERT_FALSE(shouldAttemptConnect(justAfterWrap, justBeforeWrap, 30000, true));
}

void test_connection_is_stable_only_after_the_threshold() {
  TEST_ASSERT_FALSE(connectionIsStable(kStableConnectionMs - 1, 0));
  TEST_ASSERT_TRUE(connectionIsStable(kStableConnectionMs, 0));
}

void test_stability_survives_the_millis_rollover() {
  const uint32_t connectedSince = 0xFFFFF000ul;
  TEST_ASSERT_TRUE(connectionIsStable(connectedSince + kStableConnectionMs, connectedSince));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_failure_does_not_wait);
  RUN_TEST(test_backoff_doubles_each_failure);
  RUN_TEST(test_backoff_saturates_at_the_ceiling);
  RUN_TEST(test_huge_failure_count_never_wraps_to_a_short_wait);
  RUN_TEST(test_jitter_stays_inside_the_band);
  RUN_TEST(test_jitter_on_zero_stays_zero);
  RUN_TEST(test_jitter_actually_varies);
  RUN_TEST(test_jitter_at_the_ceiling_does_not_overflow);
  RUN_TEST(test_never_attempts_without_uplink);
  RUN_TEST(test_attempts_when_the_wait_elapsed);
  RUN_TEST(test_waits_while_the_backoff_has_not_elapsed);
  RUN_TEST(test_zero_backoff_attempts_immediately);
  RUN_TEST(test_survives_the_millis_rollover);
  RUN_TEST(test_connection_is_stable_only_after_the_threshold);
  RUN_TEST(test_stability_survives_the_millis_rollover);
  return UNITY_END();
}
