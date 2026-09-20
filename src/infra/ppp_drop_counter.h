#pragma once

#include <cstdint>

// INFRA — intercepta o log do lwIP e conta, em vez de deixar cada descarte virar uma linha.
//
// O `esp-netif_lwip-ppp` loga uma vez por quadro PPP que nao coube na fila da task tcpip.
// Sob trafego isso tomou 89% do serial numa captura (7179 de 8067 linhas) e levou o UART a
// `HW FIFO Overflow`, o que corta as linhas dos outros modulos ao meio — a leitura periodica
// de bateria saia como `ateria:`, `eria:`, `ria:`. O log parava de servir exatamente sob
// carga, que e quando ele importa (debito 13).
//
// Calar o componente com `esp_log_level_set(..., ESP_LOG_NONE)` resolveria o serial e
// perderia o numero. Aqui a linha e suprimida mas o evento e contado, e quem drena o
// contador publica o total. O numero passa a ser a medicao que o debito 13 pede antes de
// mexer em CONFIG_LWIP_TCPIP_RECVMBOX_SIZE.
//
// Todo o resto do log segue intacto para o vprintf original.
namespace PppDropCounter {

// Instala o hook. Depois do Serial.begin() e antes de o PPP subir — instalado tarde, os
// descartes do inicio da sessao ainda saem como linha solta.
void begin();

// Devolve o acumulado e zera, numa operacao so. Chamado pelo loop(); o incremento vem da
// task do lwIP, entao a troca e atomica — ler e zerar em dois passos perderia os descartes
// que caissem entre eles, justamente nos picos, que e quando a conta interessa.
uint32_t takeCount();

}  // namespace PppDropCounter
