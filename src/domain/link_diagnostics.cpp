#include "link_diagnostics.h"

const uint32_t kDropReportIntervalMs = 30000;

const uint32_t kQuietWindowsPerReport = 4;

// 32 KB. O ESP32 sobe com ~170 KB de DRAM livre depois do WiFi e do PPP, entao este piso e
// bem abaixo do normal de operacao: cruza-lo ja e sinal por si so, nao ruido de medicao.
const uint32_t kTightInternalHeapBytes = 32768;

bool dropWindowClosed(uint32_t now, uint32_t lastWindowMs) {
  return (now - lastWindowMs) >= kDropReportIntervalMs;
}

bool quietReportDue(uint32_t quietWindows) {
  return quietWindows >= kQuietWindowsPerReport;
}

bool heapLooksTight(uint32_t freeInternalBytes) {
  return freeInternalBytes <= kTightInternalHeapBytes;
}

namespace {

// A leitura do heap e o que separa as duas hipoteses do debito 13. Sem ela o total sozinho
// nao decide entre aumentar a fila e cacar consumo de memoria.
//
// Estas frases seguem sem acento enquanto as da pagina foram acentuadas, e a diferenca e de
// destino: elas saem no serial, nao na interface. Terminal em campo nem sempre esta em
// UTF-8, e o valor delas e ser greppavel.
// `explainDropCause` separa as duas linhas. Na janela com descarte o veredito do heap diz
// de qual das duas hipoteses do debito 13 aquele descarte veio. Na janela silenciosa nao ha
// descarte sobre o qual opinar, e carregar a mesma frase faria a linha diagnosticar a causa
// de um evento que nao aconteceu — o heap continua ali porque a hipotese alternativa precisa
// seguir verificavel, mas sem veredito pendurado nele.
void appendHeapReading(String& line, uint32_t freeInternalBytes, bool explainDropCause) {
  line += " | heap interno livre ";
  line += numberToString(freeInternalBytes);
  line += " B";

  if (heapLooksTight(freeInternalBytes)) {
    line += explainDropCause ? " — heap no talo, o descarte pode nao ser da fila"
                             : " — heap no talo";
    return;
  }
  if (explainDropCause) {
    line += " — heap folgado, descarte e pressao de fila";
  }
}

}  // namespace

String describeDropWindow(uint32_t dropsInWindow, uint32_t freeInternalBytes) {
  const bool singular = dropsInWindow == 1;
  String line = "PPP: " + numberToString(dropsInWindow);
  line += singular ? " pacote descartado" : " pacotes descartados";
  line += " na entrada nos ultimos ";
  line += numberToString(kDropReportIntervalMs / 1000);
  line += " s";
  appendHeapReading(line, freeInternalBytes, /*explainDropCause=*/true);
  return line;
}

String describeQuietWindows(uint32_t quietWindows, uint32_t freeInternalBytes) {
  // Diz "0 pacotes descartados" com todas as letras, e nao "sem descartes": a linha existe
  // para ser lida como medicao feita, e um numero escrito e mais dificil de confundir com
  // uma ausencia do que uma negativa.
  String line = "PPP: 0 pacotes descartados em ";
  line += numberToString(quietWindows);
  line += quietWindows == 1 ? " janela de " : " janelas de ";
  line += numberToString(kDropReportIntervalMs / 1000);
  line += " s (";
  line += numberToString(quietWindows * (kDropReportIntervalMs / 1000));
  line += " s medidos)";
  appendHeapReading(line, freeInternalBytes, /*explainDropCause=*/false);
  return line;
}
