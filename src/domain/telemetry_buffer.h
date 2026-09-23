#pragma once

#include <cstdint>

#include "telemetry.h"

// ENTIDADE — o anel que guarda amostra enquanto nao ha como publicar, e a correcao do
// carimbo de tempo colhido antes do relogio. Regra pura: nao conhece RTC nem MQTT.
//
// A metrica que mais importa e a que nao pode ser enviada: uplink caido e exatamente
// quando nao ha publicacao possivel. Sem este anel a queda vira buraco no grafico em vez
// de evento com comeco e fim.
//
// O registro e POD de proposito. Quem o instancia poe o objeto em RTC_NOINIT_ATTR, e
// memoria nao inicializada nao admite construtor — a validacao do cabecalho faz o papel
// que um construtor faria, e faz melhor, porque tambem pega o conteudo de uma versao
// anterior do firmware.

// 'TLM1'. Pega RTC RAM com lixo de power-on, que e o unico caso em que o conteudo e
// aleatorio de verdade.
extern const uint32_t kTelemetryRingMagic;

// Sobe quando TelemetrySample muda de forma deliberada.
extern const uint16_t kTelemetryLayoutVersion;

// ~4 KB dos 8 KB de RTC slow RAM. A 20 B por amostra e 60 s de cadencia, cobre ~3,4 h de
// enlace fora — mais que isso e um problema que o anel nao resolve sozinho.
//
// `constexpr` e nao `extern const` como as outras constantes deste dominio, e a diferenca
// nao e estilo: este valor dimensiona o array logo abaixo, e `extern const` nao serve como
// limite de array. Declarar os dois separados abriria a chance de divergirem em silencio.
constexpr uint16_t kTelemetryRingCapacity = 204;

struct TelemetryRing {
  uint32_t magic;
  uint16_t layout;
  uint16_t sample_size;
  uint16_t head;   // proximo indice a escrever
  uint16_t count;  // quantas amostras validas existem
  TelemetrySample samples[kTelemetryRingCapacity];
};

// Cabecalho integro?
//
// Confere tres coisas, e nenhuma e redundante. `magic` pega RTC RAM aleatoria de power-on;
// `layout` pega mudanca deliberada da amostra; `sample_size` pega o erro realista — alguem
// acrescenta um campo e esquece de subir a versao. Indices fora da faixa entram na mesma
// conferencia: contagem corrompida viraria leitura fora do array, que e pior do que perder
// o buffer.
bool ringHeaderIsValid(const TelemetryRing& ring);

// Zera o anel e reescreve o cabecalho.
void ringReset(TelemetryRing& ring);

// Confere e, se preciso, zera. Devolve true se o conteudo anterior foi aproveitado.
//
// E o que roda no boot. Depois de um OTA com a amostra mudada, o firmware novo leria o
// layout antigo no mesmo endereco e publicaria lixo como medicao valida — pior do que
// perder o historico, porque dado falso vira decisao. Custo de descartar: uma atualizacao
// perde o buffer, uma vez.
bool ringValidateOrReset(TelemetryRing& ring);

uint16_t ringSize(const TelemetryRing& ring);
bool ringIsEmpty(const TelemetryRing& ring);

// Grava, descartando a mais velha quando cheio. Amostra recente vale mais do que amostra
// de tres horas atras: com o enlace fora por muito tempo, o que interessa e o trecho
// proximo da volta.
void ringPush(TelemetryRing& ring, const TelemetrySample& sample);

// Le a mais velha sem remover. Devolve false se o anel estiver vazio.
bool ringPeekOldest(const TelemetryRing& ring, TelemetrySample& out);

// Remove a mais velha. O drenar so avanca depois que a publicacao foi aceita — remover
// antes perderia a amostra numa reconexao que falhou no meio.
bool ringPopOldest(TelemetryRing& ring, TelemetrySample& out);

// Converte o carimbo monotonico em epoch, usando a hora de agora.
//
// `ts_real = agora_epoch - (agora_monotonico - amostra_monotonica)`. Devolve false — e nao
// altera nada — quando nao ha o que corrigir ou quando a conta nao fecha: amostra do
// futuro em relacao a agora e sinal de anel corrompido ou de relogio que andou para tras,
// e nos dois casos inventar um instante e pior do que segurar a amostra.
bool correctSampleTimestamp(TelemetrySample& sample, uint32_t nowMonotonicS, uint32_t nowEpochS);
