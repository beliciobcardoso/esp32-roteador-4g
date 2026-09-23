#include "telemetry_buffer.h"

#include <cstring>

// 'T','L','M','1'. Legivel num dump de memoria, que e onde este valor vai ser procurado.
const uint32_t kTelemetryRingMagic = 0x544C4D31;
const uint16_t kTelemetryLayoutVersion = 1;

namespace {

uint16_t oldestIndex(const TelemetryRing& ring) {
  // Sem `+ capacidade` a conta daria negativo antes do modulo, e em unsigned isso vira um
  // indice enorme — leitura fora do array em vez de volta ao inicio.
  return static_cast<uint16_t>((ring.head + kTelemetryRingCapacity - ring.count) %
                               kTelemetryRingCapacity);
}

}  // namespace

bool ringHeaderIsValid(const TelemetryRing& ring) {
  if (ring.magic != kTelemetryRingMagic) return false;
  if (ring.layout != kTelemetryLayoutVersion) return false;
  if (ring.sample_size != sizeof(TelemetrySample)) return false;
  // Indices tambem sao cabecalho. Cabecalho intacto com contagem corrompida levaria o
  // drenar para fora do array, que e pior do que perder o buffer.
  if (ring.count > kTelemetryRingCapacity) return false;
  if (ring.head >= kTelemetryRingCapacity) return false;
  return true;
}

void ringReset(TelemetryRing& ring) {
  // Zera tambem as amostras. Com count = 0 elas ja seriam inalcancaveis, mas 4 KB de bytes
  // antigos num dump de memoria custam meia hora de quem esta depurando outra coisa.
  std::memset(&ring, 0, sizeof(ring));
  ring.magic = kTelemetryRingMagic;
  ring.layout = kTelemetryLayoutVersion;
  ring.sample_size = sizeof(TelemetrySample);
  ring.head = 0;
  ring.count = 0;
}

bool ringValidateOrReset(TelemetryRing& ring) {
  if (ringHeaderIsValid(ring)) return true;
  ringReset(ring);
  return false;
}

uint16_t ringSize(const TelemetryRing& ring) { return ring.count; }

bool ringIsEmpty(const TelemetryRing& ring) { return ring.count == 0; }

void ringPush(TelemetryRing& ring, const TelemetrySample& sample) {
  ring.samples[ring.head] = sample;
  ring.head = static_cast<uint16_t>((ring.head + 1) % kTelemetryRingCapacity);
  // Cheio, `head` acabou de passar por cima da mais velha e `count` fica no teto — e assim
  // que o descarte do mais velho acontece, sem mover nada.
  if (ring.count < kTelemetryRingCapacity) ++ring.count;
}

bool ringPeekOldest(const TelemetryRing& ring, TelemetrySample& out) {
  if (ring.count == 0) return false;
  out = ring.samples[oldestIndex(ring)];
  return true;
}

bool ringPopOldest(TelemetryRing& ring, TelemetrySample& out) {
  if (!ringPeekOldest(ring, out)) return false;
  --ring.count;
  return true;
}

bool correctSampleTimestamp(TelemetrySample& sample, uint32_t nowMonotonicS, uint32_t nowEpochS) {
  if ((sample.flags & kTelemetryFlagClockUnsynced) == 0) return false;

  // Monotonico maior que agora e anel corrompido, nao amostra do futuro. Inventar um
  // instante seria pior do que segurar: o ponto entraria no grafico com hora plausivel.
  if (sample.ts > nowMonotonicS) return false;

  const uint32_t elapsedS = nowMonotonicS - sample.ts;
  // Placa de pe ha mais tempo do que o proprio epoch nao existe, mas a subtracao abaixo
  // cabeceria em unsigned e devolveria um instante gigante em vez de erro.
  if (elapsedS > nowEpochS) return false;

  sample.ts = nowEpochS - elapsedS;
  sample.flags &= static_cast<uint8_t>(~kTelemetryFlagClockUnsynced);
  return true;
}
