#pragma once

#include <WebServer.h>

#include "../usecases/load_settings.h"
#include "../usecases/save_settings.h"

// ADAPTADOR — rotas HTTP, parsing de formulario, Basic Auth.
// Depende so de usecases (interfaces), nunca de nvs_settings_repository diretamente.
class HttpConfigHandler {
 public:
  // Avisada quando a configuracao salva muda algo que o uplink usa (APN e credenciais).
  // Ponteiro de funcao em vez de std::function: nao ha captura a fazer e evita alocacao
  // no heap. Quem registra decide o que fazer — o adaptador nao conhece o modem.
  using UplinkSettingsChanged = void (*)(const RouterSettings& updated);

  HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase);

  void begin();
  void handleClient();

  // Registrado depois da construcao porque o destino vive noutro global e a ordem de
  // inicializacao entre unidades de traducao nao e garantida.
  void onUplinkSettingsChanged(UplinkSettingsChanged callback) { uplinkChanged_ = callback; }

 private:
  void handleGetRoot();
  void handlePostRoot();
  void handleNotFound();
  bool authenticate(const RouterSettings& current);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  WebServer server_;
  UplinkSettingsChanged uplinkChanged_ = nullptr;
};
