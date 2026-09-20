#pragma once

#include <WebServer.h>

#include "../domain/uplink_status.h"
#include "../usecases/load_settings.h"
#include "../usecases/save_settings.h"
#include "firmware_writer.h"

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

  // Avisada quando o firmware novo ja esta gravado e ativado. Nao reinicia aqui dentro:
  // a resposta ainda esta na fila do socket, e um esp_restart() no meio do handler cortaria
  // ela — o navegador mostraria erro de conexao depois de uma atualizacao bem-sucedida.
  using RestartRequested = void (*)();

  // Avisada quando o operador clicou em confirmar. O adaptador nao chama o
  // esp_ota_mark_app_valid_cancel_rollback() direto: quem decide e o main, que junta o
  // clique ao estado da imagem pela regra do dominio.
  using FirmwareConfirmed = void (*)();

  HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase,
                    FirmwareWriter& firmwareWriter);

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
  void onRestartRequested(RestartRequested callback) { restartRequested_ = callback; }
  void onFirmwareConfirmed(FirmwareConfirmed callback) { firmwareConfirmed_ = callback; }

 private:
  void handleGetRoot();
  void handlePostRoot();
  void handleNotFound();

  // Os dois lados do POST /update. O de upload roda DENTRO do parser da requisicao, um
  // bloco por vez, antes de o handler de POST existir; o de fim roda depois, uma vez.
  void handleUpdateUpload();
  void handleUpdateDone();

  // POST porque muda estado da placa: um GET aqui seria disparado por prefetch do
  // navegador ou por qualquer <img> apontando para a rota, e confirmaria uma imagem que o
  // operador nunca olhou.
  void handleConfirmFirmware();

  String firmwareStateText() const;

  // Botao de confirmar, ou nada. So aparece com a imagem em janela de verificacao: nos
  // outros estados nao ha o que confirmar, e um botao que nao faz nada convida a clicar.
  String firmwareConfirmHtml() const;
  String uplinkStatusText() const;
  String clockTextOrExcuse() const;
  String timezoneOptionsHtml(const RouterSettings& current) const;
  String adminNoticeHtml(const RouterSettings& current) const;
  bool authenticate(const RouterSettings& current);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  FirmwareWriter& firmwareWriter_;
  WebServer server_;
  UplinkSettingsChanged uplinkChanged_ = nullptr;
  UplinkStatusProvider uplinkStatus_ = nullptr;
  LocalSettingsChanged localChanged_ = nullptr;
  ClockTextProvider clockText_ = nullptr;
  RestartRequested restartRequested_ = nullptr;
  FirmwareConfirmed firmwareConfirmed_ = nullptr;

  // Estado de um upload de firmware, valido so entre o inicio e o fim de um POST /update.
  // Mora aqui e nao em variaveis locais porque o upload chega picado em varias chamadas do
  // handleUpdateUpload(), e a resposta so e montada depois, no handleUpdateDone().
  bool updateAttempted_ = false;   // alguma parte de arquivo chegou
  bool updateAuthorized_ = false;  // a credencial passou no primeiro bloco
  String updateError_;             // primeira falha; vazio enquanto tudo caminha
};
