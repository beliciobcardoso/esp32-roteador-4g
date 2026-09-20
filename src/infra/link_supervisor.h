#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>

#include "../domain/router_settings.h"
#include "../domain/uplink_status.h"
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
  // Avisada a cada vez que a sessao PPP sobe. Ponteiro de funcao pelo mesmo motivo do
  // http_config_handler: nao ha captura a fazer, e o supervisor nao precisa conhecer quem
  // escuta. Hoje o ouvinte e o relogio, que so sabe sincronizar quando ha rota para fora.
  using UplinkOnline = void (*)();

  LinkSupervisor(ModemPpp& modem, NatBridge& nat);

  // Cria a task e dispara a primeira conexao. Nao bloqueia.
  bool begin(const RouterSettings& settings);

  // Troca a configuracao usada nas proximas tentativas e derruba a sessao atual para
  // reconectar ja com ela. Chamada da task do HTTP; a troca so acontece na task do
  // supervisor, que e a unica dona do modem depois do begin().
  void applySettings(const RouterSettings& settings);

  // Registrado depois da construcao, como os callbacks do handler HTTP: o destino vive
  // noutro global e a ordem de inicializacao entre unidades de traducao nao e garantida.
  // Chamada de dentro da task do supervisor, nao da do loop().
  void onUplinkOnline(UplinkOnline callback) { uplinkOnline_ = callback; }

  // Estado corrente, para a pagina de configuracao. Um acessor so em vez de um por
  // campo: quem le quer a situacao inteira, e devolver as partes soltas ja produziu dois
  // acessores sem chamador (debito 22).
  //
  // Fora de linha porque o orcamento de reinicio vive em variavel de RTC RAM no .cpp.
  //
  // O retorno nao e um instantaneo atomico: os campos sao lidos um a um e a task do
  // supervisor pode avancar no meio. Aceito de proposito — isto alimenta um texto de
  // status que o navegador ja le com atraso, e o pior caso e uma contagem de falhas um
  // ciclo velha. Nada aqui decide nada.
  UplinkStatus status() const;

 private:
  // Falso enquanto houver atualizacao esperando confirmacao do operador. Reiniciar ali
  // faria o bootloader reverter uma imagem que talvez estivesse boa, por falta de sinal —
  // que nao e defeito dela.
  bool mayRebootNow() const;

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

  // Lida pela task do loop() via status(), escrita so pela task do supervisor. Palavras
  // alinhadas, escrita por uma tarefa e lida por outra, sem leitura-modificacao-escrita
  // cruzada — volatile basta, nao precisa de lock.
  volatile UplinkState state_ = UplinkState::Connecting;
  volatile uint32_t consecutiveFailures_ = 0;

  // RouterSettings carrega String, que aloca no heap: copiar sem lock enquanto o HTTP
  // escreve daria leitura de ponteiro liberado. Dai o mutex, que so protege este par.
  SemaphoreHandle_t settingsMutex_ = nullptr;
  RouterSettings pendingSettings_;
  bool settingsDirty_ = false;

  // Copia de trabalho da task. Nunca tocada de fora, logo nao entra no mutex.
  RouterSettings activeSettings_;

  UplinkOnline uplinkOnline_ = nullptr;

  TaskHandle_t task_ = nullptr;
};
