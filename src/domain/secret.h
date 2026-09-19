#pragma once

#include <cstddef>
#include <cstdint>

#include "string_type.h"

// DOMINIO — transforma bytes de entropia em senha legivel. De onde vem a entropia e
// hardware e mora em infra/; qual alfabeto e quantos caracteres e decisao de produto e
// mora aqui, com teste nativo.

// 32 simbolos, e o 32 nao e estetica: 256 % 32 == 0, entao mapear byte -> simbolo por
// resto e uniforme. Alfabeto de tamanho que nao divide 256 viesa os primeiros simbolos e
// perde entropia sem que nada no comportamento denuncie.
//
// Sao 26 maiusculas mais 6 digitos. Os digitos 0, 1, 5 e 8 ficaram fora porque colidem
// com O, I, S e B na leitura — estas senhas saem de log serial ou de etiqueta e sao
// digitadas a mao, e trocar um caractere custa uma viagem ate o equipamento. Ficaram o 2 e
// o 6, que colidem de leve com Z e G: chegar aos 32 vale mais que o par fraco, e sem
// minusculas o risco cai (nao ha l/I nem O/o).
extern const char kSecretAlphabet[];

// 12 simbolos de 32 = 60 bits. Cabe na passphrase WPA2 (8 a 63) e passa o minimo de 8 que
// o validate() exige da senha de admin.
const size_t kSecretLength = 12;

// Um simbolo por byte. Devolve String vazia para count == 0.
String secretFromBytes(const uint8_t* bytes, size_t count);
