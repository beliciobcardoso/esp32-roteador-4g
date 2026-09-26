#include "telemetry.h"

#include "battery.h"
#include "json.h"

const uint8_t kTelemetryFlagClockUnsynced = 0x01;
const uint8_t kTelemetryFlagRebootedForUplink = 0x02;
const uint8_t kTelemetryFlagRebootBudgetExhausted = 0x04;

const uint8_t kTelemetryStateFlagsMask =
    kTelemetryFlagRebootedForUplink | kTelemetryFlagRebootBudgetExhausted;

uint8_t encodeUplinkState(UplinkState state) {
  switch (state) {
    case UplinkState::Connecting:
      return 0;
    case UplinkState::Online:
      return 1;
    case UplinkState::Backoff:
      return 2;
  }
  // Inalcancavel com o enum atual. Fica como rede: um estado novo sem caso aqui sairia com
  // codigo de "conectando" e o painel mentiria em silencio. 255 nao colide com nada.
  return 255;
}

// As chaves sao o nome final da metrica no Prometheus (convencao do PRD 14): o Telegraf,
// com `name_override = "prometheus"`, usa a chave como esta. Por isso o prefixo `router_`,
// a unidade base no sufixo e `_total` so no counter — renomear depois quebra painel e
// historico ao mesmo tempo.
String buildTelemetryPayload(const TelemetrySample& sample) {
  const float volts = sample.battery_mv / 1000.0f;

  JsonObject body;
  body.number("ts", sample.ts)
      .number("router_battery_volts", volts, 2)
      // A carga sai da curva do dominio, nao de uma conta no consumidor: a relacao
      // tensao/carga de Li-ion nao e linear, e quem so tem os volts nao consegue refazer.
      // Razao 0-1 e nao porcentagem, que e o que o Prometheus recomenda.
      .number("router_battery_charge_ratio", voltageToPercent(volts) / 100.0f, 2)
      .number("router_uplink_state", sample.uplink_state)
      // Flag como numero 0/1, nunca como booleano JSON: o parser `json` do Telegraf so
      // aproveita numero e descarta booleano em silencio — e o segundo destes e o alerta
      // mais importante da fase.
      .number("router_uplink_rebooted",
              static_cast<uint32_t>((sample.flags & kTelemetryFlagRebootedForUplink) != 0))
      .number("router_uplink_reboot_budget_exhausted",
              static_cast<uint32_t>((sample.flags & kTelemetryFlagRebootBudgetExhausted) != 0))
      .number("router_ppp_drops_total", sample.ppp_drops_total)
      // O anel guarda KB e minutos para caber em 20 B; o nome promete bytes e segundos.
      .number("router_heap_internal_free_bytes", static_cast<uint32_t>(sample.free_heap_kb) * 1024u)
      .number("router_uptime_seconds", static_cast<uint32_t>(sample.uptime_min) * 60u);
  return body.finish();
}

bool sampleIsPublishable(const TelemetrySample& sample) {
  return (sample.flags & kTelemetryFlagClockUnsynced) == 0;
}

bool telemetryIntervalDue(uint32_t now, uint32_t lastPublishMs, uint32_t intervalMs) {
  return (now - lastPublishMs) >= intervalMs;
}

bool telemetryStateChanged(const TelemetrySample& previous, const TelemetrySample& current) {
  if (previous.uplink_state != current.uplink_state) return true;
  return (previous.flags & kTelemetryStateFlagsMask) != (current.flags & kTelemetryStateFlagsMask);
}

bool shouldPublishTelemetry(uint32_t now, uint32_t lastPublishMs, uint32_t intervalMs,
                            const TelemetrySample& previous, const TelemetrySample& current) {
  return telemetryIntervalDue(now, lastPublishMs, intervalMs) ||
         telemetryStateChanged(previous, current);
}
