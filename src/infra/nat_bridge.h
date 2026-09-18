#pragma once

#include <esp_netif.h>

// INFRA — liga o NAPT do lwIP entre o SoftAP e o uplink PPP e entrega o DNS da
// operadora aos clientes do AP pela opcao 6 do DHCP. Sem as duas coisas juntas o
// cliente associa, pega IP e nao chega a lugar nenhum.
class NatBridge {
 public:
  // `uplink` precisa ja ter IP da operadora: e de la que sai o DNS repassado aos
  // clientes. Retorna false logando o estagio exato que falhou.
  bool enable(esp_netif_t* uplink);
};
