#include "settings_migration.h"

#include "timezone.h"

namespace {

// O APN que o schema 1 gravava como default. Nao funciona na rede da Vivo, que e onde
// estas placas rodam — era por causa dele que o registro do schema 1 era descartado
// inteiro. Guardar o valor permite o contrario: descartar o APN e preservar o resto.
const char* kSchema1DefaultApn = "internet";

}  // namespace

SchemaVerdict migrateSettings(int storedSchema, const MigrationDefaults& defaults,
                              RouterSettings& settings) {
  // Schema 0 ou negativo nao foi gravado por nenhuma versao: ou a chave nao existe e o
  // adaptador leu um default, ou o registro esta corrompido. Nos dois casos nao ha o que
  // migrar, porque nao se sabe o que os outros campos significam.
  if (storedSchema <= 0) return SchemaVerdict::Rejected;

  // Registro mais novo que este firmware (downgrade). Lido como esta, de proposito: os
  // campos que esta versao conhece continuam onde sempre estiveram, e o que ela nao
  // conhece ela ignora. Recusar seria pior — faria o provisionamento sortear senha nova e
  // derrubar quem esta associado, para proteger de um campo extra que nao atrapalha.
  if (storedSchema >= kCurrentSchema) return SchemaVerdict::Current;

  // Degraus encadeados, sem `else` e sem return no meio: um registro de schema 1 numa
  // unidade que nunca foi atualizada atravessa 1 -> 2 -> 3 numa passada so. Parar no 2
  // deixaria o fuso vazio, que o validate() reprova.
  if (storedSchema <= 1) {
    // Se o APN ainda e o default antigo, a pessoa nunca escolheu um: repor o atual e o
    // que faz a unidade voltar a conectar. Se e outro valor, foi escolha dela e fica.
    if (settings.apn == kSchema1DefaultApn) {
      settings.apn = defaults.apn;
      settings.apn_user = defaults.apn_user;
      settings.apn_password = defaults.apn_password;
    }
    // As credenciais do APN nao existiam no schema 1. Quando o APN e escolhido, ficam
    // vazias — inventar usuario e senha da Vivo para o APN de outra operadora seria criar
    // dado que ninguem forneceu. APN sem autenticacao e caso legitimo (router_settings.h).
  }

  if (storedSchema <= 2) {
    // Dois campos novos, os dois com default seguro: o registro do schema 2 nao tem as
    // chaves, o adaptador le o vazio do struct e o degrau preenche. Nada do que ja estava
    // gravado e tocado — era exatamente esse o defeito que a migracao existe pra corrigir.
    settings.timezone = kDefaultTimezone;
    settings.battery_divider_ratio = defaults.battery_divider_ratio;
  }

  return SchemaVerdict::Migrated;
}
