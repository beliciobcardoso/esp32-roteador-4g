#include "firmware_update.h"

const unsigned char kEspImageMagic = 0xE9;
const size_t kEspImageHeaderSize = 24;

// Cinco minutos. Cobrem o que o rollback existe para pegar — crash no setup(), watchdog na
// subida do modem, panico nos primeiros ciclos do loop() — e ainda alcancam o defeito que
// so aparece depois da primeira reconexao de PPP, que dois minutos perdiam.
//
// A margem contra o LinkSupervisor encolheu de proposito: o piso dele e pouco mais de 7 min
// (10 falhas com backoff de 5/10/20/40/60 s), entao sobram ~2 min em vez dos ~5 de antes.
// Continua seguro porque aquele piso supoe toda tentativa falhando instantaneamente, e na
// placa cada ciclo de reconexao gasta ~15 s so entre o ERRORPEERDEAD e o Connected. Nao
// esticar mais sem mexer no supervisor junto: a partir daqui a folga vira ruido.
const unsigned long kConfirmationDeadlineMs = 600000;

const char* to_string(FirmwareUpdateError error) {
  switch (error) {
    case FirmwareUpdateError::None:
      return "ok";
    case FirmwareUpdateError::EmptyImage:
      return "nenhum arquivo foi enviado";
    case FirmwareUpdateError::TooShortToBeAnImage:
      return "arquivo pequeno demais para ser um firmware";
    case FirmwareUpdateError::NotAnEspImage:
      return "arquivo nao e uma imagem de firmware do ESP32 (envie o firmware.bin)";
    case FirmwareUpdateError::TooLargeForSlot:
      return "firmware maior do que a particao de destino";
  }
  return "erro desconhecido";
}

FirmwareUpdateError inspectImageHead(const unsigned char* head, size_t length) {
  if (head == nullptr || length == 0) return FirmwareUpdateError::EmptyImage;
  if (length < kEspImageHeaderSize) return FirmwareUpdateError::TooShortToBeAnImage;
  if (head[0] != kEspImageMagic) return FirmwareUpdateError::NotAnEspImage;
  return FirmwareUpdateError::None;
}

FirmwareUpdateError inspectImageSize(size_t imageSize, size_t slotSize) {
  if (imageSize == 0) return FirmwareUpdateError::EmptyImage;
  if (slotSize == 0 || imageSize > slotSize) return FirmwareUpdateError::TooLargeForSlot;
  return FirmwareUpdateError::None;
}

String describeFirmwareImageState(FirmwareImageState state) {
  switch (state) {
    case FirmwareImageState::Valid:
      return "confirmado";
    case FirmwareImageState::PendingVerify:
      // O aviso e o ponto da frase: quem reinicia aqui perde a atualizacao e nao entende
      // por que a placa voltou com a versao velha.
      return "em verificacao — nao reinicie ainda, reiniciar agora volta para o firmware anterior";
    case FirmwareImageState::Unmarked:
      return "gravado por serial (sem rollback)";
    case FirmwareImageState::Unknown:
      return "estado desconhecido";
  }
  return "estado desconhecido";
}

bool needsHealthConfirmation(FirmwareImageState state) {
  return state == FirmwareImageState::PendingVerify;
}

FirmwareConfirmationOutcome decideFirmwareConfirmation(FirmwareImageState state,
                                                       FirmwareHealth health,
                                                       bool operator_confirmed,
                                                       unsigned long uptimeMs) {
  if (!needsHealthConfirmation(state)) return FirmwareConfirmationOutcome::Nothing;

  // Antes da saude de proposito: o clique so pode ter chegado pelo AP e pelo servidor, e
  // uma leitura que os desminta esta errada.
  if (operator_confirmed) return FirmwareConfirmationOutcome::Confirm;

  if (!health.ap_up || !health.http_up) return FirmwareConfirmationOutcome::Revert;

  if (uptimeMs >= kConfirmationDeadlineMs) return FirmwareConfirmationOutcome::Revert;

  return FirmwareConfirmationOutcome::KeepWaiting;
}

bool supervisorMayRebootForUplink(FirmwareImageState state) {
  return !needsHealthConfirmation(state);
}
