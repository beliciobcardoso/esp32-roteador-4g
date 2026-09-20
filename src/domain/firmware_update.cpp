#include "firmware_update.h"

const unsigned char kEspImageMagic = 0xE9;
const size_t kEspImageHeaderSize = 24;

// Dois minutos. Cabem com folga antes do primeiro reboot possivel do LinkSupervisor (mais
// de 7 min) e cobrem o que o rollback existe para pegar: crash no setup(), watchdog na
// subida do modem, panico nos primeiros ciclos do loop(). Esticar mais nao pega defeito
// novo — firmware que sobrevive dois minutos com AP no ar e HTTP respondendo nao esta
// quebrado do jeito que o bootloader sabe consertar.
const unsigned long kVerificationWindowMs = 120000;

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

bool verificationWindowElapsed(unsigned long uptimeMs) {
  return uptimeMs >= kVerificationWindowMs;
}
