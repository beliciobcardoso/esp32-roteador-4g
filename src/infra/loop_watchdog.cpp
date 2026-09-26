#include "loop_watchdog.h"

#include <Arduino.h>
#include <esp_system.h>

#include "../domain/loop_health.h"
#include "timestamped_serial.h"

namespace {

// Periodo de verificacao. Um segundo: o limite e de dezenas de segundos, entao a precisao
// nao importa, e a task fica ociosa quase o tempo todo.
constexpr uint32_t kCheckIntervalMs = 1000;

// Pilha pequena de proposito — a task compara dois inteiros e imprime uma linha. O
// `esp_restart()` nao volta, entao nada depois dele consome pilha.
constexpr uint32_t kTaskStackBytes = 2560;

// Abaixo da prioridade do loopTask (1) nao serve: se o loop travasse numa espera ocupada
// sem ceder, uma task de prioridade menor nunca rodaria para reiniciar a placa. Acima do
// supervisor de uplink tambem nao faz falta; 2 basta para ganhar do loopTask.
constexpr UBaseType_t kTaskPriority = 2;

}  // namespace

bool LoopWatchdog::begin() {
  lastBeatMs_ = static_cast<uint32_t>(millis());
  const BaseType_t created = xTaskCreate(&LoopWatchdog::taskEntry, "loop_wdt",
                                         kTaskStackBytes, this, kTaskPriority, &task_);
  if (created != pdPASS) {
    logSerial.println("LoopWatchdog: task nao subiu — sem rede de seguranca para travamento do loop");
    task_ = nullptr;
    return false;
  }
  return true;
}

void LoopWatchdog::beat() { lastBeatMs_ = static_cast<uint32_t>(millis()); }

void LoopWatchdog::taskEntry(void* arg) { static_cast<LoopWatchdog*>(arg)->run(); }

void LoopWatchdog::run() {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kCheckIntervalMs));

    const uint32_t now = static_cast<uint32_t>(millis());
    const uint32_t lastBeat = lastBeatMs_;
    if (!loopHasStalled(now, lastBeat)) continue;

    // A linha sai antes do reinicio porque e a unica prova de que isto aconteceu: depois do
    // `esp_restart()` o motivo do reset e `SW_CPU_RESET`, igual ao de um OTA e ao de um
    // reinicio pedido pela pagina. Sem esta linha, um travamento em campo vira "a placa
    // reiniciou sozinha" e nao se distingue de nada.
    logSerial.printf("LoopWatchdog: loop parado ha %lu ms — reiniciando\n",
                  static_cast<unsigned long>(now - lastBeat));
    Serial.flush();

    // Reinicia mesmo com confirmacao de firmware pendente, ao contrario do LinkSupervisor,
    // que se segura. La o motivo era nao reverter por falta de sinal uma imagem que talvez
    // estivesse boa. Aqui nao ha essa duvida: com o loop() parado, `settleFirmwareConfirmation`
    // nao roda, o prazo nunca vence e o clique de confirmacao nao seria atendido — a imagem
    // ficaria em PENDING_VERIFY para sempre. Reiniciar entrega o caso ao rollback do
    // bootloader, que e o desfecho que a propria regra do dominio pede quando o servidor nao
    // esta atendendo.
    esp_restart();
  }
}
