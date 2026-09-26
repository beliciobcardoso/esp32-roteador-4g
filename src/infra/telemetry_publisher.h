#pragma once

#include <Arduino.h>

#include <cstdint>

#include "../domain/firmware_update.h"
#include "../domain/router_settings.h"
#include "../domain/telemetry.h"
#include "../domain/uplink_status.h"
#include "mqtt_client.h"

// INFRA — a telemetria da unidade (PRD 14): colhe a amostra, guarda no anel em RTC RAM e
// drena para o broker. Roda inteira na task do loop(); a do esp-mqtt so liga flags.
//
// Um caminho so para amostra ao vivo e de backfill. Toda amostra entra no anel e sai dele
// da mais velha para a mais nova, uma por vez, em QoS 1, e so e removida quando o broker
// devolve o PUBACK. Dois motivos:
// - remover antes perderia a amostra numa conexao que caiu no meio, e em QoS 0 nao existe
//   "aceito" para esperar (telemetry_buffer.h, ringPopOldest)
// - a ordem cronologica chega ao servidor como saiu da placa. Amostra velha depois da nova,
//   no mesmo lote do Telegraf, e descartada em silencio pelo serializer do remote_write
//
// Mora em infra e nao num caso de uso pelo mesmo motivo do LinkSupervisor: e orquestracao
// de hardware com implementacao unica, e uma interface para cada dependencia seria
// abstracao sem segunda ocorrencia (AGENTS.md).
class TelemetryPublisher {
 public:
  // Leituras que moram noutros globais do main.cpp. Ponteiro de funcao pelo mesmo motivo do
  // http_config_handler: nao ha captura, e o publisher nao precisa conhecer quem responde.
  using BatteryVoltageRead = float (*)();
  using UplinkStatusRead = UplinkStatus (*)();
  using ClockSyncedRead = bool (*)();
  using ImageStateRead = FirmwareImageState (*)();

  explicit TelemetryPublisher(MqttClient& client) : client_(client) {}

  void onBatteryVoltageRequested(BatteryVoltageRead read) { batteryVoltage_ = read; }
  void onUplinkStatusRequested(UplinkStatusRead read) { uplinkStatus_ = read; }
  void onClockSyncedRequested(ClockSyncedRead read) { clockSynced_ = read; }
  void onImageStateRequested(ImageStateRead read) { imageState_ = read; }
  void logTo(Print& log) { log_ = &log; }

  // Boot: valida o anel que atravessou o reset e aplica a configuracao. Os callbacks tem
  // que estar registrados antes.
  void begin(const RouterSettings& settings);

  // Configuracao nova pela pagina. So recria o cliente se algo do broker mudou: trocar o
  // fuso nao pode derrubar a conexao MQTT.
  //
  // Nao bloqueia: anota a configuracao e o loop() desmonta o cliente em etapas. Chamado de
  // dentro do handler HTTP, um `esp_mqtt_client_stop()` direto aqui podia segurar o loop()
  // por ~20 s (tentativa de conexao em andamento num 4G ruim), e passado dos 30 s o
  // LoopWatchdog reinicia a placa — derrubando o AP de todo mundo por causa de um campo
  // de telemetria.
  void applySettings(const RouterSettings& settings);

  void loop(uint32_t nowMs);

 private:
  void sampleIfDue(uint32_t nowMs);
  TelemetrySample takeSample() const;
  void handleConnectionEvents(uint32_t nowMs);
  void maybeConnect(uint32_t nowMs);
  void registerFailure();
  void publishConnectionState(uint32_t nowMs);
  void drain(uint32_t nowMs);

  bool sameBroker(const RouterSettings& settings) const;

  // Desmontagem do cliente para aplicar a configuracao pendente, uma etapa por volta do
  // loop(). Devolve true quando terminou e a configuracao nova ja vale.
  bool advanceTeardown(uint32_t nowMs);
  void reconfigure(const RouterSettings& settings);

  enum class Teardown { Idle, WaitOfflineAck, WaitDisconnect };

  MqttClient& client_;
  Print* log_ = nullptr;

  BatteryVoltageRead batteryVoltage_ = nullptr;
  UplinkStatusRead uplinkStatus_ = nullptr;
  ClockSyncedRead clockSynced_ = nullptr;
  ImageStateRead imageState_ = nullptr;

  // Copia do que o cliente esta usando, para saber se a configuracao nova muda algo.
  bool enabled_ = false;
  String host_;
  uint32_t port_ = 0;
  String user_;
  String password_;
  uint32_t intervalMs_ = 0;
  // Ligada e valida. Telemetria invalida desliga so a telemetria (validateRouting).
  bool active_ = false;

  bool hasPending_ = false;
  RouterSettings pending_;
  Teardown teardown_ = Teardown::Idle;
  uint32_t teardownSinceMs_ = 0;

  bool hasSampled_ = false;
  uint32_t lastSampleMs_ = 0;
  TelemetrySample previousSample_{};

  uint32_t failures_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool attemptInFlight_ = false;
  bool connectionUp_ = false;
  bool connectionStable_ = false;
  uint32_t connectedSinceMs_ = 0;

  // Amostra enviada esperando PUBACK. O ts e o do anel (antes da correcao de relogio): e
  // por ele que se confere, na chegada do PUBACK, que a cabeca do anel ainda e ela.
  bool inFlight_ = false;
  uint32_t inFlightTs_ = 0;
  uint32_t inFlightSinceMs_ = 0;
  // Removidas desde a ultima vez que o anel ficou vazio.
  uint32_t drainedSinceConnect_ = 0;

  bool statusSent_ = false;
  bool infoSent_ = false;
  FirmwareImageState lastInfoState_ = FirmwareImageState::Unknown;
  uint32_t lastImageCheckMs_ = 0;
};
