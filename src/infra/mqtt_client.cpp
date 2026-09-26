#include "mqtt_client.h"

#include <esp_crt_bundle.h>

#include "../domain/telemetry.h"

namespace {

// 60 e nao os 120 do default do esp-mqtt. O broker declara a placa morta em 1,5 x keepalive
// e so entao publica o Last Will: a 120 seriam 180 s, e o criterio 6 do PRD 14 pede
// `offline` em ate 2 min. O broker tambem recusa no CONNECT keepalive acima de 60
// (`max_keepalive` do mosquitto.conf do servidor).
const int kKeepaliveS = 60;

// Buffers de entrada e saida do protocolo. O payload de telemetria tem ~300 B e o broker
// recusa acima de 4 KB; 1 KB cobre o topico mais o payload com folga sem pesar no heap.
const int kBufferSize = 1024;

// Com o auto-reconnect desligado, este valor so serve para uma coisa: o intervalo em que a
// task do esp-mqtt, parada depois de uma queda, acorda para conferir se pediram tentativa
// ou parada (espera `reconnect_timeout_ms / 2`). E o que limita quanto o end() bloqueia
// nesse estado. 500 ms da 250 ms de pior caso, contra 5 s com o default de 10 s.
const int kIdlePollMs = 500;

// Teto de cada operacao de rede do esp-mqtt (conexao TCP + TLS, escrita). O end() com o
// cliente conectado espera a operacao em curso e manda o DISCONNECT, e numa volta ruim da
// task podem se somar tres escritas: 3 x 8 s fica abaixo dos 30 s do LoopWatchdog, com o
// default de 10 s nao ficaria. Abaixo de 8 s a conexao em 4G ruim comeca a falhar por
// prazo: medida em bancada, a primeira levou 4 s do begin() ao CONNACK.
const int kNetworkTimeoutMs = 8000;

}  // namespace

MqttClient::~MqttClient() { end(); }

bool MqttClient::begin(const Config& config) {
  end();

  esp_mqtt_client_config_t cfg = {};
  cfg.uri = config.uri.c_str();
  cfg.client_id = config.clientId.c_str();
  cfg.username = config.username.c_str();
  cfg.password = config.password.c_str();
  cfg.lwt_topic = config.willTopic.c_str();
  cfg.lwt_msg = kStatusOffline;
  cfg.lwt_qos = 1;
  cfg.lwt_retain = 1;
  cfg.keepalive = kKeepaliveS;
  cfg.disable_auto_reconnect = true;
  cfg.reconnect_timeout_ms = kIdlePollMs;
  cfg.network_timeout_ms = kNetworkTimeoutMs;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  cfg.buffer_size = kBufferSize;
  cfg.out_buffer_size = kBufferSize;

  // O esp_mqtt_set_config() copia as strings (strdup), entao `config` pode morrer depois.
  client_ = esp_mqtt_client_init(&cfg);
  if (client_ == nullptr) return false;

  if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttClient::onEvent, this) != ESP_OK) {
    end();
    return false;
  }
  everStarted_ = false;
  return true;
}

void MqttClient::end() {
  if (client_ == nullptr) return;
  // stop() antes de destroy(): o destroy com a task de pe espera o fim dela de qualquer
  // jeito, mas o stop() e quem manda o DISCONNECT — sem ele o broker publicaria o Last Will
  // de uma unidade que so trocou de configuracao.
  if (everStarted_) esp_mqtt_client_stop(client_);
  esp_mqtt_client_destroy(client_);
  client_ = nullptr;
  everStarted_ = false;
  connected_ = false;
  connectedEdge_ = false;
  disconnectedEdge_ = false;
  awaitedMsgId_ = -1;
  acked_ = false;
  for (auto& id : recentAcks_) id = -1;
}

bool MqttClient::connect() {
  if (client_ == nullptr) return false;
  lastTransportError_ = 0;
  lastConnectReturnCode_ = 0;
  if (!everStarted_) {
    if (esp_mqtt_client_start(client_) != ESP_OK) return false;
    everStarted_ = true;
    return true;
  }
  return esp_mqtt_client_reconnect(client_) == ESP_OK;
}

int MqttClient::enqueue(const String& topic, const String& payload, int qos, bool retain) {
  if (client_ == nullptr) return -1;
  return esp_mqtt_client_enqueue(client_, topic.c_str(), payload.c_str(),
                                 static_cast<int>(payload.length()), qos, retain ? 1 : 0, true);
}

void MqttClient::requestDisconnect() {
  if (client_ != nullptr) esp_mqtt_client_disconnect(client_);
}

void MqttClient::awaitAck(int msgId) {
  acked_ = false;
  awaitedMsgId_ = msgId;
  // Depois de publicar o id vigiado, e nao antes: um PUBACK que chegue agora ve o id e liga
  // acked_ pelo handler; um que ja tinha chegado esta no historico.
  for (const auto& id : recentAcks_) {
    if (id.load() == msgId) {
      awaitedMsgId_ = -1;
      acked_ = true;
      return;
    }
  }
}

void MqttClient::onEvent(void* handlerArgs, esp_event_base_t, int32_t, void* eventData) {
  static_cast<MqttClient*>(handlerArgs)->handle(static_cast<esp_mqtt_event_handle_t>(eventData));
}

void MqttClient::handle(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
      connected_ = true;
      connectedEdge_ = true;
      break;

    case MQTT_EVENT_DISCONNECTED:
      connected_ = false;
      disconnectedEdge_ = true;
      break;

    case MQTT_EVENT_PUBLISHED:
      recentAcks_[recentAckNext_.fetch_add(1) % kRecentAcks] = event->msg_id;
      if (event->msg_id == awaitedMsgId_.load()) {
        awaitedMsgId_ = -1;
        acked_ = true;
      }
      break;

    // MQTT_EVENT_DELETED nao e tratado: so e despachado com
    // CONFIG_MQTT_REPORT_DELETED_MESSAGES, desligado neste sdkconfig. Mensagem que expira no
    // outbox e coberta pelo prazo de PUBACK do TelemetryPublisher.

    case MQTT_EVENT_ERROR:
      if (event->error_handle == nullptr) break;
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        lastTransportError_ = event->error_handle->esp_tls_last_esp_err;
      } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        lastConnectReturnCode_ = event->error_handle->connect_return_code;
      }
      break;

    default:
      break;
  }
}
