#include "load_settings.h"

#include "../../include/config.h"

RouterSettings LoadSettingsUseCase::execute() {
  RouterSettings settings;
  if (repository_.load(settings)) {
    return settings;
  }

  // NVS ilegivel. O provisionamento roda no setup() antes de qualquer radio, entao depois
  // dele o load() acima sempre acha registro — chegar aqui significa NVS quebrada, nao
  // primeiro boot.
  //
  // As senhas ficam vazias de proposito. Antes este caminho aplicava DEFAULT_AP_PASSWORD e
  // DEFAULT_ADMIN_PASSWORD, o que reabria a senha compartilhada justamente quando ninguem
  // esta olhando. Senha vazia e reprovada pelo validate() e barra a subida do AP: falha
  // visivel em vez de placa no ar com a senha que esta no GitHub.
  settings.wifi_ssid = DEFAULT_AP_SSID;
  settings.wifi_password = "";
  settings.apn = DEFAULT_APN;
  settings.apn_user = DEFAULT_APN_USER;
  settings.apn_password = DEFAULT_APN_PASSWORD;
  settings.admin_user = DEFAULT_ADMIN_USER;
  settings.admin_password = "";
  return settings;
}
