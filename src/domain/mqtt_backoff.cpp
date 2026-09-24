#include "mqtt_backoff.h"

const uint32_t kInitialBackoffMs = 5000;
const uint32_t kMaxBackoffMs = 300000;
const uint32_t kStableConnectionMs = 60000;
const uint32_t kJitterPercent = 20;

uint32_t nextBackoffMs(uint32_t consecutiveFailures) {
  if (consecutiveFailures == 0) return 0;

  // Dobra por soma e satura por comparacao, em vez de `kInitialBackoffMs << (falhas - 1)`.
  // Com o shift, falha permanente — broker fora do ar por dias — leva o contador a um
  // deslocamento que estoura o uint32 e devolve espera curta de novo, que e o oposto do
  // que o backoff existe para fazer. O laco anda no maximo ~6 voltas antes de saturar.
  uint32_t backoff = kInitialBackoffMs;
  for (uint32_t step = 1; step < consecutiveFailures; ++step) {
    if (backoff >= kMaxBackoffMs / 2) return kMaxBackoffMs;
    backoff *= 2;
  }
  return backoff > kMaxBackoffMs ? kMaxBackoffMs : backoff;
}

uint32_t applyJitter(uint32_t backoffMs, uint32_t randomValue) {
  // Espera zero e a primeira tentativa. Jitter aqui inventaria atraso do nada.
  if (backoffMs == 0) return 0;

  // `backoffMs` vem sempre de nextBackoffMs(), logo nunca passa de kMaxBackoffMs — o
  // produto abaixo cabe folgado no uint32. Se o teto subir muito, esta multiplicacao e o
  // primeiro lugar que estoura.
  const uint32_t span = backoffMs * kJitterPercent / 100;
  if (span == 0) return backoffMs;

  return backoffMs - span + (randomValue % (2 * span + 1));
}

bool shouldAttemptConnect(uint32_t now, uint32_t lastAttemptMs, uint32_t backoffMs,
                          bool uplinkOnline) {
  if (!uplinkOnline) return false;
  return (now - lastAttemptMs) >= backoffMs;
}

bool connectionIsStable(uint32_t now, uint32_t connectedSinceMs) {
  return (now - connectedSinceMs) >= kStableConnectionMs;
}
