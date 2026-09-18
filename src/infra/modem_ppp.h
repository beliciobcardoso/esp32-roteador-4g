#pragma once

#include "../domain/router_settings.h"

// INFRA — wrapper fino sobre esp_modem (componente oficial ESP-IDF).
// Faz o power-on do A7670E (PWRKEY) e sobe uma sessao PPPoS com o APN salvo,
// expondo uma interface esp_netif PPP roteavel (nao e relay de comandos AT).
class ModemPpp {
 public:
  // Retorna false se a interface PPP ou o DCE nao inicializarem.
  // Nao bloqueia esperando IP da operadora — isso e assincrono, confirmado via log serial.
  bool start(const RouterSettings& settings);

 private:
  void powerOnSequence();
};
