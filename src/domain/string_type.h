#pragma once

#include <cstdint>

// O dominio precisa de String, e na placa String e a do core Arduino. Fora dela o
// Arduino.h nao existe, e sem este desvio a camada "pura" nao compila no host — que era
// justamente o que impedia testar validate() sem hardware (debito 19).
//
// std::string no lugar, e nao um shim escrito a mao: o dominio so usa length(), indexacao
// e concatenacao, e nos dois tipos isso significa a mesma coisa sobre os mesmos bytes.
// O que o teste nativo NAO cobre e qualquer comportamento em que os tipos divirjam
// (conversao implicita, semantica de copia em falta de memoria) — nada disso aparece aqui,
// mas aparece se o dominio crescer.
//
// Mora em arquivo proprio desde que a segunda entidade precisou do mesmo desvio: duplicar
// o #ifdef daria duas definicoes de String para manter em sincronia.
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

// Numero como texto, pelos dois caminhos. O dominio precisa disto porque `String += int`
// existe no Arduino e nao existe em std::string — escrever direto compilaria na placa e
// quebraria no host, que e exatamente o tipo de divergencia que o seam existe para evitar.
inline String numberToString(uint32_t value) {
#ifdef ARDUINO
  return String(value);
#else
  return std::to_string(value);
#endif
}
