#pragma once

#include <esp_netif.h>

#include <cstdint>

#include "../domain/modem_identity.h"
#include "../domain/router_settings.h"

// Workaround: esp_modem_api.h usa `PdpContext` numa declaracao de funcao antes de
// qualquer forward-declare do tipo. Aqui so precisamos do ponteiro opaco do DCE, entao
// declaramos os dois como tipos incompletos e mantemos o header do componente fora deste
// arquivo — quem inclui modem_ppp.h nao deveria herdar esse problema.
struct PdpContext;
struct esp_modem_dce_wrap;
typedef struct esp_modem_dce_wrap esp_modem_dce_t;

// INFRA — wrapper fino sobre esp_modem (componente oficial ESP-IDF).
// Faz o power-on do A7670E (PWRKEY) e sobe uma sessao PPPoS com o APN salvo,
// expondo uma interface esp_netif PPP roteavel (nao e relay de comandos AT).
class ModemPpp {
 public:
  // Retorna false se a interface PPP ou o DCE nao inicializarem.
  // Nao bloqueia esperando IP da operadora — isso e assincrono; usar waitForIp().
  bool start(const RouterSettings& settings);

  // Destroi DCE e netif, deixando o objeto no mesmo estado de antes do primeiro start().
  // Pre-requisito de qualquer reconexao: o modem pode ter travado em modo de dados, e so
  // o reset por hardware do proximo start() sai disso de forma confiavel.
  void stop();

  // Bloqueia ate o IP_EVENT_PPP_GOT_IP ou o timeout. Precisa existir porque o DNS
  // da operadora so chega no IPCP, e o NAT depende dele pra configurar o DHCP do AP.
  bool waitForIp(uint32_t timeoutMs);

  // Estado corrente do enlace, alimentado pelos eventos do esp_netif_ppp. Vira false no
  // PPP_LOST_IP e em qualquer NETIF_PPP_STATUS de erro — inclusive o ERRORPEERDEAD que o
  // LCP echo levanta quando a operadora some sem avisar.
  bool hasIp() const;

  // Interface PPP criada em start(). Nula antes dele ou se ele falhou.
  esp_netif_t* netif() const { return netif_; }

  // Modelo e firmware do modem, lidos uma vez por boot no primeiro start(), ainda em modo
  // comando. Campos vazios se o modem nao respondeu ao AT+SIMCOMATI — a leitura nao impede
  // o enlace de subir.
  const ModemIdentity& identity() const { return identity_; }

 private:
  void powerOnSequence();
  void readIdentityOnce();

  esp_netif_t* netif_ = nullptr;
  esp_modem_dce_t* dce_ = nullptr;
  ModemIdentity identity_;
  bool identityRead_ = false;
};
