#include "nvs_settings_repository.h"

#include <Preferences.h>

#include "../../include/config.h"

namespace {
const char* kKeyConfigured = "configured";
const char* kKeySsid = "ssid";
const char* kKeyWifiPass = "wifi_pass";
const char* kKeyApn = "apn";
const char* kKeyApnUser = "apn_user";
const char* kKeyApnPass = "apn_pass";
// Versao do formato gravado. O schema 1 nao tinha as credenciais do APN e guardava
// um APN default que nao existe na rede da Vivo; tratar esse registro como ausente
// e o que faz o dispositivo cair nos defaults novos em vez de insistir no antigo.
const char* kKeySchema = "schema";
const int kCurrentSchema = 2;
const char* kKeyAdminUser = "admin_user";
const char* kKeyAdminPass = "admin_pass";
// Chave nova sem subir o schema, de proposito. O load() trata `schema < kCurrentSchema`
// como registro ausente, entao subir para 3 apagaria SSID, senha e APN de toda unidade ja
// configurada — migracao destrutiva por causa de um booleano nao se paga. Ler com default
// e compativel por construcao: registro do schema 2 nao tem a chave, cai no false e segue
// com a senha de admin que a pessoa escolheu.
const char* kKeyAdminPending = "admin_pend";

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
  if (!configured || prefs.getInt(kKeySchema, 1) < kCurrentSchema) {
    prefs.end();
    return false;
  }

  out.wifi_ssid = prefs.getString(kKeySsid, "");
  out.wifi_password = prefs.getString(kKeyWifiPass, "");
  out.apn = prefs.getString(kKeyApn, "");
  out.apn_user = prefs.getString(kKeyApnUser, "");
  out.apn_password = prefs.getString(kKeyApnPass, "");
  out.admin_user = prefs.getString(kKeyAdminUser, "");
  out.admin_password = prefs.getString(kKeyAdminPass, "");
  out.admin_password_pending = prefs.getBool(kKeyAdminPending, false);

  prefs.end();
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
            wrote(prefs.putString(kKeyAdminPass, settings.admin_password), settings.admin_password);

  // putBool devolve 1 quando gravou, para true e para false (putUChar no core Arduino),
  // entao 0 aqui e falha e nao "gravou false".
  ok = ok && prefs.putBool(kKeyAdminPending, settings.admin_password_pending) != 0;

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
