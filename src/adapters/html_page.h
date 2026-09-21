#pragma once

// Pagina de configuracao embutida no binario.
//
// O conteudo nao esta em nenhum .cpp: e o proprio http/index.html, embutido pelo
// EMBED_TXTFILES de src/CMakeLists.txt. Editar a pagina e editar aquele arquivo — nao ha
// segunda copia para manter em dia. O asm() amarra este nome ao simbolo que o linker cria
// a partir do caminho do arquivo; renomear ou mover http/index.html quebra o link com
// "undefined reference to _binary_index_html_start", nao em silencio.
//
// A pagina e servida byte a byte, sem substituicao nenhuma: os dados vem das rotas
// /api/status e /api/config, que o proprio JS da pagina busca. Nao ha mais placeholder
// aqui, e por isso nao ha mais escape de HTML neste modulo — o unico escape que sobrou e o
// de JSON, em domain/json.h, onde ele e testavel sem placa.
extern const char kConfigPageTemplate[] asm("_binary_index_html_start");
