#include "html_page.h"

String escapeForHtmlAttribute(const String& value) {
  String escaped;
  escaped.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value[i];
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      // Este nao e escape de HTML: '{' vira entidade para que um valor gravado nao consiga
      // forjar um placeholder. Sem isso um SSID igual a "{{APN}}" atravessa o replace do
      // SSID intacto e o replace seguinte o troca pelo APN, enchendo o campo errado.
      // Renderiza como '{' do mesmo jeito.
      case '{': escaped += "&#123;"; break;
      default: escaped += character; break;
    }
  }

  return escaped;
}
