#pragma once

#include "string_type.h"

// DOMAIN — modelo e firmware do modem, lidos do AT+SIMCOMATI no boot (PRD 15).
//
// Existe por dois motivos, e o segundo vale sozinho. O primeiro e o pre-requisito do GNSS:
// nem todo A7670E tem receptor, e a diferenca esta no sufixo do modelo, nao no nome do
// produto — a etiqueta da placa desta bancada diz so "A7670E". O segundo e inventario: saber
// modelo e firmware do modem de cada unidade em campo hoje nao existe, e sai de graca no boot.
//
// O IMEI vem na mesma resposta e fica de fora de proposito. E identificador de assinante,
// nao de hardware, e nada aqui precisa dele.

struct ModemIdentity {
  String model;     // "A7670E-FASE"; vazio quando a resposta nao trouxe a linha
  String revision;  // versao de firmware do modem, como o fabricante escreve
};

// Le a resposta inteira do AT+SIMCOMATI, eco e OK incluidos. Linhas desconhecidas sao
// ignoradas, e campo ausente fica vazio em vez de inventado.
ModemIdentity parseSimcomati(const String& response);

// Tem receptor GNSS interno? Na familia A7670 so as variantes -FASE tem, segundo a tabela do
// repo canonico da LilyGO transcrita no PRD 15 (A7670E-FASE e A7670SA-FASE sim; -LASE,
// -LASC, -LNXY e as G nao). Modelo vazio responde false: sem leitura nao ha afirmacao.
bool modemHasGnss(const String& model);

// Linha de serial com o resultado. Sem acento pelo mesmo motivo das outras do serial.
String describeModem(const ModemIdentity& identity);
