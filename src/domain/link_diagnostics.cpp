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
  const bool singular = dropsInWindow == 1;
  String line = "PPP: " + numberToString(dropsInWindow);
  line += singular ? " pacote descartado" : " pacotes descartados";
  line += " na entrada nos ultimos ";
  line += numberToString(kDropReportIntervalMs / 1000);
  line += " s | heap interno livre ";
  line += numberToString(freeInternalBytes);
  line += " B";

  // A leitura do heap e o que separa as duas hipoteses do debito 13. Sem ela o total
  // sozinho nao decide entre aumentar a fila e caçar consumo de memoria.
  //
  // Esta linha segue sem acento enquanto as da pagina foram acentuadas, e a diferenca e de
  // destino: ela sai no serial, nao na interface. Terminal em campo nem sempre esta em
  // UTF-8, e o valor dela e ser greppavel — o AGENTS.md ate documenta os filtros de grep
  // para ler o serial sob trafego. "nao ser da fila" com acento quebraria esses filtros.
  line += heapLooksTight(freeInternalBytes)
              ? " — heap no talo, o descarte pode nao ser da fila"
              : " — heap folgado, descarte e pressao de fila";
  return line;
}
