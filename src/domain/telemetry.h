#pragma once

#include <cstdint>

#include "string_type.h"
#include "uplink_status.h"

// ENTIDADE — a amostra de telemetria, o JSON que sai dela e a decisao de quando publicar.
// Regra pura: nao conhece MQTT, esp_heap_caps nem RTC, entao roda no host.
//
// A amostra tem layout binario fixo porque e ela que vai para o anel em RTC slow RAM
// (ver telemetry_buffer.h), onde o orcamento e de ~4 KB e nao cabe texto. O mesmo registro
// serve para a publicacao ao vivo: amostra ao vivo e amostra do buffer tem que ter o mesmo
// formato, senao viram dois caminhos de codigo e um deles envelhece sem ninguem notar.

// Bit 0 — a amostra foi colhida antes da primeira sincronizacao de relogio, e `ts` guarda
// segundos monotonicos desde o boot, nao epoch. Enquanto este bit estiver ligado a amostra
// NAO pode ser publicada: gravar 1970 no banco contamina a serie de um jeito que so se
// descobre olhando o grafico meses depois.
extern const uint8_t kTelemetryFlagClockUnsynced;
// Bit 1 — a placa ja reiniciou por falta de uplink desde o ultimo power-on.
extern const uint8_t kTelemetryFlagRebootedForUplink;
// Bit 2 — o orcamento de reinicios acabou. E o unico estado em que esperar nao resolve.
extern const uint8_t kTelemetryFlagRebootBudgetExhausted;

// Os bits que descrevem o estado do enlace, e so eles. O bit de relogio fica de fora de
// proposito: ele e escrituracao interna, e trata-lo como mudanca de estado faria a primeira
// sincronizacao publicar uma amostra extra sem nada de novo para contar.
extern const uint8_t kTelemetryStateFlagsMask;

// 20 bytes, sem preenchimento implicito. O `reserved` de 4 bytes no fim nao e sobra: e o
// que permite o primeiro campo novo entrar sem subir a versao do anel e sem encurtar a
// janela de backfill. Sem ele, o compilador poria 2 bytes de padding invisivel ali e a
// primeira adicao custaria buffer.
struct TelemetrySample {
  uint32_t ts;               // epoch em segundos, ou monotonico se kTelemetryFlagClockUnsynced
  uint32_t ppp_drops_total;  // acumulado desde o boot — counter, nao janela
  uint16_t battery_mv;
  uint16_t free_heap_kb;
  uint16_t uptime_min;
  uint8_t uplink_state;
  uint8_t flags;
  uint32_t reserved;
};

static_assert(sizeof(TelemetrySample) == 20, "layout da amostra mudou — subir kTelemetryLayoutVersion");

// Estado do enlace como numero, porque e assim que ele chega ao Prometheus. String daria
// um label, e label que muda de valor cria serie nova a cada transicao.
uint8_t encodeUplinkState(UplinkState state);

// O JSON de uma amostra. Campo `ts` sempre presente, inclusive na amostra publicada na
// hora — e o que faz o backfill e o tempo real terem o mesmo formato.
//
// Nao aceita amostra com o relogio pendente: quem chama tem que corrigir antes (ver
// correctSampleTimestamp em telemetry_buffer.h) ou segurar a amostra no anel.
String buildTelemetryPayload(const TelemetrySample& sample);

// Amostra pode ir para a rede?
bool sampleIsPublishable(const TelemetrySample& sample);

// A janela de intervalo fechou? Subtracao de unsigned pela virada de millis(), e uint32_t
// e nao unsigned long pelo motivo explicado em mqtt_backoff.h.
bool telemetryIntervalDue(uint32_t now, uint32_t lastPublishMs, uint32_t intervalMs);

// O estado do enlace mudou entre duas amostras? Mudanca publica na hora, sem esperar a
// janela — uma queda de uplink que so aparecesse no proximo intervalo seria um evento
// datado errado, e a cadencia de 60 s existe para economizar dado, nao para atrasar
// notificacao.
bool telemetryStateChanged(const TelemetrySample& previous, const TelemetrySample& current);

// A decisao completa: publica por tempo ou por mudanca.
bool shouldPublishTelemetry(uint32_t now, uint32_t lastPublishMs, uint32_t intervalMs,
                            const TelemetrySample& previous, const TelemetrySample& current);
