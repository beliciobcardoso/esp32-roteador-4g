#include "uplink_byte_counter.h"

#include <atomic>

namespace {

// Escritos da task do lwIP (TX) e da task de recepcao do esp_modem (RX), lidos do loop().
// Atomicos porque sao read-modify-write vindos de tasks diferentes; `relaxed` basta, ninguem
// sincroniza outra memoria atraves deles.
std::atomic<esp_netif_t*> gWatched{nullptr};
std::atomic<uint32_t> gRxBytes{0};
std::atomic<uint32_t> gTxBytes{0};

}  // namespace

// Resolvidos pelo linker por causa do `-Wl,--wrap=...`: `__real_` e a funcao original da IDF,
// e toda chamada vinda de outro arquivo objeto cai no `__wrap_`.
extern "C" {
esp_err_t __real_esp_netif_receive(esp_netif_t* esp_netif, void* buffer, size_t len, void* eb);
esp_err_t __real_esp_netif_transmit(esp_netif_t* esp_netif, void* data, size_t len);

// No caminho de recepcao de TODAS as interfaces, inclusive o Wi-Fi: por isso so a
// comparacao de ponteiro antes de repassar, e nada que possa bloquear.
esp_err_t __wrap_esp_netif_receive(esp_netif_t* esp_netif, void* buffer, size_t len, void* eb) {
  if (esp_netif != nullptr && esp_netif == gWatched.load(std::memory_order_relaxed)) {
    gRxBytes.fetch_add(static_cast<uint32_t>(len), std::memory_order_relaxed);
  }
  return __real_esp_netif_receive(esp_netif, buffer, len, eb);
}

esp_err_t __wrap_esp_netif_transmit(esp_netif_t* esp_netif, void* data, size_t len) {
  if (esp_netif != nullptr && esp_netif == gWatched.load(std::memory_order_relaxed)) {
    gTxBytes.fetch_add(static_cast<uint32_t>(len), std::memory_order_relaxed);
  }
  return __real_esp_netif_transmit(esp_netif, data, len);
}
}

namespace UplinkByteCounter {

void watch(esp_netif_t* uplink) { gWatched.store(uplink, std::memory_order_relaxed); }

uint32_t rxBytes() { return gRxBytes.load(std::memory_order_relaxed); }

uint32_t txBytes() { return gTxBytes.load(std::memory_order_relaxed); }

}  // namespace UplinkByteCounter
