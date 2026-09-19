#pragma once

#include <WebServer.h>

#include "../domain/uplink_status.h"
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

  // Consultada a cada GET / para montar o bloco de status. Ponteiro de funcao pelo mesmo
  // motivo do callback acima: o adaptador nao conhece o supervisor nem o modem.
  using UplinkStatusProvider = UplinkStatus (*)();

  HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase);

  void begin();
  void handleClient();

  // Registrado depois da construcao porque o destino vive noutro global e a ordem de
  // inicializacao entre unidades de traducao nao e garantida.
  void onUplinkSettingsChanged(UplinkSettingsChanged callback) { uplinkChanged_ = callback; }
  void onUplinkStatusRequested(UplinkStatusProvider provider) { uplinkStatus_ = provider; }

 private:
  void handleGetRoot();
  void handlePostRoot();
  void handleNotFound();
  String uplinkStatusText() const;
  String adminNoticeHtml(const RouterSettings& current) const;
  bool authenticate(const RouterSettings& current);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  WebServer server_;
  UplinkSettingsChanged uplinkChanged_ = nullptr;
  UplinkStatusProvider uplinkStatus_ = nullptr;
};
