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
void test_the_deadline_gives_the_operator_more_than_the_supervisor_used_to_allow() {
  // O prazo agora e maior que os 7 min em que o supervisor reiniciava a placa por falta de
  // sinal. Isso so e seguro porque o supervisor passa a segurar o reboot enquanto a
  // confirmacao estiver pendente — ver supervisorMayRebootForUplink().
  TEST_ASSERT_TRUE(kConfirmationDeadlineMs >= 10UL * 60UL * 1000UL);
}

// --- decisao de confirmacao ---

int decision(FirmwareConfirmationOutcome outcome) {
  return static_cast<int>(outcome);
}

FirmwareHealth healthy() {
  FirmwareHealth health;
  health.ap_up = true;
  health.http_up = true;
  return health;
}

void test_an_image_that_is_not_pending_has_nothing_to_decide() {
  for (FirmwareImageState state :
       {FirmwareImageState::Valid, FirmwareImageState::Unmarked, FirmwareImageState::Unknown}) {
    TEST_ASSERT_EQUAL_INT(
        decision(FirmwareConfirmationOutcome::Nothing),
        decision(decideFirmwareConfirmation(state, healthy(), false, 0)));
  }
}

void test_a_pending_image_waits_for_the_operator() {
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::KeepWaiting),
      decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify, healthy(), false, 0)));
}

void test_the_operator_click_confirms() {
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::Confirm),
      decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify, healthy(), true, 0)));
}

void test_the_deadline_reverts_an_unconfirmed_image() {
  TEST_ASSERT_EQUAL_INT(decision(FirmwareConfirmationOutcome::Revert),
                        decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify,
                                                            healthy(), false,
                                                            kConfirmationDeadlineMs)));
}

void test_one_millisecond_before_the_deadline_still_waits() {
  TEST_ASSERT_EQUAL_INT(decision(FirmwareConfirmationOutcome::KeepWaiting),
                        decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify,
                                                            healthy(), false,
                                                            kConfirmationDeadlineMs - 1)));
}

void test_an_ap_that_did_not_come_up_reverts_without_waiting() {
  // O caso que o prazo sozinho nao pega: firmware roda, nao trava, mas ninguem consegue
  // chegar na pagina para clicar. Esperar os 10 min nao traria informacao nenhuma.
  FirmwareHealth health = healthy();
  health.ap_up = false;
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::Revert),
      decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify, health, false, 0)));
}

void test_an_http_server_that_is_down_reverts_without_waiting() {
  FirmwareHealth health = healthy();
  health.http_up = false;
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::Revert),
      decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify, health, false, 0)));
}

void test_a_click_wins_over_a_failed_self_check() {
  // Se o clique chegou, o AP subiu e o servidor respondeu — foi por eles que o POST veio.
  // Uma leitura de saude dizendo o contrario esta errada, e a prova empirica ganha.
  FirmwareHealth health;
  health.ap_up = false;
  health.http_up = false;
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::Confirm),
      decision(decideFirmwareConfirmation(FirmwareImageState::PendingVerify, health, true, 0)));
}

void test_a_broken_image_that_is_not_pending_is_left_alone() {
  // Placa gravada por serial com o AP mal configurado nao e assunto do rollback: nao ha
  // imagem anterior para voltar, e reverter a toa derrubaria a unica que existe.
  FirmwareHealth health;
  health.ap_up = false;
  health.http_up = false;
  TEST_ASSERT_EQUAL_INT(
      decision(FirmwareConfirmationOutcome::Nothing),
      decision(decideFirmwareConfirmation(FirmwareImageState::Unmarked, health, false, 0)));
}

// --- o supervisor nao pode competir com a janela ---

void test_the_supervisor_holds_its_reboot_while_confirmation_is_pending() {
  TEST_ASSERT_FALSE(supervisorMayRebootForUplink(FirmwareImageState::PendingVerify));
}

void test_the_supervisor_reboots_normally_once_the_image_is_settled() {
  TEST_ASSERT_TRUE(supervisorMayRebootForUplink(FirmwareImageState::Valid));
  TEST_ASSERT_TRUE(supervisorMayRebootForUplink(FirmwareImageState::Unmarked));
  TEST_ASSERT_TRUE(supervisorMayRebootForUplink(FirmwareImageState::Unknown));
}

// --- linha do serial com o desfecho do upload (debito 23) ---

bool lineMentions(const String& text, const char* fragment) {
  return text.find(fragment) != String::npos;
}

