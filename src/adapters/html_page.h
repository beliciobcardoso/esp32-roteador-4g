#pragma once

#include <Arduino.h>

// Pagina HTML embutida do formulario de configuracao.
// Placeholders substituidos em runtime pelo http_config_handler: {{SSID}}, {{APN}},
// {{APN_USER}}, {{ADMIN_USER}}, {{UPLINK_STATUS}}, {{ADMIN_NOTICE}}, {{CLOCK}},
// {{TIMEZONE_OPTIONS}}, {{BATTERY_RATIO}}, {{FIRMWARE_SLOT}}, {{FIRMWARE_VERSION}},
// {{FIRMWARE_STATE}}, {{FIRMWARE_CONFIRM}}.
//
// {{ADMIN_NOTICE}}, {{TIMEZONE_OPTIONS}} e {{FIRMWARE_CONFIRM}} recebem marcacao, nao valor: o
// handler monta o paragrafo de alerta e as <option> da tabela de fusos do dominio. O texto
// dentro dos dois passa pelo escape do mesmo jeito.
extern const char kConfigPageTemplate[];

// Prepara um valor para ser interpolado no template acima.
// Obrigatorio nos quatro placeholders de campo: nenhum deles e filtrado por caractere
// (validate() so olha comprimento) e SSID em 802.11 e sequencia de bytes arbitraria.
String escapeForHtmlAttribute(const String& value);
