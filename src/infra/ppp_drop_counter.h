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
// Ha uma so contagem, e ela nunca zera; a janela e derivada dela por subtracao. Dois
// contadores independentes — um de janela, um acumulado — dariam dois valores capazes de
// divergir, e ainda custariam dois incrementos no caminho de recepcao do lwIP, que e o
// lugar mais quente onde este codigo roda.
//
// Todo o resto do log segue intacto para o vprintf original.
namespace PppDropCounter {

// Instala o hook. Depois do Serial.begin() e antes de o PPP subir — instalado tarde, os
// descartes do inicio da sessao ainda saem como linha solta.
void begin();

// Quantos descartes desde a chamada anterior. E o que a linha de serial publica a cada
// janela de 30 s.
//
// **Um consumidor so.** O acumulado e atomico porque o incremento vem da task do lwIP, mas
// a marca da ultima drenagem e simples e pertence a quem drena — hoje o loop(). Chamar isto
// de duas tasks faria as duas verem deltas parciais. Nao ha segundo chamador, e criar
// sincronizacao para um que nao existe seria proteger contra o improvavel pagando no
// provavel.
uint32_t takeCount();

// Descartes desde o boot, sem zerar nunca. E a forma que a telemetria publica (PRD 14),
// porque counter cumulativo e o que o `rate()` do Prometheus sabe ler.
//
// Janela publicada como counter nao serve: mensagem MQTT perdida ou unidade offline tornam
// o delta um erro permanente na soma, enquanto o cumulativo se autocorrige — amostra
// perdida so faz o `rate()` interpolar o trecho. A volta a zero de um reboot o Prometheus
// ja trata como reset de counter.
//
// Vira em 2^32, e a subtracao de takeCount() atravessa a virada sem perder nada.
uint32_t totalCount();

}  // namespace PppDropCounter