void test_every_error_has_its_own_reason_token() {
  // Token repetido faria dois defeitos diferentes parecerem o mesmo no grep.
  const FirmwareUpdateError errors[] = {
      FirmwareUpdateError::None, FirmwareUpdateError::EmptyImage,
      FirmwareUpdateError::TooShortToBeAnImage, FirmwareUpdateError::NotAnEspImage,
      FirmwareUpdateError::TooLargeForSlot};
  const size_t count = sizeof(errors) / sizeof(errors[0]);
  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      TEST_ASSERT_TRUE(String(updateReasonToken(errors[i])) != updateReasonToken(errors[j]));
    }
  }
}

void test_reason_tokens_are_plain_ascii_without_spaces() {
  // Sem acento e sem espaco: e o que deixa o token filtravel por grep em qualquer terminal.
  const FirmwareUpdateError errors[] = {
      FirmwareUpdateError::EmptyImage, FirmwareUpdateError::TooShortToBeAnImage,
      FirmwareUpdateError::NotAnEspImage, FirmwareUpdateError::TooLargeForSlot};
  for (FirmwareUpdateError error : errors) {
    for (const char* c = updateReasonToken(error); *c != '\0'; ++c) {
      TEST_ASSERT_TRUE_MESSAGE(*c > ' ' && static_cast<unsigned char>(*c) < 0x80,
                               updateReasonToken(error));
    }
  }
}

void test_a_success_line_says_written_and_how_much() {
  String text = describeUpdateOutcome(nullptr, 987654);
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(text, "gravado"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(text, "987654 B"), text.c_str());
  TEST_ASSERT_FALSE_MESSAGE(lineMentions(text, "recusado"), text.c_str());
}

void test_a_refusal_line_names_the_reason_and_the_bytes() {
  String text = describeUpdateOutcome(updateReasonToken(FirmwareUpdateError::NotAnEspImage), 1024);
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(text, "recusado (nao_e_imagem_esp32)"), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(text, "1024 B"), text.c_str());
}

void test_an_empty_form_is_told_apart_from_a_refused_file() {
  // Os dois respondem 400 na pagina. No serial, o motivo e os zero bytes contam a diferenca.
  String empty = describeUpdateOutcome(updateReasonToken(FirmwareUpdateError::EmptyImage), 0);
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(empty, "sem_arquivo"), empty.c_str());
  TEST_ASSERT_TRUE_MESSAGE(lineMentions(empty, "| 0 B"), empty.c_str());
}

void test_every_outcome_line_starts_with_the_ota_tag() {
  // Um prefixo so para os dois desfechos: grep "OTA:" pega toda tentativa, deu certo ou nao.
  TEST_ASSERT_EQUAL(0, describeUpdateOutcome(nullptr, 1).find("OTA: "));
  TEST_ASSERT_EQUAL(0, describeUpdateOutcome("sem_credencial", 1).find("OTA: "));
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
  RUN_TEST(test_the_deadline_gives_the_operator_more_than_the_supervisor_used_to_allow);
  RUN_TEST(test_an_image_that_is_not_pending_has_nothing_to_decide);
  RUN_TEST(test_a_pending_image_waits_for_the_operator);
  RUN_TEST(test_the_operator_click_confirms);
  RUN_TEST(test_the_deadline_reverts_an_unconfirmed_image);
  RUN_TEST(test_one_millisecond_before_the_deadline_still_waits);
  RUN_TEST(test_an_ap_that_did_not_come_up_reverts_without_waiting);
  RUN_TEST(test_an_http_server_that_is_down_reverts_without_waiting);
  RUN_TEST(test_a_click_wins_over_a_failed_self_check);
  RUN_TEST(test_a_broken_image_that_is_not_pending_is_left_alone);
  RUN_TEST(test_the_supervisor_holds_its_reboot_while_confirmation_is_pending);
  RUN_TEST(test_the_supervisor_reboots_normally_once_the_image_is_settled);
  RUN_TEST(test_every_error_has_its_own_reason_token);
  RUN_TEST(test_reason_tokens_are_plain_ascii_without_spaces);
  RUN_TEST(test_a_success_line_says_written_and_how_much);
  RUN_TEST(test_a_refusal_line_names_the_reason_and_the_bytes);
  RUN_TEST(test_an_empty_form_is_told_apart_from_a_refused_file);
  RUN_TEST(test_every_outcome_line_starts_with_the_ota_tag);
  return UNITY_END();
}
