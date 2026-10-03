#include "wg_spike.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_wireguard.h>
#include <lwip/netif.h>

#include "../../include/wg_spike_secrets.h"
#include "timestamped_serial.h"

namespace {

wireguard_config_t gConfig;
wireguard_ctx_t gCtx = {};
bool gStarted = false;
esp_err_t gInitResult = ESP_FAIL;
esp_err_t gResolveResult = ESP_FAIL;
esp_err_t gConnectResult = ESP_FAIL;
bool gPeerUp = false;
uint32_t gLastReportMs = 0;
constexpr uint32_t kReportIntervalMs = 30000;

// Fork em components/esp_wireguard (ver FORK.md): o resolve faz getaddrinfo() e roda FORA do lock
// da tcpip; o connect e o peer_is_up sao API crua do lwIP e rodam DENTRO, pelo
// esp_netif_tcpip_exec() — o mesmo jeito que o NatBridge ja usa.
esp_err_t connectInTcpipContext(void* ctx) {
  auto* uplink = static_cast<esp_netif_t*>(ctx);
  char name[8] = {};
  if (esp_netif_get_netif_impl_name(uplink, name) != ESP_OK) {
    gConnectResult = ESP_ERR_NOT_FOUND;
    return gConnectResult;
  }
  struct netif* lwipUplink = netif_find(name);
  gConnectResult = lwipUplink ? esp_wireguard_connect(&gCtx, lwipUplink) : ESP_ERR_NOT_FOUND;
  return gConnectResult;
}

esp_err_t peerIsUpInTcpipContext(void*) {
  gPeerUp = esp_wireguardif_peer_is_up(&gCtx) == ESP_OK;
  return ESP_OK;
}

void start(esp_netif_t* uplink) {
  gConfig = ESP_WIREGUARD_CONFIG_DEFAULT();
  gConfig.private_key = WG_SPIKE_PRIVATE_KEY;
  gConfig.listen_port = WG_SPIKE_PORT;
  gConfig.public_key = WG_SPIKE_PEER_PUBLIC_KEY;
  gConfig.allowed_ip = WG_SPIKE_ADDRESS;
  gConfig.allowed_ip_mask = WG_SPIKE_NETMASK;
  gConfig.endpoint = WG_SPIKE_ENDPOINT;
  gConfig.port = WG_SPIKE_PORT;
  // CGNAT: sem keepalive o mapeamento da operadora expira e o servidor perde o caminho de volta.
  gConfig.persistent_keepalive = 25;

  const uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  gInitResult = esp_wireguard_init(&gConfig, &gCtx);
  if (gInitResult == ESP_OK) gResolveResult = esp_wireguard_resolve(&gCtx);
  if (gResolveResult == ESP_OK) esp_netif_tcpip_exec(&connectInTcpipContext, uplink);
  const uint32_t heapAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  logSerial.printf("WG: init %s, resolve %s, connect %s | heap interno %u -> %u B\n",
                   esp_err_to_name(gInitResult), esp_err_to_name(gResolveResult),
                   esp_err_to_name(gConnectResult), static_cast<unsigned>(heapBefore),
                   static_cast<unsigned>(heapAfter));
  // A rota padrao NAO muda: esp_wireguard_set_default() nao e chamado de proposito.
  logSerial.printf("WG: rota padrao segue em %c%c%u\n", netif_default ? netif_default->name[0] : '-',
                   netif_default ? netif_default->name[1] : '-',
                   netif_default ? netif_default->num : 0);
}

}  // namespace

namespace WgSpike {

void tick(bool uplinkOnline, bool clockSynced, esp_netif_t* uplink) {
  if (!gStarted && uplinkOnline && clockSynced && uplink != nullptr) {
    gStarted = true;
    start(uplink);
  }
  if (!gStarted || gConnectResult != ESP_OK) return;
  const uint32_t now = millis();
  if (now - gLastReportMs < kReportIntervalMs) return;
  gLastReportMs = now;
  esp_netif_tcpip_exec(&peerIsUpInTcpipContext, nullptr);
  logSerial.printf("WG: peer %s | heap interno livre %u B, minimo %u B\n", gPeerUp ? "UP" : "down",
                   static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
}

}  // namespace WgSpike
