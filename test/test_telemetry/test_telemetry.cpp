#include <unity.h>

#include "domain/telemetry.h"

namespace {

// Amostra de referencia: placa online, bateria a 3,84 V (50% na curva de battery.cpp),
// relogio ja sincronizado.
TelemetrySample baseline() {
  TelemetrySample sample{};
  sample.ts = 1758585600;  // 23/09/2026 00:00:00 UTC
  sample.ppp_drops_total = 1727;
  sample.battery_mv = 3840;
  sample.free_heap_kb = 113;
  sample.uptime_min = 42;
  sample.uplink_state = encodeUplinkState(UplinkState::Online);
  sample.flags = 0;
  sample.reserved = 0;
  return sample;
}

bool contains(const String& haystack, const char* needle) {
  return haystack.find(needle) != String::npos;
}

}  // namespace

// O layout binario e contrato com o anel em RTC: mudar sem subir a versao faz o firmware
// novo ler o buffer antigo deslocado. O static_assert no header ja quebra o build, e este
// teste existe para que a mensagem chegue tambem a quem roda so os testes.
void test_sample_layout_is_twenty_bytes() {
  TEST_ASSERT_EQUAL_UINT32(20, sizeof(TelemetrySample));
}

// Os tres estados precisam de codigos distintos, senao o painel nao distingue "conectando"
// de "sem orcamento" — que e a diferenca entre esperar e ir ate la.
void test_uplink_states_encode_distinctly() {
  const uint8_t connecting = encodeUplinkState(UplinkState::Connecting);
  const uint8_t online = encodeUplinkState(UplinkState::Online);
  const uint8_t backoff = encodeUplinkState(UplinkState::Backoff);
  TEST_ASSERT_NOT_EQUAL(connecting, online);
  TEST_ASSERT_NOT_EQUAL(online, backoff);
  TEST_ASSERT_NOT_EQUAL(connecting, backoff);
}

void test_payload_carries_the_timestamp() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"ts\":1758585600"), json.c_str());
}

// Tensao sai em volts e a carga sai da regra do dominio, nao de uma conta no consumidor.
// 3840 mV sao 3,84 V, que e ponto tabelado em battery.cpp: 50%, publicado como razao 0,50
// porque o Prometheus usa unidade base e razao, nao porcentagem (convencao do PRD 14).
void test_payload_converts_millivolts_and_reuses_the_battery_curve() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_battery_volts\":3.84"), json.c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_battery_charge_ratio\":0.50"), json.c_str());
}

// Heap e uptime sao guardados no anel em KB e em minutos para caber em 20 B, mas saem em
// bytes e segundos: e a unidade base que o nome da metrica promete. Converter no consumidor
// deixaria o nome mentindo para quem le o Prometheus sem ler este arquivo.
void test_payload_converts_ring_units_to_base_units() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_heap_internal_free_bytes\":115712"),
                           json.c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_uptime_seconds\":2520"), json.c_str());
}

// Toda chave que nao e o tempo carrega o prefixo da familia. E o que deixa o Telegraf, com
// `name_override = "prometheus"`, usar a chave como nome da metrica sem configuracao por
// campo — e o que impede `sensor_*` de colidir com o diagnostico da placa.
void test_payload_metric_keys_carry_the_router_prefix() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"battery_volts\""), json.c_str());
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"uplink_state\""), json.c_str());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_uplink_state\":"), json.c_str());
}

// ppp_drops sai como acumulado, que e o que faz rate() sobreviver a mensagem perdida e a
// unidade que ficou offline. Delta perdido seria erro permanente na soma.
void test_payload_carries_the_cumulative_drop_counter() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_ppp_drops_total\":1727"), json.c_str());
}

void test_payload_carries_the_uplink_flags_as_booleans() {
  TelemetrySample sample = baseline();
  sample.flags = kTelemetryFlagRebootedForUplink | kTelemetryFlagRebootBudgetExhausted;
  const String json = buildTelemetryPayload(sample);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"router_uplink_rebooted\":true"), json.c_str());
  TEST_ASSERT_TRUE_MESSAGE(
      contains(json, "\"router_uplink_reboot_budget_exhausted\":true"), json.c_str());
}

void test_payload_is_a_closed_json_object() {
  const String json = buildTelemetryPayload(baseline());
  TEST_ASSERT_EQUAL_CHAR('{', json[0]);
  TEST_ASSERT_EQUAL_CHAR('}', json[json.length() - 1]);
}

