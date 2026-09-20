#pragma once

#include <cstdint>

#include "string_type.h"

// ENTIDADE — montagem de JSON. Regra pura: nao conhece HTTP nem WebServer, entao roda no
// host e e testavel sem placa. Existe porque a pagina passou a buscar os dados por
// fetch(), e JSON mal escapado nao falha bonito: um unico byte solto derruba o
// JSON.parse() do navegador e a pagina inteira fica em branco, sem dizer por que.
//
// Escrito a mao em vez de ArduinoJson porque o que se monta aqui sao dois objetos planos
// de campos conhecidos. A biblioteca traria um alocador e um modelo de documento para um
// problema que e concatenar string com escape correto.

// Escapa um valor para caber entre aspas num JSON.
//
// Faz mais do que trocar aspa e barra: valida UTF-8 e substitui byte invalido por U+FFFD.
// Isso nao e purismo. SSID em 802.11 e sequencia de bytes arbitraria — nada obriga a ser
// UTF-8 — e JSON so admite texto UTF-8 valido. Deixar o byte cru passar produz um corpo
// que o navegador recusa inteiro, e o sintoma aparece longe da causa: a pagina nao carrega
// e o SSID esquisito esta tres telas atras, na etiqueta de outra unidade.
String escapeForJsonString(const String& value);

// Objeto JSON plano, montado campo a campo.
//
// So objeto plano e so os tipos abaixo: e o que as duas rotas de API precisam. Aninhamento
// e array entram quando houver o segundo caso que os peca, nao antes.
class JsonObject {
 public:
  JsonObject& text(const char* name, const String& value);
  JsonObject& boolean(const char* name, bool value);
  JsonObject& number(const char* name, uint32_t value);
  JsonObject& number(const char* name, float value, uint8_t decimals);

  // Campo cujo valor ja e JSON — outro objeto, um array, ou `null`. Nao passa por escape:
  // quem chama e responsavel pelo que entrega.
  JsonObject& raw(const char* name, const String& json);

  // Fecha o objeto. Chamar duas vezes devolve o mesmo texto; adicionar campo depois de
  // fechar e erro de quem chama e nao e detectado — nao ha caso de uso, e checar isso
  // custaria um estado so para proteger de um erro que o compilador ja torna visivel.
  String finish() const { return "{" + body_ + "}"; }

 private:
  void separate();

  String body_;
};
