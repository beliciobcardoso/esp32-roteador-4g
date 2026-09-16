#pragma once

#include <WebServer.h>

#include "../usecases/load_settings.h"
#include "../usecases/save_settings.h"

// ADAPTADOR — rotas HTTP, parsing de formulario, Basic Auth.
// Depende so de usecases (interfaces), nunca de nvs_settings_repository diretamente.
class HttpConfigHandler {
 public:
  HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase);

  void begin();
  void handleClient();

 private:
  void handleGetRoot();
  void handlePostRoot();
  bool authenticate(const RouterSettings& current);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  WebServer server_;
};