// Amostra sem relogio guarda monotonico em `ts`. Publicar isso poria 1970 no banco, e o
// estrago so aparece meses depois, quando alguem olhar o grafico longo.
void test_sample_without_a_clock_is_not_publishable() {
  TelemetrySample sample = baseline();
  sample.flags |= kTelemetryFlagClockUnsynced;
  TEST_ASSERT_FALSE(sampleIsPublishable(sample));
  TEST_ASSERT_TRUE(sampleIsPublishable(baseline()));
}

void test_interval_holds_until_the_window_closes() {
  TEST_ASSERT_FALSE(telemetryIntervalDue(59999, 0, 60000));
  TEST_ASSERT_TRUE(telemetryIntervalDue(60000, 0, 60000));
}

void test_interval_survives_the_millis_rollover() {
  const uint32_t last = 0xFFFFF000u;
  TEST_ASSERT_TRUE(telemetryIntervalDue(last + 60000, last, 60000));
}

void test_state_change_is_the_uplink_state() {
  TelemetrySample previous = baseline();
  TelemetrySample current = baseline();
  current.uplink_state = encodeUplinkState(UplinkState::Backoff);
  TEST_ASSERT_TRUE(telemetryStateChanged(previous, current));
}

void test_state_change_includes_the_uplink_flags() {
  TelemetrySample previous = baseline();
  TelemetrySample current = baseline();
  current.flags |= kTelemetryFlagRebootBudgetExhausted;
  TEST_ASSERT_TRUE(telemetryStateChanged(previous, current));
}

// Bateria e heap mudam a cada leitura. Se contassem como mudanca de estado, a publicacao
// por evento viraria publicacao continua e a cadencia de 60 s nao economizaria nada.
void test_measurements_alone_are_not_a_state_change() {
  TelemetrySample previous = baseline();
  TelemetrySample current = baseline();
  current.battery_mv = 3700;
  current.free_heap_kb = 90;
  current.ppp_drops_total = 99999;
  current.uptime_min = 43;
  current.ts += 30;
  TEST_ASSERT_FALSE(telemetryStateChanged(previous, current));
}

// A primeira sincronizacao apaga o bit de relogio. Tratar isso como mudanca de estado
// publicaria uma amostra extra sem nada de novo para contar.
void test_the_clock_flag_is_not_a_state_change() {
  TelemetrySample previous = baseline();
  previous.flags |= kTelemetryFlagClockUnsynced;
  TelemetrySample current = baseline();
  TEST_ASSERT_FALSE(telemetryStateChanged(previous, current));
}

// Queda de uplink no meio da janela nao pode esperar o proximo intervalo: o evento ficaria
// datado com ate um minuto de atraso, e e o instante da queda que interessa.
void test_state_change_publishes_before_the_window_closes() {
  TelemetrySample previous = baseline();
  TelemetrySample current = baseline();
  current.uplink_state = encodeUplinkState(UplinkState::Backoff);
  TEST_ASSERT_TRUE(shouldPublishTelemetry(1000, 0, 60000, previous, current));
}

void test_quiet_link_publishes_only_on_the_interval() {
  TelemetrySample previous = baseline();
  TelemetrySample current = baseline();
  current.battery_mv = 3800;
  TEST_ASSERT_FALSE(shouldPublishTelemetry(1000, 0, 60000, previous, current));
  TEST_ASSERT_TRUE(shouldPublishTelemetry(60000, 0, 60000, previous, current));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sample_layout_is_twenty_bytes);
  RUN_TEST(test_uplink_states_encode_distinctly);
  RUN_TEST(test_payload_carries_the_timestamp);
  RUN_TEST(test_payload_converts_millivolts_and_reuses_the_battery_curve);
  RUN_TEST(test_payload_converts_ring_units_to_base_units);
  RUN_TEST(test_payload_metric_keys_carry_the_router_prefix);
  RUN_TEST(test_payload_carries_the_cumulative_drop_counter);
  RUN_TEST(test_payload_carries_the_uplink_flags_as_booleans);
  RUN_TEST(test_payload_is_a_closed_json_object);
  RUN_TEST(test_sample_without_a_clock_is_not_publishable);
  RUN_TEST(test_interval_holds_until_the_window_closes);
  RUN_TEST(test_interval_survives_the_millis_rollover);
  RUN_TEST(test_state_change_is_the_uplink_state);
  RUN_TEST(test_state_change_includes_the_uplink_flags);
  RUN_TEST(test_measurements_alone_are_not_a_state_change);
  RUN_TEST(test_the_clock_flag_is_not_a_state_change);
  RUN_TEST(test_state_change_publishes_before_the_window_closes);
  RUN_TEST(test_quiet_link_publishes_only_on_the_interval);
  return UNITY_END();
}
