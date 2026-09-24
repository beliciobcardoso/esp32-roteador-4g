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
extern const uint32_t kDropReportIntervalMs;

// Quantas janelas silenciosas seguidas cabem antes de uma linha sair MESMO sem descarte.
//
// Existe porque ate 23/09/2026 janela zerada nao imprimia nada, e isso tornava "zero
// descartes" indistinguivel de "o relatorio nao esta saindo" — as duas coisas apareciam na
// tela do mesmo jeito, como ausencia. Para o debito 13 isso e fatal: o que falta ali e
// justamente provar zero sob carga, e uma prova que se parece com uma falha nao prova nada.
//
// 4 janelas (2 min) e o meio termo: uma medicao de 10 min sob carga ganha 5 linhas, cada uma
// afirmando explicitamente quanto tempo passou sem descarte, e uma unidade ociosa em campo
// gasta 30 linhas por hora — contra as milhares que motivaram este arquivo.
extern const uint32_t kQuietWindowsPerReport;

// Piso de heap interno abaixo do qual a leitura deixa de ser confortavel. Nao e o ponto em
// que o lwIP falha — e o ponto a partir do qual nao da mais para afirmar que o descarte veio
// da fila, que e a distincao que o debito 13 precisa fechar.
extern const uint32_t kTightInternalHeapBytes;

// A janela fechou? So conta tempo; o que fazer com ela e decisao separada.
//
// A conta e por subtracao de unsigned de proposito, para atravessar a virada de millis()
// (~49 dias). Comparacao direta de instantes prenderia o relatorio ate a volta seguinte.
bool dropWindowClosed(uint32_t now, uint32_t lastWindowMs);

// Fechou a janela e nao houve descarte nenhum: ja e hora de dizer isso em voz alta?
bool quietReportDue(uint32_t quietWindows);

bool heapLooksTight(uint32_t freeInternalBytes);

// A linha que vai para o serial. Diz o total da janela, o heap interno livre e qual das duas
// hipoteses do debito 13 o heap sustenta — fila cheia ou heap esgotado. Sem essa segunda
// parte o numero sozinho nao decide nada, e decidir e o proposito da medicao.
String describeDropWindow(uint32_t dropsInWindow, uint32_t freeInternalBytes);

// A linha do silencio. Diz quantas janelas passaram sem um descarte sequer e quanto tempo
// isso cobre, para o zero virar afirmacao datada em vez de ausencia de linha.
String describeQuietWindows(uint32_t quietWindows, uint32_t freeInternalBytes);
