#pragma once

#include "../domain/router_settings.h"

// Interface abstrata — usecases dependem disso, nunca da implementacao concreta (NVS, arquivo, etc).
class SettingsRepository {
 public:
  virtual ~SettingsRepository() = default;

  // Retorna true e preenche `out` se havia config salva; false se a NVS estava vazia.
  virtual bool load(RouterSettings& out) = 0;

  // Retorna true se a gravacao foi bem-sucedida.
  virtual bool save(const RouterSettings& settings) = 0;
};
