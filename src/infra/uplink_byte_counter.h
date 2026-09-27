#pragma once

#include <esp_netif.h>

#include <cstdint>

// INFRA — bytes que passam pelo enlace 4G desde o boot, nos dois sentidos. E a medicao do
// custo de dado da unidade (PRD 14, criterio 8), publicada como counter na telemetria.
//
// Onde conta: nas duas funcoes publicas por onde o PPP fala com o modem —
// `esp_netif_receive()` (UART -> PPP) e `esp_netif_transmit()` (PPP -> UART) —, pelo
// `--wrap` do linker (`build_flags` do `platformio.ini`). O wrapper compara o ponteiro da
// netif, soma e chama a original. Os caminhos descartados, e por que:
//
//   - trocar `netif->input` da netif do lwIP: o PPP entrega o pacote chamando `ip4_input()`
//     direto (`ppp.c`), sem passar por ele, e o RX contaria zero;
//   - os contadores MIB2 que o proprio PPP do lwIP ja mantem (`ifinoctets`/`ifoutoctets`):
//     exigem compilar o lwIP com `MIB2_STATS=1`, o que muda o layout da `struct netif` num
//     binario que tem os blobs do Wi-Fi — divergencia de layout vira corrupcao de memoria;
//   - contar dentro do `esp_modem`: e componente gerenciado, regravado a cada atualizacao.
//
// A unidade e o byte do enlace serial PPP, ja com o enquadramento HDLC e o escape, e com o
// LCP echo. Fica alguns por cento ACIMA dos bytes IP que a operadora cobra — para estimar
// custo, o lado seguro. Inclui o trafego que o NAT repassa dos clientes do AP: sem cliente
// associado, e o custo da propria unidade.
//
// O `--wrap` so pega chamada entre arquivos objeto diferentes. As duas daqui vem de fora
// (`esp_netif_lwip_ppp.c` e `esp_modem_netif.cpp`), mas se uma atualizacao da IDF passar a
// chama-las do mesmo arquivo em que sao definidas, o contador para de contar SEM ERRO de
// build. Zero bytes com o uplink online e o sintoma.
namespace UplinkByteCounter {

// Qual netif contar. Chamar a cada sessao PPP, com a netif nova — o `ModemPpp` destroi e
// recria a dele a cada tentativa. Os totais nao zeram: acumulam desde o boot.
void watch(esp_netif_t* uplink);

uint32_t rxBytes();
uint32_t txBytes();

}  // namespace UplinkByteCounter
