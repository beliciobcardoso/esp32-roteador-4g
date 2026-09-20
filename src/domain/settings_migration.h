#pragma once

#include "router_settings.h"
#include "string_type.h"

// DOMINIO — o que fazer com um registro gravado por uma versao anterior do firmware.
//
// Existe separado do adaptador de NVS por dois motivos. O primeiro e testabilidade: o
// `pio test -e native` so compila `src/domain/`, entao politica que viva no adaptador nao
// tem onde ser testada. O segundo e que isso e regra, nao armazenamento — decidir que uma
// unidade em campo nao pode perder o SSID por causa de um campo novo nao depende de a
// persistencia ser NVS.
//
// Antes daqui a resposta era sempre a mesma: registro com schema menor que o atual era
// tratado como ausente. Isso ja era destrutivo, e depois do provisionamento por unidade
// (debito 10) virou pior — `load()` falso faz o ProvisionSettingsUseCase **sortear senha
// nova**, entao subir o schema derrubaria todos os clientes de uma unidade em campo e a
// senha nova so existiria no serial, que ninguem esta lendo.

// Versao do formato gravado. Mora aqui, e nao no adaptador, porque e a politica que decide
// o que cada versao significa.
const int kCurrentSchema = 3;

enum class SchemaVerdict {
  // Registro ilegivel: numero de schema que nenhuma versao do firmware gravou.
  Rejected,
  // Registro antigo, ajustado em memoria para o formato atual.
  Migrated,
  // Nada a fazer.
  Current,
};

// Valores de fabrica que a migracao precisa repor quando o registro antigo guardava algo
// sabidamente inutil. Vem de fora porque `include/config.h` e configuracao de build, e o
// dominio nao depende dela.
struct MigrationDefaults {
  String apn;
  String apn_user;
  String apn_password;
  // Calibracao do divisor resistivo. Vem de fora e o fuso nao: o fuso default e escolha
  // de produto e mora em `domain/timezone.h`, onde esta garantido de ser uma opcao valida;
  // o ratio e calibracao por placa e mora no `config.h` como os outros defaults de fabrica.
  float battery_divider_ratio = 0.0f;
};

// Ajusta `settings` do formato `storedSchema` para o atual. Nao toca em nada quando
// devolve `Rejected` ou `Current`.
SchemaVerdict migrateSettings(int storedSchema, const MigrationDefaults& defaults,
                              RouterSettings& settings);
