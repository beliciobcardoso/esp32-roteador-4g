#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

// INFRA — reinicia a placa quando o loop() para de dar sinal de vida. O limite e a regra
// pura de `domain/loop_health`; aqui fica a task, o relogio e o esp_restart().
//
// Vive numa task propria porque e a unica forma de enxergar o defeito: quem trava e o
// loopTask, entao qualquer verificacao hospedada nele trava junto. A task so acorda, compara
// dois numeros e volta a dormir.
//
// Por que nao `enableLoopWDT()`, que o core oferece de graca: ele prende o veredito ao
// `CONFIG_ESP_TASK_WDT_TIMEOUT_S`, que vale 5 s. Cinco segundos sem um bloco novo e comum em
// Wi-Fi ruim durante um upload legitimo, e subir esse numero no `sdkconfig.defaults` mexeria
// no gerado e arrastaria o debito 26 junto. Com limite proprio o numero e escolhido pelo que
// esta sendo protegido, e nao pelo default de uma flag global.
class LoopWatchdog {
 public:
  // Cria a task e marca o primeiro batimento. Nao bloqueia. Falso se a task nao subir — e
  // ai a placa segue sem rede de seguranca, que e o estado de hoje, nao uma regressao.
  bool begin();

  // "Ainda estou andando". Chamada de dois lugares, de proposito:
  //
  //   - do loop(), a cada volta, que e o caso normal;
  //   - do callback de bloco do upload de firmware, que roda DENTRO do handleClient() e
  //     portanto dentro da mesma travessia que nao devolve o controle ao loop() por dezenas
  //     de segundos. Sem esse segundo ponto, todo upload legitimo seria lido como travamento.
  //
  // O que sobra sem batimento e exatamente o que se quer pegar: a espera por dados que nao
  // vem mais.
  void beat();

 private:
  static void taskEntry(void* arg);
  void run();

  // Escrita pelo loopTask, lida pela task do watchdog. `volatile` e suficiente: e uma
  // palavra de 32 bits alinhada, cuja leitura e escrita o xtensa faz numa instrucao so, e
  // ninguem faz read-modify-write nela. Um mutex aqui protegeria contra uma corrida que nao
  // existe e poderia bloquear justamente a task que precisa continuar andando.
  volatile uint32_t lastBeatMs_ = 0;
  TaskHandle_t task_ = nullptr;
};
