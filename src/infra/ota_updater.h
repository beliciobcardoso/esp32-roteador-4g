#pragma once

#include <Arduino.h>

#include "../adapters/firmware_writer.h"
#include "../domain/firmware_update.h"

// INFRA — grava o firmware novo no slot livre e conta o que a imagem em execucao e.
//
// Grava pelo `Update` do Arduino em vez de chamar esp_ota_begin/write/end direto: ele ja
// apaga a flash por bloco conforme o upload anda (sem parada de segundos no inicio, que
// estouraria o timeout do navegador), e segura os 16 primeiros bytes ate o fim — imagem
// interrompida no meio fica sem cabecalho e o bootloader nao a escolhe.
//
// O `Update.end()` chama `esp_ota_set_boot_partition()`, que roda `esp_image_verify()`
// antes de mexer no otadata: e ali que o SHA-256 da propria imagem (`hash_appended`,
// esp_app_format.h) reprova upload truncado ou corrompido. Por isso nao ha checksum no
// formulario — seria conferir os mesmos bytes duas vezes.
//
// O rollback continua valendo por este caminho: com CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// o `esp_ota_set_boot_partition()` grava o otadata como ESP_OTA_IMG_NEW
// (`set_new_state_otadata()`, app_update/esp_ota_ops.c) e o bootloader passa para
// PENDING_VERIFY no primeiro boot da imagem nova.
//
// Sem estado proprio: o `Update` e um global da lib e ter um segundo lugar guardando
// "estou gravando" so criaria divergencia entre os dois.
class OtaUpdater : public FirmwareWriter {
 public:
  // Abre o slot de destino. Nao recebe tamanho de proposito: o corpo multipart do
  // WebServer nao diz quanto dele e imagem, entao o `Update` reserva a particao inteira e
  // o tamanho real so aparece no fim do upload.
  bool begin() override;

  // Devolve falso se a lib aceitou menos bytes do que os entregues — acontece quando o
  // apagamento ou a escrita da flash falha, e continuar mandaria o resto do arquivo para
  // um slot com buraco no meio.
  bool write(unsigned char* data, size_t length) override;

  // Fecha e ativa o slot. Aqui dentro roda a verificacao da imagem inteira.
  bool finish() override;

  void abort() override;

  // Texto do ultimo erro do `Update` (em ingles, vindo da lib). So para o log — a pagina
  // responde com as frases do dominio.
  String lastErrorText() const override;

  // Tamanho do slot que receberia a imagem, ou 0 se nao deu para consultar a particao. E
  // o teto que o inspectImageSize() usa.
  size_t targetSlotSize() const override;

  String runningSlotLabel() const override;

  // Versao, data/hora de compilacao e os 4 primeiros bytes do SHA-256 do .elf. O sha esta
  // ai porque duas compilacoes da mesma versao tem o mesmo texto e conteudo diferente —
  // sem ele nao da para saber qual das duas esta na placa.
  String runningVersionText() const override;

  FirmwareImageState runningImageState() const override;

  // Marca a imagem em execucao como boa e cancela o rollback. So chamar quando o
  // needsHealthConfirmation() concordar.
  bool confirmRunningImage();

  // Marca a imagem em execucao como ruim e reinicia no slot anterior.
  //
  // Devolve falso quando a IDF recusa, e so nesse caso devolve alguma coisa: dando certo,
  // a placa reinicia de dentro da chamada. O motivo pratico da recusa e nao existir outro
  // slot com imagem valida — "do not have any suitable apps in slots" —, o que acontece
  // depois de uma sequencia de reverts que gastou a imagem do outro lado.
  //
  // O reboot e ativo de proposito. O rollback do bootloader e passivo: ele so acontece se
  // alguma coisa resetar a placa. Um firmware que sobe, roda e nao atende ninguem nunca
  // reseta sozinho, e ficaria de pe e inalcancavel para sempre.
  bool revertToPreviousImage();
};
