#pragma once

#include "../domain/router_settings.h"

// INFRA — wrapper fino sobre a API de SoftAP do ESP-IDF/Arduino.
class WifiAp {
 public:
  // Sobe o SoftAP com SSID/senha de `settings`, IP fixo, WPA2/WPA3 misto, ate 20 clientes.
  // Retorna false se a config de IP ou o start do AP falharem.
  bool start(const RouterSettings& settings);
};
