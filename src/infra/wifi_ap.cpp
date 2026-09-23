#include "wifi_ap.h"

#include <WiFi.h>
#include <esp_wifi.h>

namespace {
// Faixa do AP. 192.168.10.0/24 e nao a 192.168.4.0/24 do default do core: a .4 e a faixa
// que metade dos roteadores de mesa e dos exemplos de ESP32 usa, e colidir com a rede onde
// a unidade e configurada faz a pagina responder de outro aparelho sem erro nenhum — foi o
// que aconteceu em 23/09/2026 numa bancada, onde http://192.168.4.1/ devolveu um nginx da
// rede da empresa em vez da placa.
//
// Valor fixo em codigo, nao na NVS: existe uma faixa em uso, nao duas. Enderecar isso pela
// pagina seria a abstracao antes da segunda ocorrencia real, e ainda daria a quem configura
// a chance de se trancar para fora da propria unidade.
//
// Trocar este valor arrasta o DHCP junto e nao exige mais nada: set_esp_interface_ip()
// (core Arduino, WiFiGeneric.cpp) deriva o lease do proprio IP do AP — inicio em ap_ip + 1,
// fim em inicio + 10. O DnsForwarder e o NatBridge leem de WiFi.softAPIP(), entao tambem
// seguem sozinhos. A mascara precisa continuar entre /24 e /28, que e a faixa que aquela
// funcao aceita.
const IPAddress kApIp(192, 168, 10, 1);
const IPAddress kApGateway(192, 168, 10, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
const uint8_t kApChannel = 1;
// Teto do driver no ESP32 classico: ESP_WIFI_MAX_CONN_NUM vale 15
// (esp_wifi_types.h:320). Pedir mais que isso nao e ambicao, e numero mentiroso —
// o softAP aceita o valor e silenciosamente atende 15.
const int kMaxClients = 15;
}  // namespace

bool WifiAp::start(const RouterSettings& settings) {
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(kApIp, kApGateway, kApSubnet)) {
    return false;
  }

  bool started = WiFi.softAP(settings.wifi_ssid.c_str(), settings.wifi_password.c_str(),
                              kApChannel, /*ssid_hidden=*/0, kMaxClients);
  if (!started) {
    return false;
  }

  // WPA3 em softAP nao existe no ESP32 classico com IDF 4.4: o Kconfig
  // ESP32_WIFI_ENABLE_WPA3_SAE cobre so o lado station ("connection with eligible
  // AP's"). SoftAP SAE chegou no IDF 5.x (ESP_WIFI_SOFTAP_SAE_SUPPORT). Pedir
  // WIFI_AUTH_WPA2_WPA3_PSK aqui so gerava "Invalid authmode 7" e o driver caia
  // num modo nao determinado. Fixamos WPA2-PSK explicitamente em vez de confiar
  // no default do core Arduino.
  wifi_config_t current_config;
  if (esp_wifi_get_config(WIFI_IF_AP, &current_config) != ESP_OK) {
    return false;
  }
  current_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  if (esp_wifi_set_config(WIFI_IF_AP, &current_config) != ESP_OK) {
    return false;
  }

  return true;
}
