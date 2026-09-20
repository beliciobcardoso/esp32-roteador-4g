#include "link_diagnostics.h"

const unsigned long kDropReportIntervalMs = 30000;

// 32 KB. O ESP32 sobe com ~170 KB de DRAM livre depois do WiFi e do PPP, entao este piso e
// bem abaixo do normal de operacao: cruza-lo ja e sinal por si so, nao ruido de medicao.
const uint32_t kTightInternalHeapBytes = 32768;

bool dropReportDue(unsigned long now, unsigned long lastReportMs, uint32_t dropsInWindow) {
  if (dropsInWindow == 0) return false;
  return (now - lastReportMs) >= kDropReportIntervalMs;
}

bool heapLooksTight(uint32_t freeInternalBytes) {
  return freeInternalBytes <= kTightInternalHeapBytes;
}

String describeDropWindow(uint32_t dropsInWindow, uint32_t freeInternalBytes) {
  String line = "PPP: " + numberToString(dropsInWindow);
  line += dropsInWindow == 1 ? " pacote " : " pacotes ";
  line += "descartados na entrada nos ultimos ";
  line += numberToString(kDropReportIntervalMs / 1000);
  line += " s | heap interno livre ";
  line += numberToString(freeInternalBytes);
  line += " B";

  // A leitura do heap e o que separa as duas hipoteses do debito 13. Sem ela o total
  // sozinho nao decide entre aumentar a fila e caçar consumo de memoria.
  line += heapLooksTight(freeInternalBytes)
              ? " — heap no talo, o descarte pode nao ser da fila"
              : " — heap folgado, descarte e pressao de fila";
  return line;
}
