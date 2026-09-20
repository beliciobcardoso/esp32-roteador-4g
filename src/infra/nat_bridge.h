#pragma once

#include <esp_netif.h>

// INFRA — liga o NAPT do lwIP entre o SoftAP e o uplink PPP. O DNS dos clientes nao passa
// por aqui: eles recebem o IP do proprio AP na opcao 6 do DHCP e falam com o DnsForwarder.
class NatBridge {
 public:
  // `uplink` precisa ja ter IP da operadora. Retorna false logando o estagio exato que
  // falhou; uplink sem DNS nao e falha, so limita o forwarder a responder SERVFAIL.
  bool enable(esp_netif_t* uplink);
};
