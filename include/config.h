#pragma once

// Defaults de fabrica — usados no primeiro boot, quando a NVS ainda nao tem config salva.
#define DEFAULT_AP_SSID "esp32-roteador-4g"
#define DEFAULT_AP_PASSWORD "roteador4g"
#define DEFAULT_APN "internet"
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASSWORD "admin1234"

// Namespace usado na NVS para as chaves de configuracao do roteador.
#define SETTINGS_NVS_NAMESPACE "router_cfg"
