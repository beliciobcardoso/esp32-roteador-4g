#include "settings_migration.h"

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

  if (storedSchema == 1) {
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

  return SchemaVerdict::Migrated;
}
