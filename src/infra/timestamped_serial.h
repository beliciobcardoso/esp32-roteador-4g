#pragma once

#include <Arduino.h>

#include "clock.h"

// INFRA — o Serial das linhas do projeto, com a hora local no inicio de cada uma:
//
//   22:25:52 Bateria: 4.16V | ~96%
//
// Existe porque o tick do IDF (`I (56015)`) zera a cada boot e nao se cruza com nada — nem
// com o relogio de quem estava no celular, nem com o de outra captura. Com a hora na linha,
// "o upload travou as 22:14" vira algo que se procura no log.
//
// So cobre o que o projeto imprime. As linhas do ESP-IDF (`I (…) wifi:`) e as do core
// Arduino (`[…][E][WebServer…]`) seguem com o carimbo delas: nao passam por aqui, e trocar
// o delas exigiria mexer no sdkconfig, com o debito 26 junto.
//
// Antes da primeira sincronizacao o prefixo e `--:--:--`, e nao a hora que o relogio do
// sistema tiver: ele conta a partir do boot, e `00:00:03` seria lido como tres da manha. A
// regra e a mesma do `Clock::nowText()`: a placa nunca finge um horario.
//
// Varias tasks escrevem no serial (loop, supervisor do uplink, watchdog). O indicador de
// inicio de linha e compartilhado sem trava, como o proprio Serial: duas linhas simultaneas
// ja se cortavam antes, e o prefixo pode cair no meio de uma delas do mesmo jeito. Uma
// trava aqui poderia segurar justamente a task do watchdog.
class TimestampedSerial : public Print {
 public:
  // Sem o relogio, todo prefixo sai `--:--:--`. Chamar no setup(), depois do Clock::begin().
  void begin(const Clock& clock);

  size_t write(uint8_t byte) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  void writePrefix();

  const Clock* clock_ = nullptr;
  volatile bool atLineStart_ = true;
};

extern TimestampedSerial logSerial;
