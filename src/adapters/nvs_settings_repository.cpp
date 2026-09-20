#include "nvs_settings_repository.h"

#include <Preferences.h>

#include "../../include/config.h"
#include "../domain/settings_migration.h"

namespace {
const char* kKeyConfigured = "configured";
const char* kKeySsid = "ssid";
const char* kKeyWifiPass = "wifi_pass";
const char* kKeyApn = "apn";
const char* kKeyApnUser = "apn_user";
const char* kKeyApnPass = "apn_pass";
// Versao do formato gravado. O numero e a politica de cada versao vivem em
// `domain/settings_migration`; aqui fica so a chave.
const char* kKeySchema = "schema";
const char* kKeyAdminUser = "admin_user";
const char* kKeyAdminPass = "admin_pass";
// Chave lida com default, sem ter subido o schema. Na epoca (debito 10) isso era
// obrigatorio: registro de schema menor era descartado, entao subir para 3 apagaria SSID,
// senha e APN de toda unidade configurada. Com `domain/settings_migration` isso deixou de
// ser verdade, mas o padrao continua sendo o melhor para campo novo com default seguro —
// registro do schema 2 nao tem a chave, cai no false e segue com a senha que a pessoa
// escolheu, sem precisar de degrau de migracao nenhum.
const char* kKeyAdminPending = "admin_pend";
// Chaves do schema 3. Entraram com degrau de migracao em `domain/settings_migration`, e
// nao lidas com default como o `admin_pend`: nenhuma das duas tem default seguro que o
// dominio aceite — fuso vazio e ratio 0.0 sao reprovados pelo validate(), de proposito.
const char* kKeyTimezone = "tz";
const char* kKeyBatteryRatio = "bat_ratio";

// putString devolve strlen(value) quando gravou e 0 quando falhou (erro no nvs_set_str ou
// no nvs_commit — particao cheia cai aqui). Comparar com o comprimento esperado e o que
// separa "gravou vazio" de "nao gravou": apn_user e apn_password sao opcionais e vazios de
// proposito, entao um criterio `> 0` reprovaria justamente o caso legitimo.
//
// Ponto cego que sobra: falha ao gravar campo vazio tambem devolve 0, e 0 == 0 passa por
// sucesso. Separar os dois exigiria reler a chave. Na pratica nao esconde nada: as causas
// de falha derrubam o handle inteiro, e os outros cinco campos nao sao vazios.
bool wrote(size_t written, const String& value) {
  return written == value.length();
}
}  // namespace

bool NvsSettingsRepository::load(RouterSettings& out) {
  Preferences prefs;
  // Sem namespace gravado ainda, nvs_open em modo leitura falha e begin() devolve false.
  // Antes isso caia no getBool com default false e dava no mesmo por acidente; explicito
  // porque o acidente depende do default e some se alguem mexer nele.
  if (!prefs.begin(SETTINGS_NVS_NAMESPACE, /*readOnly=*/true)) return false;

  bool configured = prefs.getBool(kKeyConfigured, false);
  if (!configured) {
    prefs.end();
    return false;
  }

  // Registro gravado antes de existir a chave conta como schema 1 — era o formato da
  // epoca.
  int storedSchema = prefs.getInt(kKeySchema, 1);

  // Numa temporaria, nao direto no `out`: registro recusado nao pode deixar o chamador
  // com meia configuracao lida, porque `load()` falso significa "nao ha registro".
  RouterSettings stored;
  stored.wifi_ssid = prefs.getString(kKeySsid, "");
  stored.wifi_password = prefs.getString(kKeyWifiPass, "");
  stored.apn = prefs.getString(kKeyApn, "");
  stored.apn_user = prefs.getString(kKeyApnUser, "");
  stored.apn_password = prefs.getString(kKeyApnPass, "");
  stored.admin_user = prefs.getString(kKeyAdminUser, "");
  stored.admin_password = prefs.getString(kKeyAdminPass, "");
  stored.admin_password_pending = prefs.getBool(kKeyAdminPending, false);
  // Defaults que o degrau 2 -> 3 sobrescreve. Ficam invalidos de proposito: se o degrau
  // nao rodar por engano, o validate() reprova em vez de a placa operar com fuso errado e
  // bateria lida como 0 V.
  stored.timezone = prefs.getString(kKeyTimezone, "");
  stored.battery_divider_ratio = prefs.getFloat(kKeyBatteryRatio, 0.0f);

  prefs.end();

  MigrationDefaults defaults;
  defaults.apn = DEFAULT_APN;
  defaults.apn_user = DEFAULT_APN_USER;
  defaults.apn_password = DEFAULT_APN_PASSWORD;
  defaults.battery_divider_ratio = BATTERY_VOLTAGE_DIVIDER_RATIO;

  // A migracao acontece em memoria e nao regrava nada. De proposito: `load()` que escreve
  // surpreende, e a consolidacao vem de graca no primeiro `save()`, que sempre grava o
  // schema atual. Ate la a unidade continua migrando a cada boot, o que nao custa nada
  // porque `migrateSettings()` e puro e idempotente.
  if (migrateSettings(storedSchema, defaults, stored) == SchemaVerdict::Rejected) {
    return false;
  }

  out = stored;
  return true;
}

bool NvsSettingsRepository::save(const RouterSettings& settings) {
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NVS_NAMESPACE, /*readOnly=*/false)) return false;

  // Curto-circuito de proposito: se uma chave falhou, insistir nas seguintes nao ajuda —
  // a causa (particao cheia, handle invalido) vale para todas.
  bool ok = wrote(prefs.putString(kKeySsid, settings.wifi_ssid), settings.wifi_ssid) &&
            wrote(prefs.putString(kKeyWifiPass, settings.wifi_password), settings.wifi_password) &&
            wrote(prefs.putString(kKeyApn, settings.apn), settings.apn) &&
            wrote(prefs.putString(kKeyApnUser, settings.apn_user), settings.apn_user) &&
            wrote(prefs.putString(kKeyApnPass, settings.apn_password), settings.apn_password) &&
            wrote(prefs.putString(kKeyAdminUser, settings.admin_user), settings.admin_user) &&
            wrote(prefs.putString(kKeyAdminPass, settings.admin_password), settings.admin_password) &&
            wrote(prefs.putString(kKeyTimezone, settings.timezone), settings.timezone);

  // putBool devolve 1 quando gravou, para true e para false (putUChar no core Arduino),
  // entao 0 aqui e falha e nao "gravou false".
  ok = ok && prefs.putBool(kKeyAdminPending, settings.admin_password_pending) != 0;

  // putFloat devolve 4 quando grava (sizeof(float)) e 0 quando falha. Sem a ambiguidade do
  // campo vazio das strings: nao existe float de comprimento zero.
  ok = ok && prefs.putFloat(kKeyBatteryRatio, settings.battery_divider_ratio) != 0;

  // putInt devolve 4 e putBool devolve 1 quando gravam — valores fixos, entao aqui 0 so
  // pode ser falha e nao ha a ambiguidade do campo vazio.
  ok = ok && prefs.putInt(kKeySchema, kCurrentSchema) != 0;
  // `configured` por ultimo: se algo acima falhou, a flag nao e reescrita. Nao torna a
  // gravacao atomica — a Preferences faz nvs_commit por chave e nao oferece transacao,
  // entao uma falha no meio deixa campos novos e antigos misturados. O que este ultimo
  // passo garante e que a falha seja reportada, nao escondida atras de um "salvo".
  ok = ok && prefs.putBool(kKeyConfigured, true) != 0;

  prefs.end();
  return ok;
}
