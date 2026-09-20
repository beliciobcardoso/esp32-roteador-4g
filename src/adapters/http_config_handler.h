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

  // Ultima leitura da bateria, em volts. Provider e nao leitura direta pelo mesmo motivo
  // dos outros: o adaptador nao conhece o ADC. E e a ULTIMA leitura, ja tirada pelo loop,
  // nao uma nova — o readVoltage() bloqueia ~100 ms para tirar a media do ruido, e 100 ms
  // dentro do handler atrasariam a resposta e o handleClient() inteiro a cada polling de
  // status. Devolve zero enquanto nenhuma leitura aconteceu.
  using BatteryVoltageProvider = float (*)();
  void onBatteryVoltageRequested(BatteryVoltageProvider provider) { batteryVoltage_ = provider; }
  void onRestartRequested(RestartRequested callback) { restartRequested_ = callback; }
  void onFirmwareConfirmed(FirmwareConfirmed callback) { firmwareConfirmed_ = callback; }

 private:
  // GET / — devolve a pagina embutida byte a byte, sem montar String nenhuma.
  void handleGetPage();
  void handleGetStatus();
  void handleGetConfig();
  void handlePostConfig();
  void handleNotFound();

  // ETag da pagina: hash do conteudo embutido, calculado uma vez no primeiro pedido.
  // Hash do conteudo e nao a versao do firmware porque os dois nao andam juntos durante o
  // desenvolvimento — editar o HTML sem novo commit deixaria a versao igual e o navegador
  // serviria a pagina velha do cache, com o sintoma de "minha alteracao nao apareceu".
  const String& pageETag();

  // Os dois lados do POST /update. O de upload roda DENTRO do parser da requisicao, um
  // bloco por vez, antes de o handler de POST existir; o de fim roda depois, uma vez.
  void handleUpdateUpload();
  void handleUpdateDone();

  // POST porque muda estado da placa: um GET aqui seria disparado por prefetch do
  // navegador ou por qualquer <img> apontando para a rota, e confirmaria uma imagem que o
  // operador nunca olhou.
  void handleConfirmFirmware();

  String firmwareStateText() const;
  String uplinkStatusText() const;
  String clockTextOrExcuse() const;

  // Lista de fusos como array JSON. Montada aqui, com escape campo a campo, em vez de num
  // JsonArray no dominio: e a unica lista que a API devolve, e abstracao com uma ocorrencia
  // so custa mais do que a concatenacao que ela esconderia.
  String timezoneOptionsJson() const;
  bool authenticate(const RouterSettings& current);

  // Responde JSON e proibe cache. Sem o no-store o navegador reaproveitaria o status
  // anterior no polling seguinte e a tela congelaria mostrando dado velho.
  void sendJson(int code, const String& json);

  // Erro como JSON, no mesmo formato que a pagina espera em toda rota de API: um objeto com
  // "erro". Texto plano aqui obrigaria o JS a adivinhar o tipo do corpo pelo status.
  void sendJsonError(int code, const String& message);

  LoadSettingsUseCase& loadUseCase_;
  SaveSettingsUseCase& saveUseCase_;
  FirmwareWriter& firmwareWriter_;
  WebServer server_;
  UplinkSettingsChanged uplinkChanged_ = nullptr;
  UplinkStatusProvider uplinkStatus_ = nullptr;
  LocalSettingsChanged localChanged_ = nullptr;
  ClockTextProvider clockText_ = nullptr;
  BatteryVoltageProvider batteryVoltage_ = nullptr;
  String pageETag_;
  RestartRequested restartRequested_ = nullptr;
  FirmwareConfirmed firmwareConfirmed_ = nullptr;

  // Estado de um upload de firmware, valido so entre o inicio e o fim de um POST /update.
  // Mora aqui e nao em variaveis locais porque o upload chega picado em varias chamadas do
  // handleUpdateUpload(), e a resposta so e montada depois, no handleUpdateDone().
  bool updateAttempted_ = false;   // alguma parte de arquivo chegou
  bool updateAuthorized_ = false;  // a credencial passou no primeiro bloco
  String updateError_;             // primeira falha; vazio enquanto tudo caminha
};
