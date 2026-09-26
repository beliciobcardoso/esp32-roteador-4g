#pragma once

#include <Arduino.h>
#include <mqtt_client.h>

#include <atomic>

// INFRA — casca fina sobre o `esp_mqtt_client` da IDF 4.4. Nao decide nada: quando tentar,
// o que publicar e quando remover do anel sao do TelemetryPublisher e do dominio.
//
// Tres escolhas de configuracao que nao sao detalhe (PRD 14):
// - auto-reconnect DESLIGADO. O do esp-mqtt tenta a cada 10 s fixos sem saber se ha rota;
//   quem sabe e o LinkSupervisor, e a tentativa passa a ser comandada pelo backoff do
//   dominio. Com ele desligado, o cliente fica parado depois de uma queda ate `connect()`
//   chamar `esp_mqtt_client_reconnect()` (`MQTT_STATE_WAIT_RECONNECT` em mqtt_client.c)
// - confianca pelo pacote de CAs publicas (`esp_crt_bundle`), nao por PEM embutido: a
//   renovacao do certificado do broker nao exige regravar a frota
// - `enqueue`, nunca `publish`. O `publish` bloqueia o chamador ate a rede responder, e o
//   chamador e o loop() — a pagina de configuracao congelaria junto
//
// Os eventos chegam na task do esp-mqtt, nao na do loop(). O handler so escreve atomicos;
// quem le e age e o loop(). Nada de String nem de anel dentro do handler.
class MqttClient {
 public:
  struct Config {
    String uri;       // mqtts://host:porta
    String clientId;  // o codigo da unidade: duas placas com o mesmo codigo derrubam uma a outra
    String username;
    String password;
    String willTopic;
  };

  ~MqttClient();

  // Cria o cliente com o Last Will armado. Nao conecta.
  bool begin(const Config& config);

  // Destroi o cliente. Seguro sem begin(). O Last Will NAO sai: o broker so o publica
  // quando a conexao morre sem DISCONNECT, e aqui o DISCONNECT e enviado.
  //
  // PODE BLOQUEAR. O `esp_mqtt_client_stop()` espera o lock do cliente, que a task do
  // esp-mqtt segura durante uma tentativa inteira de conexao (transporte + TLS + CONNACK,
  // ate ~20 s), e com o cliente conectado ele manda o DISCONNECT sincrono pela rede. So e
  // rapido (<= reconnect_timeout_ms / 2) com o cliente parado depois de uma queda. Quem
  // chama do loop() tem que garantir esse estado antes — ver TelemetryPublisher, desmontagem.
  void end();

  bool isStarted() const { return client_ != nullptr; }
  // A task do esp-mqtt ja foi criada por um connect(). Antes disso end() nao bloqueia.
  bool hasEverConnected() const { return everStarted_; }

  // Pede o DISCONNECT sem esperar: a task do esp-mqtt manda e despacha a queda depois.
  void requestDisconnect();

  // Uma tentativa. A primeira e o `start()`, que ja conecta; as seguintes sao
  // `reconnect()`, que so vale com o cliente parado depois de uma queda. Devolve false se o
  // cliente nao aceitou o pedido — por exemplo, com uma tentativa ainda em andamento.
  bool connect();

  bool isConnected() const { return connected_.load(); }

  // Bordas, cada uma entregue uma vez. A queda cobre tanto a conexao que caiu quanto a
  // tentativa que falhou: o esp-mqtt despacha MQTT_EVENT_DISCONNECTED nos dois casos.
  bool takeConnected() { return connectedEdge_.exchange(false); }
  bool takeDisconnected() { return disconnectedEdge_.exchange(false); }

  // Poe a mensagem na fila do cliente e devolve o msg_id, ou -1. `store` sempre ligado: sem
  // ele o esp-mqtt descarta QoS 0 que nao da para mandar na hora.
  int enqueue(const String& topic, const String& payload, int qos, bool retain);

  // Passa a vigiar o PUBACK deste msg_id. Uma mensagem vigiada por vez, que e o que a
  // drenagem do anel precisa.
  //
  // O PUBACK pode chegar antes desta chamada: o enqueue() devolve o id e a task do esp-mqtt
  // ja pode ter mandado e recebido a confirmacao. Por isso o handler guarda os ultimos ids
  // confirmados, e esta funcao olha ali antes de passar a esperar.
  void awaitAck(int msgId);

  // O PUBACK da mensagem vigiada chegou? Entregue uma vez.
  bool takeAck() { return acked_.exchange(false); }

  // Ultimo erro de TLS/socket da tentativa que falhou, para o log. Zero se nao houve.
  int lastTransportError() const { return lastTransportError_.load(); }
  // Codigo de retorno do CONNACK recusado (5 = nao autorizado). Zero se nao houve.
  int lastConnectReturnCode() const { return lastConnectReturnCode_.load(); }

 private:
  static void onEvent(void* handlerArgs, esp_event_base_t base, int32_t eventId, void* eventData);
  void handle(esp_mqtt_event_handle_t event);

  esp_mqtt_client_handle_t client_ = nullptr;
  bool everStarted_ = false;

  std::atomic<bool> connected_{false};
  std::atomic<bool> connectedEdge_{false};
  std::atomic<bool> disconnectedEdge_{false};
  std::atomic<int> awaitedMsgId_{-1};
  std::atomic<bool> acked_{false};
  // Ultimos msg_ids com PUBACK. Quatro cobre o que pode estar em voo ao mesmo tempo: o
  // status e o info da conexao e uma amostra.
  static constexpr int kRecentAcks = 4;
  std::atomic<int> recentAcks_[kRecentAcks] = {{-1}, {-1}, {-1}, {-1}};
  std::atomic<unsigned> recentAckNext_{0};
  std::atomic<int> lastTransportError_{0};
  std::atomic<int> lastConnectReturnCode_{0};
};
