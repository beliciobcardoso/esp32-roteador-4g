#pragma once

#include <cstddef>

#include "string_type.h"

// ENTIDADE — o que a placa aceita como firmware novo, e o que ela pode dizer sobre a
// imagem que esta rodando. Regra pura: nao conhece esp_ota, Update nem HTTP.
//
// O exame da imagem aqui e deliberadamente raso. Quem valida de verdade e o
// `esp_image_verify()` que o `esp_ota_set_boot_partition()` roda antes de trocar o otadata
// (`image_validate()` em `app_update/esp_ota_ops.c`), e ele confere o SHA-256 que o
// proprio formato carrega (`hash_appended`, `esp_app_format.h`) — upload truncado ou
// corrompido nao passa por ali. O que este arquivo faz e barrar o arquivo obviamente
// errado *antes* de apagar o slot inteiro, e dizer por que em portugues em vez de
// devolver "MAGIC_BYTE".

enum class FirmwareUpdateError {
  None,
  EmptyImage,
  TooShortToBeAnImage,
  NotAnEspImage,
  TooLargeForSlot,
};

const char* to_string(FirmwareUpdateError error);

// Primeiro byte de toda imagem de app do ESP32 (`ESP_IMAGE_HEADER_MAGIC`,
// `esp_app_format.h`). Um `.elf` comeca com 0x7F, que e o engano mais comum.
extern const unsigned char kEspImageMagic;

// `sizeof(esp_image_header_t)`, fixado por ESP_STATIC_ASSERT no proprio header da IDF.
extern const size_t kEspImageHeaderSize;

// Chamado no primeiro bloco recebido, antes de gravar qualquer coisa. Ponteiro nulo conta
// como upload vazio: o chamador roda dentro do parser do WebServer, e desreferenciar ali
// derruba o AP junto — sobraria uma placa sem pagina para consertar nada.
FirmwareUpdateError inspectImageHead(const unsigned char* head, size_t length);

// Chamado quando o upload termina. Nao da para checar antes: o `WebServer` nao expoe o
// Content-Length, e mesmo se expusesse ele mede o corpo multipart inteiro, com fronteiras
// e cabecalhos de parte, nao a imagem.
//
// `slotSize` zero significa que nao se conseguiu consultar a particao. Recusa tudo de
// proposito — tratar como "cabe" seria aceitar qualquer tamanho justo quando o limite e
// desconhecido.
FirmwareUpdateError inspectImageSize(size_t imageSize, size_t slotSize);

// Estado da imagem em execucao, espelhando `esp_ota_img_states_t` sem arrastar a IDF para
// o dominio.
enum class FirmwareImageState {
  Valid,          // confirmada; reiniciar e seguro
  PendingVerify,  // recem-gravada por OTA, ainda em janela de verificacao
  Unmarked,       // imagem gravada por serial, ou otadata sem registro dela
  Unknown,        // nao deu para ler o estado
};

// Frase para quem abriu a pagina. Responde uma pergunta so: posso reiniciar esta placa
// agora? Em PendingVerify a resposta e nao, e isso precisa estar escrito — reiniciar
// dentro da janela faz o bootloader reverter um firmware que estava funcionando.
String describeFirmwareImageState(FirmwareImageState state);

// So PendingVerify precisa de confirmacao. Nos outros estados nao ha o que confirmar: em
// Valid o `esp_ota_mark_app_valid_cancel_rollback()` nao faz nada, e em Unmarked — placa
// gravada por serial, otadata em branco — ele devolve ESP_FAIL com "Running firmware is
// factory" no log. Chamar ali seria erro a cada boot sem defeito nenhum por tras.
bool needsHealthConfirmation(FirmwareImageState state);

// Quanto tempo de pe a imagem nova precisa antes de ser confirmada. Confirmar no setup()
// tornaria o rollback quase inutil: pegaria so o firmware que morre antes do AP subir.
//
// O teto vem do LinkSupervisor, que reinicia a placa depois de 10 falhas de uplink. Com
// backoff de 5/10/20/40/60 s isso passa de 7 min mesmo se toda tentativa falhasse na hora,
// e o reboot dele e por falta de sinal — nao e defeito do firmware. A janela tem que
// terminar bem antes, ou uma area sem cobertura reverte uma atualizacao boa.
extern const unsigned long kVerificationWindowMs;

bool verificationWindowElapsed(unsigned long uptimeMs);
