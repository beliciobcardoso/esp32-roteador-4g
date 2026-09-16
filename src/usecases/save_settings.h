#pragma once

#include "../adapters/settings_repository.h"

struct SaveSettingsResult {
  bool success;
  SettingsValidationError error;
};

// CASO DE USO — valida via domain e persiste via repository. Nao grava nada se invalido.
class SaveSettingsUseCase {
 public:
  explicit SaveSettingsUseCase(SettingsRepository& repository) : repository_(repository) {}

  SaveSettingsResult execute(const RouterSettings& settings);

 private:
  SettingsRepository& repository_;
};
