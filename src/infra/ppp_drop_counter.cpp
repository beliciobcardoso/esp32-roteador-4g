#include "ppp_drop_counter.h"

#include <esp_log.h>

#include <atomic>
#include <cstring>

namespace {

// A mensagem que o lwIP monta em `pppos_input_tcpip()` quando o `sys_mbox_trypost()` na
// fila da task tcpip falha. O texto chega aqui dentro do format string do ESP_LOGE — antes
// de qualquer argumento ser formatado — entao basta procurar o literal, sem montar a linha.
constexpr const char* kDropFragment = "pppos_input_tcpip failed";

// O vprintf que estava instalado antes. Todo log que nao seja o descarte passa por ele sem
// alteracao: este hook e um filtro, nao uma substituicao do log do sistema.
vprintf_like_t gPreviousVprintf = nullptr;

// Nunca zera. A janela sai da diferenca contra gLastTaken, e o valor cru e o que a
// telemetria publica como counter.
std::atomic<uint32_t> gTotal{0};

// Marca da ultima drenagem. Nao e atomico de proposito: so a task que chama takeCount()
// escreve e le esta variavel, e o unico chamador e o loop(). Ver o contrato no header.
uint32_t gLastTaken = 0;

// Roda na task que emitiu o log — inclusive a do lwIP, dentro do caminho de recepcao. Por
// isso nao aloca, nao formata e nao loga: um ESP_LOG daqui reentraria neste mesmo hook.
int filteringVprintf(const char* format, va_list args) {
  if (format != nullptr && std::strstr(format, kDropFragment) != nullptr) {
    gTotal.fetch_add(1, std::memory_order_relaxed);
    // Devolve o que um printf teria escrito. Zero e honesto: nada foi para o serial.
    return 0;
  }
  if (gPreviousVprintf == nullptr) return 0;
  return gPreviousVprintf(format, args);
}

}  // namespace

namespace PppDropCounter {

void begin() {
  // Guarda de idempotencia: chamado duas vezes sem ela, o hook se instalaria por cima de si
  // mesmo e `gPreviousVprintf` passaria a apontar para `filteringVprintf` — recursao infinita
  // na primeira linha de log que nao fosse descarte.
  if (gPreviousVprintf != nullptr) return;
  gPreviousVprintf = esp_log_set_vprintf(&filteringVprintf);
}

uint32_t takeCount() {
  const uint32_t total = gTotal.load(std::memory_order_relaxed);
  // Subtracao de unsigned: atravessa a virada de 2^32 sem devolver um delta absurdo, do
  // mesmo jeito que as contas de millis() no dominio.
  const uint32_t sinceLast = total - gLastTaken;
  gLastTaken = total;
  return sinceLast;
}

uint32_t totalCount() { return gTotal.load(std::memory_order_relaxed); }

}  // namespace PppDropCounter
