#pragma once

#include "../adapters/settings_repository.h"

// CASO DE USO — le config salva ou aplica defaults de fabrica se a NVS estiver vazia.
class LoadSettingsUseCase {
 public:
  explicit LoadSettingsUseCase(SettingsRepository& repository) : repository_(repository) {}

  RouterSettings execute();

 private:
  SettingsRepository& repository_;
};
