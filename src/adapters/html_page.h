#pragma once

#include <Arduino.h>

// Pagina HTML embutida do formulario de configuracao.
// Placeholders substituidos em runtime pelo http_config_handler: {{SSID}}, {{APN}}, {{APN_USER}}, {{ADMIN_USER}}.
extern const char kConfigPageTemplate[];

// Prepara um valor salvo para ser interpolado dentro de value="..." no template acima.
// Obrigatorio nos quatro placeholders: nenhum dos campos e filtrado por caractere
// (validate() so olha comprimento) e SSID em 802.11 e sequencia de bytes arbitraria.
String escapeForHtmlAttribute(const String& value);
