#pragma once

#include <cstdint>

#include "string_type.h"

// ENTIDADE — o que a placa diz sobre pacote descartado no uplink. Regra pura: nao conhece
// lwIP, esp_log nem heap_caps.
//
// Existe por causa do debito 13. O lwIP loga uma linha por quadro PPP que nao coube na fila
// da task tcpip, e sob trafego isso tomou 89% do serial numa captura (7179 de 8067 linhas),
// junto de `HW FIFO Overflow` cortando as linhas dos outros modulos ao meio. O log parava de
// ser confiavel exatamente quando havia carga — que e quando se quer olhar para ele.
//
// A saida nao e calar o componente: `esp_log_level_set(..., ESP_LOG_NONE)` esconderia o
// sintoma e continuaria descartando pacote. O que este arquivo descreve e a agregacao —
// contar os descartes da janela e dizer o total numa linha so. O numero deixa de afogar o
// serial e passa a ser justamente a medicao que o debito 13 pede antes de mexer em
// CONFIG_LWIP_TCPIP_RECVMBOX_SIZE.

// Janela de agregacao. 30 s e longo o bastante para uma rajada de download inteira caber
// numa linha, e curto o bastante para o numero ainda se referir ao que estava acontecendo
// quando quem le olhou para a tela.
extern const unsigned long kDropReportIntervalMs;

// Piso de heap interno abaixo do qual a leitura deixa de ser confortavel. Nao e o ponto em
// que o lwIP falha — e o ponto a partir do qual nao da mais para afirmar que o descarte veio
// da fila, que e a distincao que o debito 13 precisa fechar.
extern const uint32_t kTightInternalHeapBytes;

// Ha relatorio a emitir? So com a janela fechada E algum descarte na conta: janela silenciosa
// nao gera linha, senao a agregacao viraria a mesma poluicao, mais lenta.
//
// A conta e por subtracao de unsigned de proposito, para atravessar a virada de millis()
// (~49 dias). Comparacao direta de instantes prenderia o relatorio ate a volta seguinte.
bool dropReportDue(unsigned long now, unsigned long lastReportMs, uint32_t dropsInWindow);

bool heapLooksTight(uint32_t freeInternalBytes);

// A linha que vai para o serial. Diz o total da janela, o heap interno livre e qual das duas
// hipoteses do debito 13 o heap sustenta — fila cheia ou heap esgotado. Sem essa segunda
// parte o numero sozinho nao decide nada, e decidir e o proposito da medicao.
String describeDropWindow(uint32_t dropsInWindow, uint32_t freeInternalBytes);
