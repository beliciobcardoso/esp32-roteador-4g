#pragma once

#include <cstddef>
#include <cstdint>

// INFRA — bytes de entropia real, inclusive antes do radio subir. Transformar byte em
// senha legivel e do dominio (domain/secret.h).
void fillRandomBytes(uint8_t* buffer, size_t length);
