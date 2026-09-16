#include "save_settings.h"

SaveSettingsResult SaveSettingsUseCase::execute(const RouterSettings& settings) {
  SettingsValidationError error = validate(settings);
  if (error != SettingsValidationError::None) {
    return {false, error};
  }

  bool saved = repository_.save(settings);
  return {saved, SettingsValidationError::None};
}
