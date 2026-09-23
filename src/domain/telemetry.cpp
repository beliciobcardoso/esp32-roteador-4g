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

String buildTelemetryPayload(const TelemetrySample& sample) {
  const float volts = sample.battery_mv / 1000.0f;

  JsonObject body;
  body.number("ts", sample.ts)
      .number("battery_volts", volts, 2)
      // A carga sai da curva do dominio, nao de uma conta no consumidor: a relacao
      // tensao/carga de Li-ion nao e linear, e quem so tem os volts nao consegue refazer.
      .number("battery_percent", static_cast<uint32_t>(voltageToPercent(volts)))
      .number("uplink_state", sample.uplink_state)
      .boolean("uplink_rebooted", (sample.flags & kTelemetryFlagRebootedForUplink) != 0)
      .boolean("uplink_exhausted", (sample.flags & kTelemetryFlagRebootBudgetExhausted) != 0)
      .number("ppp_drops_total", sample.ppp_drops_total)
      .number("free_heap_kb", sample.free_heap_kb)
      .number("uptime_min", sample.uptime_min);
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
