#include "json.h"

namespace {

const char kHexDigits[] = "0123456789abcdef";

void appendUnicodeEscape(String& out, uint16_t codepoint) {
  out += "\\u";
  out += kHexDigits[(codepoint >> 12) & 0xF];
  out += kHexDigits[(codepoint >> 8) & 0xF];
  out += kHexDigits[(codepoint >> 4) & 0xF];
  out += kHexDigits[codepoint & 0xF];
}

// Quantos bytes a sequencia UTF-8 que comeca neste byte deveria ter. Zero significa que o
// byte nao pode iniciar sequencia nenhuma — continuacao solta (10xxxxxx) ou um dos valores
// que o UTF-8 nunca usa (0xC0, 0xC1, 0xF5..0xFF).
uint8_t sequenceLength(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  return 0;
}

bool isContinuation(uint8_t byte) { return (byte & 0xC0) == 0x80; }

// Confere se a sequencia inteira esta presente e bem formada, incluindo as faixas que o
// primeiro byte sozinho nao decide: 0xE0 so admite continuacao a partir de 0xA0 (abaixo
// disso e codificacao longa de um caractere que cabia em 2 bytes), 0xED nao pode entrar na
// faixa de surrogate, e 0xF0/0xF4 limitam o plano.
bool sequenceIsValid(const String& value, size_t start, uint8_t length) {
  if (start + length > value.length()) return false;

  const uint8_t lead = static_cast<uint8_t>(value[start]);
  for (uint8_t i = 1; i < length; ++i) {
    if (!isContinuation(static_cast<uint8_t>(value[start + i]))) return false;
  }

  const uint8_t second = length > 1 ? static_cast<uint8_t>(value[start + 1]) : 0;
  if (lead == 0xE0 && second < 0xA0) return false;
  if (lead == 0xED && second >= 0xA0) return false;
  if (lead == 0xF0 && second < 0x90) return false;
  if (lead == 0xF4 && second >= 0x90) return false;

  return true;
}

}  // namespace

String escapeForJsonString(const String& value) {
  String escaped;
  escaped.reserve(value.length());

  size_t i = 0;
  while (i < value.length()) {
    const uint8_t byte = static_cast<uint8_t>(value[i]);

    if (byte < 0x80) {
      switch (byte) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
          // Os outros de controle nao tem atalho e nao podem aparecer crus entre aspas.
          if (byte < 0x20) {
            appendUnicodeEscape(escaped, byte);
          } else {
            escaped += static_cast<char>(byte);
          }
          break;
      }
      ++i;
      continue;
    }

    const uint8_t length = sequenceLength(byte);
    if (length == 0 || !sequenceIsValid(value, i, length)) {
      // Um byte por vez, nao a sequencia toda: o proximo byte pode iniciar uma sequencia
      // valida, e descartar um bloco inteiro apagaria caractere legitimo depois do lixo.
      appendUnicodeEscape(escaped, 0xFFFD);
      ++i;
      continue;
    }

    for (uint8_t offset = 0; offset < length; ++offset) {
      escaped += value[i + offset];
    }
    i += length;
  }

  return escaped;
}

void JsonObject::separate() {
  if (body_.length() > 0) body_ += ",";
}

JsonObject& JsonObject::text(const char* name, const String& value) {
  separate();
  body_ += "\"";
  body_ += name;
  body_ += "\":\"";
  body_ += escapeForJsonString(value);
  body_ += "\"";
  return *this;
}

JsonObject& JsonObject::boolean(const char* name, bool value) {
  return raw(name, value ? "true" : "false");
}

JsonObject& JsonObject::number(const char* name, uint32_t value) {
  return raw(name, numberToString(value));
}

JsonObject& JsonObject::number(const char* name, float value, uint8_t decimals) {
  return raw(name, numberToString(value, decimals));
}

JsonObject& JsonObject::raw(const char* name, const String& json) {
  separate();
  body_ += "\"";
  body_ += name;
  body_ += "\":";
  body_ += json;
  return *this;
}
