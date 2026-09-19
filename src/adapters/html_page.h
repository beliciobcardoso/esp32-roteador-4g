#pragma once

#include <Arduino.h>

// Pagina HTML embutida do formulario de configuracao.
// Placeholders substituidos em runtime pelo http_config_handler: {{SSID}}, {{APN}},
// {{APN_USER}}, {{ADMIN_USER}}, {{UPLINK_STATUS}}.
extern const char kConfigPageTemplate[];

// Prepara um valor para ser interpolado no template acima.
// Obrigatorio nos quatro placeholders de campo: nenhum deles e filtrado por caractere
// (validate() so olha comprimento) e SSID em 802.11 e sequencia de bytes arbitraria.
String escapeForHtmlAttribute(const String& value);
