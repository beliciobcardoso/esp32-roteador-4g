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
//
// O conteudo nao esta em nenhum .cpp: e o proprio http/index.html, embutido pelo
// EMBED_TXTFILES de src/CMakeLists.txt. Editar a pagina e editar aquele arquivo — nao ha
// segunda copia para manter em dia. O asm() amarra este nome ao simbolo que o linker cria
// a partir do caminho do arquivo; renomear ou mover http/index.html quebra o link com
// "undefined reference to _binary_index_html_start", nao em silencio.
extern const char kConfigPageTemplate[] asm("_binary_index_html_start");

// Prepara um valor para ser interpolado no template acima.
// Obrigatorio nos quatro placeholders de campo: nenhum deles e filtrado por caractere
// (validate() so olha comprimento) e SSID em 802.11 e sequencia de bytes arbitraria.
String escapeForHtmlAttribute(const String& value);
