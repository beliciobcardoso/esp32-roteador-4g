#include "secret.h"

const char kSecretAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234679";

namespace {
// Amarra a promessa do header: se alguem editar o alfabeto e sair de 32, o mapeamento por
// resto passa a ser viesado. Falhar na compilacao e melhor que gerar senha mais fraca em
// silencio.
static_assert(sizeof(kSecretAlphabet) - 1 == 32,
              "kSecretAlphabet precisa ter 32 simbolos para o mapeamento ser uniforme");
}  // namespace

String secretFromBytes(const uint8_t* bytes, size_t count) {
  String secret;
  for (size_t i = 0; i < count; ++i) {
    secret += kSecretAlphabet[bytes[i] % 32];
  }
  return secret;
}
