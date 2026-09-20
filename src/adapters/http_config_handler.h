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

  // Avisada quando a gravacao muda algo que vale a quente e nao passa pelo uplink: hoje o
  // fuso do relogio e a calibracao do divisor da bateria. Separada do callback de uplink
  // porque derrubar o PPP por causa de um fuso seria estrago sem motivo.
  using LocalSettingsChanged = void (*)(const RouterSettings& updated);

  HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase);

  void begin();
  void handleClient();

  // Registrado depois da construcao porque o destino vive noutro global e a ordem de
  // inicializacao entre unidades de traducao nao e garantida.
  void onUplinkSettingsChanged(UplinkSettingsChanged callback) { uplinkChanged_ = callback; }
  void onUplinkStatusRequested(UplinkStatusProvider provider) { uplinkStatus_ = provider; }
  void onLocalSettingsChanged(LocalSettingsChanged callback) { localChanged_ = callback; }

  // Consultado a cada GET / para a linha do relogio. Devolve string vazia enquanto o NTP
  // nao sincronizou — a pagina diz isso em vez de mostrar um horario inventado.
  using ClockTextProvider = String (*)();
  void onClockTextRequested(ClockTextProvider provider) { clockText_ = provider; }

 private:
  void handleGetRoot();
  void handlePostRoot();
  void handleNotFound();
  String uplinkStatusText() const;
  String clockTextOrExcuse() const;
  String timezoneOptionsHtml(const RouterSettings& current) const;
  String adminNoticeHtml(const RouterSettings& current) const;
  bool authenticate(const RouterSettings& current);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  WebServer server_;
  UplinkSettingsChanged uplinkChanged_ = nullptr;
  UplinkStatusProvider uplinkStatus_ = nullptr;
  LocalSettingsChanged localChanged_ = nullptr;
  ClockTextProvider clockText_ = nullptr;
};
