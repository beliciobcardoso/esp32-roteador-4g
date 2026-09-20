#include <unity.h>

#include "domain/firmware_update.h"

namespace {

int code(FirmwareUpdateError error) {
  return static_cast<int>(error);
}

// Cabecalho plausivel: magic no lugar e bytes suficientes. O conteudo dos outros campos
// nao importa aqui — quem valida a imagem de verdade e o esp_image_verify() no fim da
// gravacao, e este exame so existe para barrar o arquivo errado antes de apagar o slot.
unsigned char* validHead() {
  static unsigned char head[64];
  for (size_t i = 0; i < sizeof(head); ++i) head[i] = 0x5A;
  head[0] = kEspImageMagic;
  return head;
}

void test_a_plausible_head_is_accepted() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::None), code(inspectImageHead(validHead(), 64)));
}

void test_a_head_without_the_magic_byte_is_rejected() {
  unsigned char* head = validHead();
  head[0] = 0x7F;  // ELF: e o erro classico, subir o .elf em vez do .bin

  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::NotAnEspImage), code(inspectImageHead(head, 64)));
}

void test_an_empty_upload_is_rejected() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::EmptyImage), code(inspectImageHead(validHead(), 0)));
}

// Arquivo menor que o proprio cabecalho nao tem como ser imagem, mesmo comecando com o
// magic — e o caso de um .bin truncado no meio do primeiro bloco.
void test_a_head_shorter_than_the_header_is_rejected() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::TooShortToBeAnImage),
                        code(inspectImageHead(validHead(), kEspImageHeaderSize - 1)));
}

void test_a_head_exactly_the_size_of_the_header_is_accepted() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::None),
                        code(inspectImageHead(validHead(), kEspImageHeaderSize)));
}

// Ponteiro nulo nao e caso de formulario, e sim de chamada errada. Recusar em vez de
// desreferenciar: o adaptador roda dentro do parser do WebServer, onde um crash derruba o
// AP junto e deixa a placa sem pagina para consertar nada.
void test_a_null_head_is_rejected() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::EmptyImage), code(inspectImageHead(nullptr, 64)));
}

void test_a_size_that_fits_the_slot_is_accepted() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::None), code(inspectImageSize(929000, 0x1D0000)));
}

void test_a_size_exactly_the_size_of_the_slot_is_accepted() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::None), code(inspectImageSize(0x1D0000, 0x1D0000)));
}

void test_a_size_above_the_slot_is_rejected() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::TooLargeForSlot),
                        code(inspectImageSize(0x1D0000 + 1, 0x1D0000)));
}

void test_a_zero_size_is_rejected() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::EmptyImage), code(inspectImageSize(0, 0x1D0000)));
}

// Slot desconhecido (falha ao consultar a particao) nao pode virar "cabe": seria aceitar
// qualquer tamanho justamente quando nao se sabe o limite.
void test_an_unknown_slot_size_rejects_everything() {
  TEST_ASSERT_EQUAL_INT(code(FirmwareUpdateError::TooLargeForSlot), code(inspectImageSize(1024, 0)));
}

void test_every_error_code_has_its_own_message() {
  const FirmwareUpdateError all[] = {
      FirmwareUpdateError::None,          FirmwareUpdateError::EmptyImage,
      FirmwareUpdateError::TooShortToBeAnImage, FirmwareUpdateError::NotAnEspImage,
      FirmwareUpdateError::TooLargeForSlot,
  };
  const size_t count = sizeof(all) / sizeof(all[0]);

  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_NOT_NULL(to_string(all[i]));
    TEST_ASSERT_TRUE(String(to_string(all[i])).length() > 0);
    for (size_t j = i + 1; j < count; ++j) {
      TEST_ASSERT_TRUE(String(to_string(all[i])) != String(to_string(all[j])));
    }
  }
}

// --- Estado da imagem em execucao ---

