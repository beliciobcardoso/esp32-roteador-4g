#include "ota_trace.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/tcpip.h>

#include "ppp_drop_counter.h"
#include "timestamped_serial.h"
#include "uplink_byte_counter.h"

namespace {

constexpr size_t kReportEveryBytes = 64 * 1024;

// Escritos pelo loopTask, lidos tambem pela task do watchdog. Palavras de 32 bits alinhadas,
// mesma regra do `lastBeatMs_` do LoopWatchdog: volatile basta, ninguem faz read-modify-write
// fora do loopTask.
volatile const char* gPhase = "ocioso";
volatile uint32_t gStartMs = 0;
volatile uint32_t gLastChunkMs = 0;
volatile size_t gBytes = 0;

// So o loopTask toca.
size_t gNextReportBytes = 0;
uint32_t gWindowStartMs = 0;
size_t gWindowStartBytes = 0;
uint32_t gWindowStartRx = 0;

uint32_t elapsedMs(uint32_t now) { return now - gStartMs; }

const char* taskStateName(eTaskState state) {
  switch (state) {
    case eRunning: return "running";
    case eReady: return "ready";
    case eBlocked: return "blocked";
    case eSuspended: return "suspended";
    case eDeleted: return "deleted";
    default: return "?";
  }
}

void start(uint32_t now) {
  gStartMs = now;
  gNextReportBytes = kReportEveryBytes;
  gWindowStartMs = now;
  gWindowStartBytes = 0;
  gWindowStartRx = UplinkByteCounter::rxBytes();
  gPhase = "dados";
  logSerial.printf("OTA-T: inicio | uart rbfull %u fifo %u | pppdrop %u | heap %u min %u\n",
                   static_cast<unsigned>(PppDropCounter::uartRingBufferFullTotal()),
                   static_cast<unsigned>(PppDropCounter::uartFifoOverflowTotal()),
                   static_cast<unsigned>(PppDropCounter::totalCount()),
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

}  // namespace

namespace OtaTrace {

void onChunk(size_t totalBytes, bool isStart) {
  const uint32_t now = static_cast<uint32_t>(millis());
  if (isStart) start(now);
  gBytes = totalBytes;
  gLastChunkMs = now;
  if (totalBytes < gNextReportBytes) return;
  gNextReportBytes = totalBytes + kReportEveryBytes;

  const uint32_t windowMs = now - gWindowStartMs;
  const size_t windowBytes = totalBytes - gWindowStartBytes;
  const uint32_t rx = UplinkByteCounter::rxBytes();
  logSerial.printf(
      "OTA-T: %u B | %lu ms | %lu B/s | enlace rx +%u B | uart rbfull %u fifo %u | pppdrop %u "
      "| heap %u min %u\n",
      static_cast<unsigned>(totalBytes), static_cast<unsigned long>(elapsedMs(now)),
      static_cast<unsigned long>(windowMs == 0 ? 0 : windowBytes * 1000UL / windowMs),
      static_cast<unsigned>(rx - gWindowStartRx),
      static_cast<unsigned>(PppDropCounter::uartRingBufferFullTotal()),
      static_cast<unsigned>(PppDropCounter::uartFifoOverflowTotal()),
      static_cast<unsigned>(PppDropCounter::totalCount()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
  gWindowStartMs = now;
  gWindowStartBytes = totalBytes;
  gWindowStartRx = rx;
}

void mark(const char* phase) {
  gPhase = phase;
  const uint32_t now = static_cast<uint32_t>(millis());
  logSerial.printf("OTA-T: fase %s | %u B | %lu ms | uart rbfull %u fifo %u\n", phase,
                   static_cast<unsigned>(gBytes), static_cast<unsigned long>(elapsedMs(now)),
                   static_cast<unsigned>(PppDropCounter::uartRingBufferFullTotal()),
                   static_cast<unsigned>(PppDropCounter::uartFifoOverflowTotal()));
  Serial.flush();
}

void dumpStall() {
  const uint32_t now = static_cast<uint32_t>(millis());
  const char* phase = const_cast<const char*>(gPhase);
  logSerial.printf("OTA-T: travado | fase %s | %u B | ultimo bloco ha %lu ms\n", phase,
                   static_cast<unsigned>(gBytes),
                   static_cast<unsigned long>(gStartMs == 0 ? 0 : now - gLastChunkMs));

  // Quem segura o lock da tcpip. Sem dono: o loop nao esta preso no lock, esta esperando
  // dado num socket (WebServer::_uploadReadByte) ou noutra coisa.
  TaskHandle_t holder = xSemaphoreGetMutexHolder(lock_tcpip_core);
  if (holder == nullptr) {
    logSerial.println("OTA-T: lock da tcpip livre");
  } else {
    logSerial.printf("OTA-T: lock da tcpip com %s (%s)\n", pcTaskGetName(holder),
                     taskStateName(eTaskGetState(holder)));
  }

  const char* const watched[] = {"loopTask", "tiT", "uart_task", "wifi"};
  for (const char* name : watched) {
    TaskHandle_t task = xTaskGetHandle(name);
    if (task == nullptr) continue;
    logSerial.printf("OTA-T: task %s %s\n", name, taskStateName(eTaskGetState(task)));
  }
  logSerial.printf("OTA-T: uart rbfull %u fifo %u | pppdrop %u | heap %u min %u\n",
                   static_cast<unsigned>(PppDropCounter::uartRingBufferFullTotal()),
                   static_cast<unsigned>(PppDropCounter::uartFifoOverflowTotal()),
                   static_cast<unsigned>(PppDropCounter::totalCount()),
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

}  // namespace OtaTrace
