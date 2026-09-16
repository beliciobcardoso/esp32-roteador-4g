#include "wifi_ap.h"

#include <WiFi.h>
#include <esp_wifi.h>

namespace {
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);
const uint8_t kApChannel = 1;
const int kMaxClients = 20;
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

  // WiFi.softAP() do core Arduino nao expoe authmode WPA2/WPA3 misto diretamente;
  // decisao fechada exige WIFI_AUTH_WPA2_WPA3_PSK, entao ajustamos via API ESP-IDF.
  wifi_config_t current_config;
  if (esp_wifi_get_config(WIFI_IF_AP, &current_config) == ESP_OK) {
    current_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &current_config);
  }

  return true;
}
