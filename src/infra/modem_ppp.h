#pragma once

#include <esp_netif.h>

#include <cstdint>

#include "../domain/router_settings.h"

// INFRA — wrapper fino sobre esp_modem (componente oficial ESP-IDF).
// Faz o power-on do A7670E (PWRKEY) e sobe uma sessao PPPoS com o APN salvo,
// expondo uma interface esp_netif PPP roteavel (nao e relay de comandos AT).
class ModemPpp {
 public:
  // Retorna false se a interface PPP ou o DCE nao inicializarem.
  // Nao bloqueia esperando IP da operadora — isso e assincrono; usar waitForIp().
  bool start(const RouterSettings& settings);

  // Bloqueia ate o IP_EVENT_PPP_GOT_IP ou o timeout. Precisa existir porque o DNS
  // da operadora so chega no IPCP, e o NAT depende dele pra configurar o DHCP do AP.
  bool waitForIp(uint32_t timeoutMs);

  // Interface PPP criada em start(). Nula antes dele ou se ele falhou.
  esp_netif_t* netif() const { return netif_; }

 private:
  void powerOnSequence();

  esp_netif_t* netif_ = nullptr;
};
