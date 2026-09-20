#include "provision_settings.h"

#include "../../include/config.h"
#include "../domain/secret.h"
#include "../domain/timezone.h"

ProvisionResult ProvisionSettingsUseCase::execute() {
  ProvisionResult result;

  if (repository_.load(result.settings)) {
    // Unidade ja provisionada. Nao sorteia nada: sortear a cada boot trocaria a senha do
    // AP debaixo de quem esta associado.
    result.persisted = true;
    return result;
  }

  // Um sorteio so para os dois segredos: a janela de entropia do fillRandomBytes abre e
  // fecha o SAR ADC, e abrir duas vezes nao acrescenta nada.
  uint8_t bytes[kSecretLength * 2];
  entropy_(bytes, sizeof(bytes));

  result.settings.wifi_ssid = DEFAULT_AP_SSID;
  result.settings.wifi_password = secretFromBytes(bytes, kSecretLength);
  result.settings.apn = DEFAULT_APN;
  result.settings.apn_user = DEFAULT_APN_USER;
  result.settings.apn_password = DEFAULT_APN_PASSWORD;
  result.settings.admin_user = DEFAULT_ADMIN_USER;
  result.settings.admin_password = secretFromBytes(bytes + kSecretLength, kSecretLength);
  // A senha sorteada vale para um acesso: sai impressa no serial, e a primeira gravacao
  // pela pagina obriga a troca.
  result.settings.admin_password_pending = true;
  result.settings.timezone = kDefaultTimezone;
  result.settings.battery_divider_ratio = BATTERY_VOLTAGE_DIVIDER_RATIO;
  result.provisioned = true;

  // Valida antes de gravar pelo mesmo motivo do SaveSettingsUseCase: defaults de fabrica
  // invalidos (APN vazio, por exemplo) persistidos deixam a placa subindo sem AP, e sem AP
  // nao ha pagina para desfazer. Este caminho nao passa pelo SaveSettingsUseCase porque e
  // o unico que grava a pendencia da senha de admin, que aquele caso de uso nao conhece.
  if (validate(result.settings) != SettingsValidationError::None) {
    return result;
  }

  result.persisted = repository_.save(result.settings);
  return result;
}
