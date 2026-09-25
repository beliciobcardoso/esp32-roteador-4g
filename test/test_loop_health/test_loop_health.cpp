#include <unity.h>

#include "domain/loop_health.h"

namespace {

// --- o limite ---

void test_a_loop_that_just_beat_is_not_stalled() {
  TEST_ASSERT_FALSE(loopHasStalled(1000, 1000));
}

void test_a_pause_below_the_limit_is_not_a_stall() {
  TEST_ASSERT_FALSE(loopHasStalled(1000 + kLoopStallLimitMs - 1, 1000));
}

void test_a_pause_at_the_limit_is_a_stall() {
  TEST_ASSERT_TRUE(loopHasStalled(1000 + kLoopStallLimitMs, 1000));
}

void test_a_pause_above_the_limit_is_a_stall() {
  TEST_ASSERT_TRUE(loopHasStalled(1000 + kLoopStallLimitMs + 1, 1000));
}

// A medicao que originou o arquivo: 137 s de silencio, muito alem de qualquer limite.
void test_the_measured_bench_stall_is_a_stall() {
  TEST_ASSERT_TRUE(loopHasStalled(1054432 + 137295, 1054432));
}

// --- a virada de millis() ---
//
// O caso que `unsigned long` escondia: no host ele tem 8 bytes e a subtracao nunca vira,
// entao o teste passava sem exercitar nada. Com `uint32_t` a conta vira igual na placa.

void test_a_short_pause_across_the_millis_wrap_is_not_a_stall() {
  const uint32_t lastBeat = UINT32_MAX - 1000;
  const uint32_t now = lastBeat + (kLoopStallLimitMs - 1);  // vira no meio
  TEST_ASSERT_FALSE(loopHasStalled(now, lastBeat));
}

void test_a_long_pause_across_the_millis_wrap_is_a_stall() {
  const uint32_t lastBeat = UINT32_MAX - 1000;
  const uint32_t now = lastBeat + kLoopStallLimitMs;  // vira no meio
  TEST_ASSERT_TRUE(loopHasStalled(now, lastBeat));
}

// O limite precisa caber com folga na janela de confirmacao do OTA e acima da morte por
// keepalive, senao o reinicio disputa com correcoes que deveriam agir antes dele.
void test_the_limit_sits_between_the_keepalive_death_and_the_confirmation_deadline() {
  TEST_ASSERT_GREATER_THAN_UINT32(11000, kLoopStallLimitMs);
  TEST_ASSERT_LESS_THAN_UINT32(600000, kLoopStallLimitMs);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_loop_that_just_beat_is_not_stalled);
  RUN_TEST(test_a_pause_below_the_limit_is_not_a_stall);
  RUN_TEST(test_a_pause_at_the_limit_is_a_stall);
  RUN_TEST(test_a_pause_above_the_limit_is_a_stall);
  RUN_TEST(test_the_measured_bench_stall_is_a_stall);
  RUN_TEST(test_a_short_pause_across_the_millis_wrap_is_not_a_stall);
  RUN_TEST(test_a_long_pause_across_the_millis_wrap_is_a_stall);
  RUN_TEST(test_the_limit_sits_between_the_keepalive_death_and_the_confirmation_deadline);
  return UNITY_END();
}
