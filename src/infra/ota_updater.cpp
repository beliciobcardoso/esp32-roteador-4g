#include "ota_updater.h"

#include <Update.h>
#include <esp_ota_ops.h>

bool OtaUpdater::begin() {
  // UPDATE_SIZE_UNKNOWN nao e "tamanho qualquer": o Update troca isso pelo tamanho da
  // particao de destino e recusa escrita alem dela. O teto continua existindo.
  return Update.begin(UPDATE_SIZE_UNKNOWN);
}

bool OtaUpdater::write(unsigned char* data, size_t length) {
  // Bloco vazio chega quando o arquivo termina exatamente na fronteira do buffer do
  // WebServer. Nao e erro, e mandar para a lib so gastaria caminho.
  if (data == nullptr || length == 0) return true;
  return Update.write(data, length) == length;
}

bool OtaUpdater::finish() {
  // `true` = aceitar o fim com o "restante" nao escrito. O restante e o pedaco do slot que
  // sobra depois da imagem, porque o tamanho reservado foi a particao inteira. Quem
  // verifica se a imagem esta completa e o esp_image_verify() la dentro, nao esta conta.
  return Update.end(true);
}

void OtaUpdater::abort() {
  if (Update.isRunning()) Update.abort();
}

String OtaUpdater::lastErrorText() const {
  return String(Update.errorString());
}

size_t OtaUpdater::targetSlotSize() const {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  return target == nullptr ? 0 : target->size;
}

String OtaUpdater::runningSlotLabel() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running == nullptr ? String("desconhecido") : String(running->label);
}

String OtaUpdater::runningVersionText() const {
  const esp_app_desc_t* description = esp_ota_get_app_description();
  if (description == nullptr) return "desconhecida";

  String text = String(description->version);
  if (text.length() == 0) text = "sem versao";
  text += " (";
  text += description->date;
  text += " ";
  text += description->time;
  text += ") build ";
  for (int i = 0; i < 4; ++i) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02x", description->app_elf_sha256[i]);
    text += hex;
  }
  return text;
}

FirmwareImageState OtaUpdater::runningImageState() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) return FirmwareImageState::Unknown;

  esp_ota_img_states_t state;
  esp_err_t error = esp_ota_get_state_partition(running, &state);

  // ESP_ERR_NOT_FOUND e o caso normal de placa gravada por serial: o otadata esta em
  // branco e nao ha registro desta imagem nele. Nao e defeito, e nao ha rollback armado.
  if (error == ESP_ERR_NOT_FOUND) return FirmwareImageState::Unmarked;
  if (error != ESP_OK) return FirmwareImageState::Unknown;

  switch (state) {
    case ESP_OTA_IMG_VALID:
      return FirmwareImageState::Valid;
    // NEW e o que o esp_ota_set_boot_partition() grava; o bootloader passa para
    // PENDING_VERIFY no primeiro boot. Ver NEW rodando significa bootloader sem
    // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE lendo otadata gravado por um que tinha — a
    // imagem esta sob a mesma regra, entao vale a mesma resposta.
    case ESP_OTA_IMG_NEW:
    case ESP_OTA_IMG_PENDING_VERIFY:
      return FirmwareImageState::PendingVerify;
    case ESP_OTA_IMG_UNDEFINED:
      return FirmwareImageState::Unmarked;
    // INVALID e ABORTED existem no otadata, mas o bootloader nao escolhe uma imagem
    // nesses estados — ler isso da imagem em execucao significa que a leitura mentiu.
    default:
      return FirmwareImageState::Unknown;
  }
}

bool OtaUpdater::confirmRunningImage() {
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}