// A frase responde uma pergunta so: posso reiniciar esta placa agora? Em PendingVerify a
// resposta e nao, e precisa estar no texto — reiniciar nessa janela reverte um firmware bom.
void test_every_state_has_its_own_message() {
  const FirmwareImageState all[] = {
      FirmwareImageState::Valid,
      FirmwareImageState::PendingVerify,
      FirmwareImageState::Unmarked,
      FirmwareImageState::Unknown,
  };
  const size_t count = sizeof(all) / sizeof(all[0]);

  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_TRUE(describeFirmwareImageState(all[i]).length() > 0);
    for (size_t j = i + 1; j < count; ++j) {
      TEST_ASSERT_TRUE(describeFirmwareImageState(all[i]) != describeFirmwareImageState(all[j]));
    }
  }
}

// Confirmar saude so faz sentido em PendingVerify. Nos outros estados a chamada ou nao faz
// nada (Valid) ou devolve ESP_FAIL (Unmarked, otadata em branco de placa gravada por
// serial) — erro no log a cada boot sem nada a confirmar.
void test_only_a_pending_image_needs_confirmation() {
  TEST_ASSERT_TRUE(needsHealthConfirmation(FirmwareImageState::PendingVerify));
  TEST_ASSERT_FALSE(needsHealthConfirmation(FirmwareImageState::Valid));
  TEST_ASSERT_FALSE(needsHealthConfirmation(FirmwareImageState::Unmarked));
  TEST_ASSERT_FALSE(needsHealthConfirmation(FirmwareImageState::Unknown));
}

// Janela de verificacao: so depois dela a imagem e confirmada. O numero nao e arbitrario —
// tem que caber com folga antes do primeiro reboot que o LinkSupervisor consegue disparar
// (10 falhas com backoff de 5/10/20/40/60 s dao mais de 7 min, sem contar o tempo de cada
// tentativa). Reiniciar dentro da janela reverte, e o supervisor nao pode causar isso.
void test_the_window_is_not_over_before_it_elapses() {
  TEST_ASSERT_FALSE(verificationWindowElapsed(0));
  TEST_ASSERT_FALSE(verificationWindowElapsed(kVerificationWindowMs - 1));
}

void test_the_window_is_over_once_it_elapses() {
  TEST_ASSERT_TRUE(verificationWindowElapsed(kVerificationWindowMs));
  TEST_ASSERT_TRUE(verificationWindowElapsed(kVerificationWindowMs + 1));
}

void test_the_window_fits_before_the_supervisor_can_reboot() {
  // 7 min e o piso do tempo ate o supervisor reiniciar a placa. A janela tem que terminar
  // bem antes, senao falta de sinal — que nao e defeito do firmware — dispara o rollback.
  TEST_ASSERT_TRUE(kVerificationWindowMs < 7UL * 60UL * 1000UL);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_plausible_head_is_accepted);
  RUN_TEST(test_a_head_without_the_magic_byte_is_rejected);
  RUN_TEST(test_an_empty_upload_is_rejected);
  RUN_TEST(test_a_head_shorter_than_the_header_is_rejected);
  RUN_TEST(test_a_head_exactly_the_size_of_the_header_is_accepted);
  RUN_TEST(test_a_null_head_is_rejected);
  RUN_TEST(test_a_size_that_fits_the_slot_is_accepted);
  RUN_TEST(test_a_size_exactly_the_size_of_the_slot_is_accepted);
  RUN_TEST(test_a_size_above_the_slot_is_rejected);
  RUN_TEST(test_a_zero_size_is_rejected);
  RUN_TEST(test_an_unknown_slot_size_rejects_everything);
  RUN_TEST(test_every_error_code_has_its_own_message);
  RUN_TEST(test_every_state_has_its_own_message);
  RUN_TEST(test_only_a_pending_image_needs_confirmation);
  RUN_TEST(test_the_window_is_not_over_before_it_elapses);
  RUN_TEST(test_the_window_is_over_once_it_elapses);
  RUN_TEST(test_the_window_fits_before_the_supervisor_can_reboot);
  return UNITY_END();
}
