#pragma once

#include <cstddef>
#include <cstdint>

#include "../adapters/settings_repository.h"

struct ProvisionResult {
  RouterSettings settings;
  // True quando os segredos foram sorteados agora — ou seja, primeiro boot com NVS vazia.
  // E o unico momento em que as credenciais podem ser mostradas.
  bool provisioned = false;
  // False quando o sorteio nao chegou na NVS. O AP sobe com a senha desta sessao, mas o
  // proximo boot sorteia outra: quem anotou a senha perde o acesso sem aviso.
  bool persisted = false;
};

// CASO DE USO — garante que a unidade tenha segredos proprios antes de qualquer radio
// subir. Le a config salva; se nao houver, sorteia senha de AP e de admin, marca a troca
// obrigatoria da senha de admin e persiste.
class ProvisionSettingsUseCase {
 public:
  // Ponteiro de funcao em vez de interface: e o mesmo seam que o resto do projeto usa
  // (callbacks do http_config_handler), e uma fonte de entropia nao tem estado nem segunda
  // implementacao a justificar hierarquia.
  using EntropySource = void (*)(uint8_t* buffer, size_t length);

  ProvisionSettingsUseCase(SettingsRepository& repository, EntropySource entropy)
      : repository_(repository), entropy_(entropy) {}

  ProvisionResult execute();

 private:
  SettingsRepository& repository_;
  EntropySource entropy_;
};
