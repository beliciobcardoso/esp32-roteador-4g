#include "telemetry_publisher.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <ctime>

#include "../domain/mqtt_backoff.h"
#include "../domain/telemetry_buffer.h"
#include "ppp_drop_counter.h"

namespace {

// ~4 KB em RTC slow RAM: atravessa reset e o reboot do LinkSupervisor, nao atravessa queda
// de alimentacao (PRD 14, "Onde o buffer mora"). NOINIT porque inicializar zeraria o que se
// quer guardar — quem decide se o conteudo presta e o cabecalho (ringValidateOrReset).
RTC_NOINIT_ATTR TelemetryRing gTelemetryRing;

// Sem PUBACK em 30 s, a amostra volta a ser enviada. O broker nao repete PUBACK de mensagem
// que ja confirmou, entao o pior caso e uma duplicata com o mesmo ts e o mesmo valor — que o
// Prometheus absorve sem criar ponto novo.
const uint32_t kAckTimeoutMs = 30000;

// O estado da imagem vem do otadata, na flash. Conferir a cada volta do loop() seria leitura
// de flash de graca; a cada 10 s o `info` reflete uma confirmacao de OTA quase na hora.
const uint32_t kImageStateCheckMs = 10000;

// Tensao zero e "o ADC ainda nao mediu" (lastBatteryVoltage nasce 0.0f no main.cpp), nao
// bateria morta: placa ligada nao le 0 V. Amostra com 0 V dispararia o alerta de bateria.
const float kNoBatteryReading = 0.0f;

// Prazos da desmontagem. Passado o do PUBACK do `offline`, desconecta assim mesmo: o que
// importa e aplicar a configuracao nova, e sem o `offline` o servidor ainda ve a unidade
// cair pelo Last Will quando a conexao morrer.
const uint32_t kOfflineAckTimeoutMs = 5000;
// Passado este, a conexao ja nao responde, e o esp-mqtt esta parado ou prestes a parar.
const uint32_t kDisconnectTimeoutMs = 15000;

// Amostras drenadas em sequencia, sem o anel esvaziar no meio, a partir das quais a volta a
// zero vira linha no serial. Em regime o anel tem uma ou duas amostras por vez; so backlog
// de verdade (conexao que voltou) merece aparecer, senao a linha sai a cada minuto.
const uint32_t kBacklogReportMin = 3;

uint32_t monotonicSeconds() {
  // esp_timer e 64 bits em microssegundos: nao vira como o millis() em 49 dias.
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

uint16_t saturateU16(uint32_t value) { return value > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(value); }

String firmwareVersion() {
  const esp_app_desc_t* app = esp_ota_get_app_description();
  return app != nullptr ? String(app->version) : String("unknown");
}

}  // namespace

void TelemetryPublisher::begin(const RouterSettings& settings) {
  const bool kept = ringValidateOrReset(gTelemetryRing);
  const uint16_t dropped = ringDiscardUnsynced(gTelemetryRing);
  if (log_ != nullptr) {
    if (kept) {
      log_->printf("Telemetria: anel com %u amostra(s) do boot anterior", ringSize(gTelemetryRing));
      if (dropped > 0) log_->printf(", %u sem relogio descartada(s)", dropped);
      log_->println();
    } else {
      log_->println("Telemetria: anel vazio (power-on ou layout novo)");
    }
  }
  // No boot nao ha cliente ainda, entao aplicar direto nao bloqueia nada.
  intervalMs_ = settings.telemetry_interval_s * 1000u;
  reconfigure(settings);
}

bool TelemetryPublisher::sameBroker(const RouterSettings& settings) const {
  return settings.telemetry_enabled == enabled_ && settings.mqtt_host == host_ &&
         settings.mqtt_port == port_ && settings.mqtt_user == user_ &&
         settings.mqtt_password == password_;
}

void TelemetryPublisher::applySettings(const RouterSettings& settings) {
  // O intervalo vale a quente, sem derrubar a conexao.
  intervalMs_ = settings.telemetry_interval_s * 1000u;
  if (sameBroker(settings) && (active_ || !settings.telemetry_enabled)) {
    // Configuracao voltou a ser a que o cliente ja usa, no meio de uma desmontagem.
    switch (teardown_) {
      case Teardown::Idle:
        hasPending_ = false;
        return;
      case Teardown::WaitOfflineAck:
        // O `offline` ja foi publicado e a conexao segue de pe: sem republicar `online`, a
        // unidade ficaria "offline" retida no painel enquanto publica normalmente.
        hasPending_ = false;
        teardown_ = Teardown::Idle;
        statusSent_ = false;
        return;
      case Teardown::WaitDisconnect:
        // O DISCONNECT ja foi pedido e nao se desfaz: termina a desmontagem e reconecta com
        // a mesma configuracao.
        pending_ = settings;
        return;
    }
  }
  pending_ = settings;
  hasPending_ = true;
  teardown_ = Teardown::Idle;
}

bool TelemetryPublisher::advanceTeardown(uint32_t nowMs) {
  // Sem task do esp-mqtt, o end() nao bloqueia: nada a desmontar.
  if (!client_.hasEverConnected()) {
    reconfigure(pending_);
    return true;
  }

  switch (teardown_) {
    case Teardown::Idle:
      // Tentativa em andamento: a task do esp-mqtt segura o lock ate ela acabar, e o end()
      // esperaria junto. Espera a borda de conexao ou de queda, sem travar o loop().
      if (attemptInFlight_) return false;
      if (client_.isConnected()) {
        // Sem este `offline`, trocar o codigo da unidade deixaria o codigo antigo "online"
        // retido para sempre: DISCONNECT limpo nao dispara o Last Will.
        const int msgId = client_.enqueue(unitTopic(user_, kTopicStatus), kStatusOffline, 1, true);
        if (msgId >= 0) client_.awaitAck(msgId);
        teardown_ = Teardown::WaitOfflineAck;
        teardownSinceMs_ = nowMs;
        return false;
      }
      reconfigure(pending_);
      return true;

    case Teardown::WaitOfflineAck:
      if (client_.takeAck() || !client_.isConnected() ||
          nowMs - teardownSinceMs_ >= kOfflineAckTimeoutMs) {
        client_.requestDisconnect();
        teardown_ = Teardown::WaitDisconnect;
        teardownSinceMs_ = nowMs;
      }
      return false;

    case Teardown::WaitDisconnect:
      if (client_.isConnected() && nowMs - teardownSinceMs_ < kDisconnectTimeoutMs) return false;
      reconfigure(pending_);
      return true;
  }
  return false;
}

void TelemetryPublisher::reconfigure(const RouterSettings& settings) {
  hasPending_ = false;
  teardown_ = Teardown::Idle;
  client_.end();
  enabled_ = settings.telemetry_enabled;
  host_ = settings.mqtt_host;
  port_ = settings.mqtt_port;
  user_ = settings.mqtt_user;
  password_ = settings.mqtt_password;

  active_ = false;
  failures_ = 0;
  backoffMs_ = 0;
  attemptInFlight_ = false;
  connectionUp_ = false;
  connectionStable_ = false;
  inFlight_ = false;
  statusSent_ = false;
  infoSent_ = false;

  if (!enabled_) {
    if (log_ != nullptr) log_->println("Telemetria: desligada");
    return;
  }

  const SettingsValidationError invalid = validateTelemetry(settings);
  if (invalid != SettingsValidationError::None) {
    // So a telemetria para. O roteamento subiu com validateRouting(), e e por ele que se
    // chega na pagina para corrigir isto.
    if (log_ != nullptr) log_->printf("Telemetria: configuracao invalida (%s) — nao vai conectar\n", to_string(invalid));
    return;
  }

  MqttClient::Config config;
  config.uri = String("mqtts://") + host_ + ":" + String(port_);
  config.clientId = user_;
  config.username = user_;
  config.password = password_;
  config.willTopic = unitTopic(user_, kTopicStatus);
  if (!client_.begin(config)) {
    if (log_ != nullptr) log_->println("Telemetria: NAO foi possivel criar o cliente MQTT");
    return;
  }

  active_ = true;
  // A senha nunca vai para o log (criterio 7): host, porta e codigo bastam para diagnostico.
  if (log_ != nullptr) {
    log_->printf("Telemetria: ligada, broker %s:%u como \"%s\", a cada %u s\n", host_.c_str(),
                 port_, user_.c_str(), settings.telemetry_interval_s);
  }
}

void TelemetryPublisher::loop(uint32_t nowMs) {
  if (hasPending_) {
    handleConnectionEvents(nowMs);
    if (!advanceTeardown(nowMs)) return;
  }
  if (!active_) return;

  sampleIfDue(nowMs);
  handleConnectionEvents(nowMs);

  if (!client_.isConnected()) {
    maybeConnect(nowMs);
    return;
  }
  publishConnectionState(nowMs);
  drain(nowMs);
}

TelemetrySample TelemetryPublisher::takeSample() const {
  TelemetrySample sample{};

  const bool synced = clockSynced_ != nullptr && clockSynced_();
  if (synced) {
    sample.ts = static_cast<uint32_t>(time(nullptr));
  } else {
    // Sem relogio, guarda o instante monotonico e marca: a correcao para epoch acontece na
    // drenagem, quando ja houver hora (correctSampleTimestamp). Gravar 0 poria a amostra em
    // 1970; descartar perderia justamente os primeiros minutos de quem reiniciou.
    sample.ts = monotonicSeconds();
    sample.flags |= kTelemetryFlagClockUnsynced;
  }

  const float volts = batteryVoltage_ != nullptr ? batteryVoltage_() : kNoBatteryReading;
  sample.battery_mv = saturateU16(static_cast<uint32_t>(volts * 1000.0f + 0.5f));
  sample.free_heap_kb = saturateU16(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u);
  sample.uptime_min = saturateU16(monotonicSeconds() / 60u);
  sample.ppp_drops_total = PppDropCounter::totalCount();

  const UplinkStatus uplink = uplinkStatus_ != nullptr ? uplinkStatus_() : UplinkStatus{};
  sample.uplink_state = encodeUplinkState(uplink.state);
  if (uplink.rebooted_for_uplink) sample.flags |= kTelemetryFlagRebootedForUplink;
  if (uplink.reboot_budget_exhausted) sample.flags |= kTelemetryFlagRebootBudgetExhausted;
  return sample;
}

void TelemetryPublisher::sampleIfDue(uint32_t nowMs) {
  if (batteryVoltage_ == nullptr || batteryVoltage_() <= kNoBatteryReading) return;

  const TelemetrySample current = takeSample();
  if (hasSampled_ &&
      !shouldPublishTelemetry(nowMs, lastSampleMs_, intervalMs_, previousSample_, current)) {
    return;
  }
  ringPush(gTelemetryRing, current);
  previousSample_ = current;
  lastSampleMs_ = nowMs;
  hasSampled_ = true;
}

void TelemetryPublisher::registerFailure() {
  ++failures_;
  backoffMs_ = applyJitter(nextBackoffMs(failures_), esp_random());
}

void TelemetryPublisher::handleConnectionEvents(uint32_t nowMs) {
  if (client_.takeConnected()) {
    attemptInFlight_ = false;
    connectionUp_ = true;
    connectionStable_ = false;
    connectedSinceMs_ = nowMs;
    inFlight_ = false;
    statusSent_ = false;
    infoSent_ = false;
    drainedSinceConnect_ = 0;
    // A linha do criterio 9: heap interno com o TLS de pe. O minimo desde o boot inclui o
    // pico do handshake que acabou de acontecer.
    if (log_ != nullptr) {
      log_->printf("MQTT: conectado a %s:%u como \"%s\" | anel %u amostra(s) | heap interno livre %u B, minimo desde o boot %u B\n",
                   host_.c_str(), port_, user_.c_str(), ringSize(gTelemetryRing),
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    }
  }

  if (client_.takeDisconnected()) {
    const bool wasUp = connectionUp_;
    attemptInFlight_ = false;
    connectionUp_ = false;
    inFlight_ = false;
    if (hasPending_) {
      // Queda pedida pela desmontagem: nao e falha, e a proxima tentativa ja e do cliente novo.
      if (log_ != nullptr) log_->println("MQTT: desconectado para aplicar a configuracao nova");
      return;
    }
    registerFailure();
    if (log_ != nullptr) {
      log_->printf("MQTT: %s (tls 0x%x, connack %d) — nova tentativa em %u s, falhas seguidas %u\n",
                   wasUp ? "desconectado" : "tentativa falhou", client_.lastTransportError(),
                   client_.lastConnectReturnCode(), static_cast<unsigned>(backoffMs_ / 1000u),
                   static_cast<unsigned>(failures_));
    }
  }

  // Backoff so volta ao inicio depois de a conexao durar: um enlace que sobe e cai em dois
  // segundos zeraria a espera a cada ciclo, e a protecao viraria a tempestade que ela evita.
  if (connectionUp_ && !connectionStable_ && connectionIsStable(nowMs, connectedSinceMs_)) {
    connectionStable_ = true;
    failures_ = 0;
    backoffMs_ = 0;
  }
}

void TelemetryPublisher::maybeConnect(uint32_t nowMs) {
  if (attemptInFlight_) return;
  const bool uplinkOnline =
      uplinkStatus_ != nullptr && uplinkStatus_().state == UplinkState::Online;
  if (!shouldAttemptConnect(nowMs, lastAttemptMs_, backoffMs_, uplinkOnline)) return;

  lastAttemptMs_ = nowMs;
  if (client_.connect()) {
    attemptInFlight_ = true;
    return;
  }
  // O cliente recusou o pedido (ainda nao parado depois da queda anterior). Conta como
  // falha para a proxima tentativa ter espera, e nao virar uma chamada por volta do loop().
  registerFailure();
}

void TelemetryPublisher::publishConnectionState(uint32_t nowMs) {
  if (!statusSent_) {
    // Retido, QoS 1: substitui o `offline` que o Last Will deixou na ultima queda.
    statusSent_ = client_.enqueue(unitTopic(user_, kTopicStatus), kStatusOnline, 1, true) >= 0;
  }

  if (imageState_ == nullptr) return;
  if (infoSent_ && nowMs - lastImageCheckMs_ < kImageStateCheckMs) return;
  lastImageCheckMs_ = nowMs;

  // A cada conexao e a cada mudanca: sem republicar depois da confirmacao, a unidade
  // ficaria "OTA pendente" no painel ate reconectar.
  const FirmwareImageState state = imageState_();
  if (infoSent_ && state == lastInfoState_) return;
  if (client_.enqueue(unitTopic(user_, kTopicInfo), buildInfoPayload(firmwareVersion(), state), 1, true) < 0) {
    return;
  }
  lastInfoState_ = state;
  infoSent_ = true;
}

void TelemetryPublisher::drain(uint32_t nowMs) {
  if (inFlight_) {
    if (client_.takeAck()) {
      TelemetrySample head{};
      // Com o anel cheio, uma amostra nova pode ter passado por cima da que estava em voo.
      // So remove se a cabeca ainda e a enviada; senao a nova seria perdida sem ter saido.
      if (ringPeekOldest(gTelemetryRing, head) && head.ts == inFlightTs_) {
        ringPopOldest(gTelemetryRing, head);
        ++drainedSinceConnect_;
      }
      inFlight_ = false;
      if (ringIsEmpty(gTelemetryRing)) {
        if (drainedSinceConnect_ >= kBacklogReportMin && log_ != nullptr) {
          log_->printf("MQTT: anel drenado, %u amostra(s) acumulada(s) enviada(s)\n",
                       static_cast<unsigned>(drainedSinceConnect_));
        }
        drainedSinceConnect_ = 0;
      }
    } else if (nowMs - inFlightSinceMs_ >= kAckTimeoutMs) {
      inFlight_ = false;  // reenvia a mesma amostra na volta seguinte
      return;
    } else {
      return;
    }
  }

  TelemetrySample head{};
  if (!ringPeekOldest(gTelemetryRing, head)) return;

  TelemetrySample outgoing = head;
  const bool synced = clockSynced_ != nullptr && clockSynced_();
  switch (decideDrain(outgoing, synced, monotonicSeconds(), static_cast<uint32_t>(time(nullptr)))) {
    case DrainAction::Wait:
      return;

    case DrainAction::Discard:
      ringPopOldest(gTelemetryRing, head);
      if (log_ != nullptr) log_->println("MQTT: amostra sem data possivel descartada do anel");
      return;

    case DrainAction::Publish: {
      const int msgId = client_.enqueue(unitTopic(user_, kTopicTelemetry),
                                        buildTelemetryPayload(outgoing), 1, false);
      if (msgId < 0) return;
      client_.awaitAck(msgId);
      inFlight_ = true;
      inFlightTs_ = head.ts;
      inFlightSinceMs_ = nowMs;
      return;
    }
  }
}
