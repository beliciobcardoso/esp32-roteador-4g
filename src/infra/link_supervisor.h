#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>

#include "../domain/router_settings.h"
#include "modem_ppp.h"
#include "nat_bridge.h"

// INFRA — mantem o uplink 4G de pe sem intervencao: conecta no boot, detecta queda pelos
// eventos do PPP e reconecta com backoff, rearmando o NAT a cada sessao nova.
//
// Vive numa task propria de proposito. A sequencia de conexao bloqueia por ate ~160s no
// pior caso (power-on + sync AT + SIM + registro + IPCP); rodando dentro do loop() isso
// congelaria a pagina de configuracao justamente quando o usuario quer abrir ela pra
// descobrir por que a internet caiu.
class LinkSupervisor {
 public:
  enum class State {
    Connecting,  // tentativa em andamento
    Online,      // PPP com IP e NAT armado
    Backoff,     // esperando a proxima tentativa
  };

  LinkSupervisor(ModemPpp& modem, NatBridge& nat);

  // Cria a task e dispara a primeira conexao. Nao bloqueia.
  bool begin(const RouterSettings& settings);

  // Troca a configuracao usada nas proximas tentativas e derruba a sessao atual para
  // reconectar ja com ela. Chamada da task do HTTP; a troca so acontece na task do
  // supervisor, que e a unica dona do modem depois do begin().
  void applySettings(const RouterSettings& settings);

  State state() const { return state_; }

  // Tentativas malsucedidas seguidas. Zera a cada conexao bem-sucedida.
  uint32_t consecutiveFailures() const { return consecutiveFailures_; }

 private:
  static void taskEntry(void* context);
  void run();
  bool connectOnce(const RouterSettings& settings);
  bool refreshSettings();
  // Espera `delayMs` em fatias, voltando antes se chegar configuracao nova. Sem isso um
  // APN corrigido pela pagina de config esperaria ate 60s de backoff pra ser tentado, e a
  // resposta HTTP que diz "reconectando" viraria mentira.
  bool waitInterruptible(uint32_t delayMs);

  ModemPpp& modem_;
  NatBridge& nat_;

  // Lida pela task do loop() via state()/consecutiveFailures(), escrita so pela task do
  // supervisor. Palavras alinhadas, escrita por uma tarefa e lida por outra, sem
  // leitura-modificacao-escrita cruzada — volatile basta, nao precisa de lock.
  volatile State state_ = State::Connecting;
  volatile uint32_t consecutiveFailures_ = 0;

  // RouterSettings carrega String, que aloca no heap: copiar sem lock enquanto o HTTP
  // escreve daria leitura de ponteiro liberado. Dai o mutex, que so protege este par.
  SemaphoreHandle_t settingsMutex_ = nullptr;
  RouterSettings pendingSettings_;
  bool settingsDirty_ = false;

  // Copia de trabalho da task. Nunca tocada de fora, logo nao entra no mutex.
  RouterSettings activeSettings_;

  TaskHandle_t task_ = nullptr;
};
