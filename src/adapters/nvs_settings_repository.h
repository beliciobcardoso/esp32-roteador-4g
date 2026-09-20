#pragma once

#include "settings_repository.h"

// Implementacao concreta usando NVS (via Preferences, disponivel no framework Arduino).
class NvsSettingsRepository : public SettingsRepository {
 public:
  bool load(RouterSettings& out) override;
  bool save(const RouterSettings& settings) override;
};
