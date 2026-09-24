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

// Motivo curto, ASCII e sem espaco, para a linha do serial. O to_string() acima e para a
// pagina, com acento e frase inteira; o serial fica sem acento para continuar greppavel, e um
// token fixo e mais facil de filtrar do que uma frase que alguem vai reescrever.
const char* updateReasonToken(FirmwareUpdateError error);

// Linha unica do serial com o desfecho de um POST /update (debito 23). `reason` nulo e o
// caso de sucesso. Sai uma por requisicao, nunca por bloco recebido: o upload chega em
// centenas de blocos, e uma linha por bloco afogaria o serial como o debito 13 afogava.
//
// Os bytes recebidos vao nos dois desfechos porque sao o que separa "a placa recusou o
// arquivo" de "o navegador mandou outra coisa" — foi a pergunta sem resposta na validacao
// de 20/09/2026, quando o navegador abortou o envio e o serial ficou mudo nos dois casos.
String describeUpdateOutcome(const char* reason, uint32_t receivedBytes);

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

// Prazo para o operador abrir a pagina e clicar em confirmar. Nao ha mais confirmacao
// automatica por tempo: ficar de pe nao prova que alguem consegue chegar na placa, e era
// justamente um firmware que sobe, roda e nao atende que passava batido.
//
// 10 min passa dos 7 min em que o LinkSupervisor reiniciava a placa por falta de sinal. E
// deliberado, e so e seguro porque supervisorMayRebootForUplink() segura aquele reboot
// enquanto a confirmacao estiver pendente — sem isso, uma area sem cobertura reverteria
// uma atualizacao boa antes de o operador chegar.
extern const unsigned long kConfirmationDeadlineMs;

// O que a placa conseguiu levantar deste boot. Nao e diagnostico completo: sao as duas
// coisas sem as quais ninguem consegue entrar para consertar nada.
struct FirmwareHealth {
  bool ap_up = false;
  bool http_up = false;
};

enum class FirmwareConfirmationOutcome {
  Nothing,      // a imagem nao esta em janela de verificacao; nao ha o que decidir
  KeepWaiting,  // dentro do prazo e de pe: espera o clique
  Confirm,      // confirma e cancela o rollback
  Revert,       // volta para a imagem anterior
};

// Decide o destino da imagem recem-gravada. Regra pura: recebe o estado, o que a placa
// levantou, se o operador clicou e ha quanto tempo ela esta de pe.
//
// O clique ganha de uma leitura de saude ruim de proposito. Se o POST chegou, o AP subiu e
// o servidor respondeu — foi por eles que ele veio. Uma flag dizendo o contrario esta
// errada, e a prova empirica vale mais que a leitura.
//
// Saude ruim reverte na hora, sem esperar o prazo: esperar 10 min por um clique que so
// poderia vir por um caminho que nao existe nao acrescenta informacao nenhuma.
FirmwareConfirmationOutcome decideFirmwareConfirmation(FirmwareImageState state,
                                                       FirmwareHealth health,
                                                       bool operator_confirmed,
                                                       unsigned long uptimeMs);

// O LinkSupervisor reinicia a placa depois de 10 falhas de uplink — com backoff de
// 5/10/20/40/60 s, a partir de ~7 min. Dentro da janela de confirmacao esse reboot faria o
// bootloader reverter uma imagem que talvez estivesse boa, por um motivo que nao e defeito
// do firmware: falta de sinal.
//
// Enquanto a confirmacao estiver pendente ele segura. O custo e perder ate 10 min de
// tentativas de reconexao quando o firmware novo e justamente o que quebrou o modem — e
// esse caso termina em Revert no fim do prazo de qualquer jeito.
bool supervisorMayRebootForUplink(FirmwareImageState state);
