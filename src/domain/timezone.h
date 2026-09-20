#pragma once

#include <cstddef>

#include "string_type.h"

// ENTIDADE — os fusos que o roteador aceita e a string POSIX TZ de cada um.
//
// Isto e regra, nao UI. O ESP-IDF nao embarca tzdata: so aceita string POSIX passada por
// `setenv("TZ", ...)` + `tzset()`, e nenhum dos dois devolve erro para uma string sem
// sentido — o resultado e hora errada em silencio. O <select> da pagina nao protege nada,
// porque um POST direto manda o que quiser. A tabela e a fonte das opcoes E do que o
// validate() aceita, pelos dois caminhos.
//
// Quatro opcoes cobrem o pais. Nenhuma precisa de regra de transicao: o Brasil nao tem
// horario de verao desde 2019 (Decreto 9.772/2019).

struct TimezoneOption {
  // Valor gravado na NVS e entregue ao setenv(). E o que o formulario envia.
  const char* posix;
  // O que a pessoa le no <select>. Nunca e gravado.
  const char* label;
};

// Fuso que o firmware grava sozinho: no provisionamento de fabrica e no degrau de migracao
// do schema 2 para o 3. E a primeira entrada da tabela por construcao — um default fora da
// tabela faria o validate() reprovar a propria configuracao de fabrica, e sem configuracao
// valida o AP nao sobe.
extern const char* const kDefaultTimezone;

const TimezoneOption* timezoneOptions();
size_t timezoneOptionCount();

bool isKnownTimezone(const String& posix);

// Rotulo correspondente, ou nullptr quando o valor nao esta na tabela.
const char* timezoneLabel(const String& posix);
