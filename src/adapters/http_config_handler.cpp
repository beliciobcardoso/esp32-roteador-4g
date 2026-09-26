#include "http_config_handler.h"

#include <lwip/sockets.h>

#include "../domain/battery.h"
#include "../domain/firmware_update.h"
#include "../domain/json.h"
#include "../domain/timezone.h"
#include "html_page.h"

namespace {
const int kServerPort = 80;

// Keepalive do socket que recebe o firmware. Sem isto, um cliente que SOME — Wi-Fi
// desligado, fora de alcance — nao fecha a conexao, e `WebServer::_uploadReadByte` espera
// para sempre em `while(!client.available() && client.connected()) delay(2);`, dentro do
// loop(). Medido em bancada em 24/09/2026: 137 s de serial mudo com o celular fora, e a
// placa seguiu parada depois que ele voltou. Com keepalive o lwIP declara a conexao morta,
// `connected()` vira falso e o `UPLOAD_FILE_ABORTED` roda como deveria.
//
// 5 s ocioso + 3 sondas de 2 s = morte em ~11 s, bem abaixo do limite de travamento de
// `domain/loop_health` (30 s): quando as duas correcoes valem, esta age primeiro e a placa
// se recupera sem reiniciar. O reinicio fica para quando esta aqui falhar.
//
// Pode falhar, e o motivo e uma linha do core: `WiFiClient::connected()` so derruba o
// estado em ENOTCONN, EPIPE, ECONNRESET, ECONNREFUSED e ECONNABORTED; qualquer outro errno
// cai no `default` e e lido como "ainda conectado". Se o lwIP entregar ETIMEDOUT ao matar a
// conexao por keepalive, este bloco nao resolve nada — e por isso ele entra junto com o
// watchdog, e nao no lugar dele.
const int kKeepAliveIdleSeconds = 5;
const int kKeepAliveIntervalSeconds = 2;
const int kKeepAliveProbes = 3;

// Recebe por valor porque `WebServer::client()` devolve uma copia, e nao uma referencia.
// Nao ha perda: o WiFiClient guarda o descritor num shared_ptr, entao a copia aponta para o
// mesmo socket, e e nele que o setsockopt escreve.
void enableKeepAlive(WiFiClient client) {
  const int on = 1;
  client.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  client.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &kKeepAliveIdleSeconds,
                         sizeof(kKeepAliveIdleSeconds));
  client.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &kKeepAliveIntervalSeconds,
                         sizeof(kKeepAliveIntervalSeconds));
  client.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &kKeepAliveProbes,
                         sizeof(kKeepAliveProbes));
}
const char* kAuthRealm = "roteador-4g";
const char* kJsonType = "application/json";

// Nome curto do estado, para o CSS da pagina escolher a cor do indicador. Separado da
// frase do describeUplinkStatus(): a frase e para a pessoa ler e muda de redacao, o nome
// e contrato com o JS e nao pode mudar sem quebrar a folha de estilo.
const char* uplinkStateName(UplinkState state) {
  switch (state) {
    case UplinkState::Online: return "online";
    case UplinkState::Backoff: return "backoff";
    case UplinkState::Connecting: return "connecting";
  }
  return "connecting";
}

// Mesmo desenho do uplink, e pelo mesmo motivo: a frase do describeFirmwareImageState() e
// para a pessoa ler e muda de redacao, o nome e contrato com o JS. Sem ele a pagina teria
// que adivinhar o estado por substring na frase, ou se contentar com o booleano de
// confirmacao — que nao distingue "gravado por serial" de "confirmado".
const char* firmwareImageStateName(FirmwareImageState state) {
  switch (state) {
    case FirmwareImageState::Valid: return "valid";
    case FirmwareImageState::PendingVerify: return "pending";
    case FirmwareImageState::Unmarked: return "unmarked";
    case FirmwareImageState::Unknown: return "unknown";
  }
  return "unknown";
}

// Numero inteiro do formulario, estrito. O toInt() le "443abc" como 443 e "" como 0 sem
// avisar, e a porta errada passaria calada. Aqui qualquer coisa que nao seja so digito
// devolve 0, que o validate() reprova com a mensagem certa. Onze digitos ja nao cabem em
// uint32_t, e sao recusados antes da conta para ela nao dar a volta e virar um numero
// valido.
uint32_t parseUnsignedField(const String& raw) {
  if (raw.length() == 0 || raw.length() > 10) return 0;
  uint64_t value = 0;
  for (size_t i = 0; i < raw.length(); ++i) {
    if (raw[i] < '0' || raw[i] > '9') return 0;
    value = value * 10 + static_cast<uint64_t>(raw[i] - '0');
  }
  return value > UINT32_MAX ? 0 : static_cast<uint32_t>(value);
}
}  // namespace

HttpConfigHandler::HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase,
                                     FirmwareWriter& firmwareWriter)
    : loadUseCase_(loadUseCase),
      saveUseCase_(saveUseCase),
      firmwareWriter_(firmwareWriter),
      server_(kServerPort) {}

void HttpConfigHandler::begin() {
  server_.on("/", HTTP_GET, [this]() { handleGetPage(); });
  server_.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  // POST continua chegando urlencoded, e nao JSON: o WebServer ja parseia esse formato em
  // server_.arg(), enquanto JSON exigiria um parser escrito a mao no firmware — aninhamento,
  // escapes e unicode — para oito campos planos que o navegador monta com URLSearchParams
  // em uma linha. A resposta e JSON; o pedido nao precisa ser.
  server_.on("/api/config", HTTP_POST, [this]() { handlePostConfig(); });
  // Duas funcoes para a mesma rota: a segunda roda durante a leitura do corpo, bloco a
  // bloco, e a primeira so depois que o corpo inteiro acabou.
  server_.on("/update", HTTP_POST, [this]() { handleUpdateDone(); },
             [this]() { handleUpdateUpload(); });
  server_.on("/firmware/confirmar", HTTP_POST, [this]() { handleConfirmFirmware(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();
  // Depois do begin(), nao antes: o begin() chama collectHeaders(0, 0) e apagaria a lista.
  // O WebServer so guarda cabecalho que foi pedido — sem isto o If-None-Match chega vazio,
  // o 304 nunca acontece e a pagina inteira desce a cada visita sem ninguem notar.
  const char* collected[] = {"If-None-Match"};
  server_.collectHeaders(collected, 1);
}

void HttpConfigHandler::handleClient() {
  server_.handleClient();
}

bool HttpConfigHandler::authenticate(const RouterSettings& current) {
  // Credencial vazia nunca autentica. Nao e paranoia: o fallback do LoadSettingsUseCase
  // devolve senha vazia quando a NVS fica ilegivel, e sem esta guarda o authenticate()
  // compararia contra "" e deixaria entrar quem mandasse "admin:" sem senha. No boot isso
  // nao chega a acontecer porque o AP nem sobe, mas uma falha de leitura com o AP ja no ar
  // chegaria — e ai o erro de armazenamento viraria porta aberta.
  if (current.admin_user.length() == 0 || current.admin_password.length() == 0) {
    server_.requestAuthentication(BASIC_AUTH, kAuthRealm);
    return false;
  }

  if (server_.authenticate(current.admin_user.c_str(), current.admin_password.c_str())) {
    return true;
  }
  server_.requestAuthentication(BASIC_AUTH, kAuthRealm);
  return false;
}

// Sonda de portal cativo do Android e /favicon.ico batem aqui o tempo todo. Sem este
// handler o caminho default do WebServer registra "request handler not found" como erro,
// e numa depuracao de campo essas linhas se misturam com erro de verdade (debito 14).
// Corpo fixo e curto: sem eco da URI e sem lista de rotas, nada que descreva o servidor.
// Sem Basic Auth de proposito — exigir credencial aqui faria o navegador abrir o popup de
// senha por causa de um favicon.
void HttpConfigHandler::handleNotFound() {
  server_.send(404, "text/plain", "não encontrado");
}

// Texto do bloco de status. Sem provider registrado a pagina diz isso em vez de omitir o
// bloco: um bloco ausente parece pagina antiga, e a frase aponta para o defeito de
// montagem em vez de deixar quem le achando que o 4G esta bem.
String HttpConfigHandler::uplinkStatusText() const {
  if (uplinkStatus_ == nullptr) {
    return "Estado do uplink indisponível: nenhuma fonte de status foi registrada.";
  }
  return describeUplinkStatus(uplinkStatus_());
}

// Linha do relogio. Sem provider registrado, ou com o NTP ainda sem sincronizar, a pagina
// diz isso em vez de omitir a linha ou mostrar a epoch de 1970 formatada — a regra da Fase
// 7 e que o firmware nunca finge um horario.
String HttpConfigHandler::clockTextOrExcuse() const {
  if (clockText_ == nullptr) {
    return "indisponível: nenhuma fonte de hora foi registrada.";
  }
  String text = clockText_();
  if (text.length() == 0) return "ainda não sincronizado (precisa do uplink 4G).";
  return text;
}

// A lista sai da tabela do dominio, nao de uma lista repetida aqui: duas listas para o
// mesmo conjunto divergem na primeira vez que uma for editada, e a que o validate() usa e a
// do dominio — a pagina e que ficaria oferecendo um fuso recusado na gravacao.
//
// Quem marca a opcao escolhida e o JS, comparando com o campo `timezone` da mesma resposta.
// Registro cujo fuso nao esta na tabela (POST forjado, downgrade de firmware) nao casa com
// nenhuma opcao e o <select> fica na primeira — gravar a pagina como esta conserta.
String HttpConfigHandler::timezoneOptionsJson() const {
  String json = "[";
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    const TimezoneOption& option = timezoneOptions()[i];
    if (i > 0) json += ",";
    JsonObject entry;
    entry.text("posix", option.posix).text("label", option.label);
    json += entry.finish();
  }
  json += "]";
  return json;
}

// Frase do estado da imagem em execucao. Sai do dominio para nao haver duas redacoes da
// mesma regra — e a frase do PendingVerify e um aviso, nao enfeite.
String HttpConfigHandler::firmwareStateText() const {
  return describeFirmwareImageState(firmwareWriter_.runningImageState());
}

void HttpConfigHandler::handleConfirmFirmware() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  // Estado conferido aqui tambem, e nao so na montagem do botao: a pagina pode estar aberta
  // desde antes de a imagem ser confirmada por outro caminho, e o POST chegaria para uma
  // janela que ja fechou.
  if (!needsHealthConfirmation(firmwareWriter_.runningImageState())) {
    sendJsonError(409, "Não há atualização pendente de confirmação.");
    return;
  }

  if (firmwareConfirmed_ != nullptr) firmwareConfirmed_();

  // 200 com JSON, e nao mais o 303 para a raiz: quem faz este POST agora e o fetch() da
  // propria pagina, que seguiria o redirect e baixaria a pagina inteira so para descartar.
  // O F5 que o 303 evitava tambem deixou de existir — nao ha navegacao para recarregar.
  JsonObject body;
  body.text("mensagem", "Atualização confirmada.");
  sendJson(200, body.finish());
}

// FNV-1a sobre o conteudo embutido. Nao e hash criptografico e nao precisa ser: o que se
// quer e um rotulo que muda quando a pagina muda, para o navegador perguntar "ainda vale?"
// e receber 304 em vez dos KB inteiros pelo link 4G. Calculado uma vez e guardado — o
// conteudo esta na flash e nao muda enquanto a placa roda.
const String& HttpConfigHandler::pageETag() {
  if (pageETag_.length() > 0) return pageETag_;

  uint32_t hash = 2166136261u;
  size_t length = 0;
  for (const char* cursor = kConfigPageTemplate; *cursor != '\0'; ++cursor, ++length) {
    hash ^= static_cast<uint8_t>(*cursor);
    hash *= 16777619u;
  }

  // Comprimento junto do hash: duas paginas diferentes com o mesmo hash de 32 bits sao
  // improvaveis, mas custa nada tornar a coincidencia ainda menos provavel.
  pageETag_ = "\"" + numberToString(hash) + "-" + numberToString(static_cast<uint32_t>(length)) + "\"";
  return pageETag_;
}

// A pagina sai da flash direto para o socket. Nenhum replace, nenhuma String de conteudo:
// o send_P escreve o cabecalho e depois streama o ponteiro, entao os KB da pagina nunca
// existem no heap. Era esse custo — uma copia por request mais uma realocacao por
// placeholder — que inviabilizava crescer a pagina.
void HttpConfigHandler::handleGetPage() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  const String& etag = pageETag();
  // no-cache e nao no-store: o navegador PODE guardar, mas tem que perguntar antes de usar.
  // E a pergunta que vira 304 e economiza o download inteiro na segunda visita.
  server_.sendHeader("Cache-Control", "no-cache");
  server_.sendHeader("ETag", etag);

  if (server_.header("If-None-Match") == etag) {
    server_.send(304, "text/plain", "");
    return;
  }

  server_.send_P(200, "text/html", kConfigPageTemplate);
}

// Tudo que muda sozinho. O polling da pagina bate aqui; o que nao muda sem gravacao mora no
// /api/config e nao e reenviado a cada 3 s.
void HttpConfigHandler::handleGetStatus() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  const UplinkStatus uplink = uplinkStatus_ != nullptr ? uplinkStatus_() : UplinkStatus();
  const FirmwareImageState imageState = firmwareWriter_.runningImageState();
  const float volts = batteryVoltage_ != nullptr ? batteryVoltage_() : 0.0f;
  const ModemIdentity modem = modemIdentity_ != nullptr ? modemIdentity_() : ModemIdentity();

  JsonObject body;
  body.text("uplink_state", uplinkStateName(uplink.state))
      .text("uplink_text", uplinkStatusText())
      .number("uplink_failures", uplink.consecutive_failures)
      .boolean("uplink_rebooted", uplink.rebooted_for_uplink)
      .boolean("uplink_exhausted", uplink.reboot_budget_exhausted)
      .text("clock_text", clockTextOrExcuse())
      .boolean("clock_synced", clockText_ != nullptr && clockText_().length() > 0)
      .number("battery_volts", volts, 2)
      // O percentual sai da regra do dominio e nao de uma conta no JS: a curva da bateria e
      // regra de negocio, e duplicada no navegador ela envelhece sozinha.
      .number("battery_percent", static_cast<uint32_t>(voltageToPercent(volts)))
      .number("ppp_drops_total", pppDrops_ != nullptr ? pppDrops_() : 0u)
      .text("modem_model", modem.model)
      .text("modem_revision", modem.revision)
      .boolean("modem_gnss", modemHasGnss(modem.model))
      .text("firmware_slot", firmwareWriter_.runningSlotLabel())
      .text("firmware_version", firmwareWriter_.runningVersionText())
      .text("firmware_image_state", firmwareImageStateName(imageState))
      .text("firmware_state", firmwareStateText())
      .boolean("firmware_needs_confirmation", needsHealthConfirmation(imageState))
      // O prazo vem do dominio, o mesmo numero que o loop() usa para decidir. A pagina e o
      // unico lugar onde o operador descobre que existe um prazo — sem clique, a placa volta
      // sozinha para a imagem anterior.
      .number("firmware_deadline_min",
              static_cast<uint32_t>(kConfirmationDeadlineMs / 60000))
      .boolean("admin_password_pending", current.admin_password_pending)
      // A mensagem sai do to_string() do dominio, a mesma que o POST recusado devolve: duas
      // fontes de texto para a mesma regra divergem na primeira vez que uma delas e editada.
      .text("admin_notice", current.admin_password_pending
                                ? String(to_string(SettingsValidationError::AdminPasswordMustChange))
                                : String(""));

  sendJson(200, body.finish());
}

// O que o formulario edita. Nenhuma senha sai daqui, nem mascarada: mascara e devolvida ao
// servidor no POST seguinte e viraria a senha nova, e o comprimento sozinho ja diz mais do
// que precisa ser dito. Os campos de senha nascem vazios na pagina, com o mesmo
// "deixe em branco para manter" que o POST ja implementa.
void HttpConfigHandler::handleGetConfig() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  JsonObject body;
  body.text("wifi_ssid", current.wifi_ssid)
      .text("apn", current.apn)
      .text("apn_user", current.apn_user)
      .text("admin_user", current.admin_user)
      .text("timezone", current.timezone)
      .number("battery_ratio", current.battery_divider_ratio, 2)
      .boolean("telemetry_enabled", current.telemetry_enabled)
      .text("mqtt_host", current.mqtt_host)
      .number("mqtt_port", current.mqtt_port)
      .text("mqtt_user", current.mqtt_user)
      // So se existe, nunca o valor (criterio 7 do PRD 14): e o que a pagina precisa para
      // dizer "definida" no placeholder, e nada alem disso.
      .boolean("mqtt_password_set", current.mqtt_password.length() > 0)
      .number("telemetry_interval_s", current.telemetry_interval_s)
      .raw("timezones", timezoneOptionsJson());

  sendJson(200, body.finish());
}

void HttpConfigHandler::sendJson(int code, const String& json) {
  // no-store e nao no-cache: status reaproveitado do cache no polling seguinte congelaria a
  // tela mostrando dado velho, e aqui nao ha o que economizar — a resposta tem centenas de
  // bytes, nao KB.
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, kJsonType, json);
}

void HttpConfigHandler::sendJsonError(int code, const String& message) {
  JsonObject body;
  body.text("erro", message);
  sendJson(code, body.finish());
}

void HttpConfigHandler::handlePostConfig() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  // Campos de senha em branco mantem o valor atual — evita forcar o usuario
  // a retypar a senha toda vez que so quer mudar o SSID, por exemplo.
  RouterSettings updated;
  updated.wifi_ssid = server_.arg("wifi_ssid");
  String newWifiPassword = server_.arg("wifi_password");
  updated.wifi_password = newWifiPassword.length() > 0 ? newWifiPassword : current.wifi_password;
  updated.apn = server_.arg("apn");
  updated.apn_user = server_.arg("apn_user");
  String newApnPassword = server_.arg("apn_password");
  updated.apn_password = newApnPassword.length() > 0 ? newApnPassword : current.apn_password;
  updated.admin_user = server_.arg("admin_user");
  String newAdminPassword = server_.arg("admin_password");
  updated.admin_password = newAdminPassword.length() > 0 ? newAdminPassword : current.admin_password;
  updated.timezone = server_.arg("timezone");

  // Virgula recusada antes do toFloat(), que para "2,19" devolve 2.00 sem sinal nenhum de
  // erro — valor dentro da faixa valida, e a bateria passaria a ser lida com 9% a menos
  // para sempre. O <input type="number"> ja normaliza para ponto, mas ele so existe no
  // navegador: um POST direto manda o que quiser.
  String rawRatio = server_.arg("battery_ratio");
  if (rawRatio.indexOf(',') >= 0) {
    sendJsonError(400, "use ponto e não vírgula no divisor da bateria");
    return;
  }
  updated.battery_divider_ratio = rawRatio.toFloat();

  // Checkbox desmarcado nao vai no formulario, entao ausencia e "desligada" — que e tambem o
  // lado seguro para um POST direto que nao conhece o campo. Os outros campos da telemetria
  // fazem o contrario: ausentes, mantem o valor atual, para um cliente que ainda nao os
  // conhece nao gravar porta zero e ser recusado por um campo que nem mandou.
  updated.telemetry_enabled = server_.arg("telemetry_enabled") == "on";
  updated.mqtt_host = server_.hasArg("mqtt_host") ? server_.arg("mqtt_host") : current.mqtt_host;
  updated.mqtt_port = server_.hasArg("mqtt_port") ? parseUnsignedField(server_.arg("mqtt_port"))
                                                  : current.mqtt_port;
  updated.mqtt_user = server_.hasArg("mqtt_user") ? server_.arg("mqtt_user") : current.mqtt_user;
  String newMqttPassword = server_.arg("mqtt_password");
  updated.mqtt_password = newMqttPassword.length() > 0 ? newMqttPassword : current.mqtt_password;
  updated.telemetry_interval_s = server_.hasArg("telemetry_interval_s")
                                     ? parseUnsignedField(server_.arg("telemetry_interval_s"))
                                     : current.telemetry_interval_s;

  // A pendencia cai sozinha quando a senha muda, e sobrevive a qualquer outra gravacao.
  // Checar aqui em vez de dentro do validate() porque a regra compara duas configuracoes e
  // o validate() ve uma sozinha.
  updated.admin_password_pending = adminPasswordChangeStillRequired(current, updated);
  if (updated.admin_password_pending) {
    sendJsonError(400, to_string(SettingsValidationError::AdminPasswordMustChange));
    return;
  }

  SaveSettingsResult result = saveUseCase_.execute(updated);
  if (!result.success) {
    sendJsonError(400, to_string(result.error));
    return;
  }

  // Nem todo campo entra em vigor do mesmo jeito, e a resposta precisa dizer qual e qual
  // — senao o usuario troca o SSID, ve "salvo" e fica esperando a rede nova aparecer.
  bool uplinkChanged = updated.apn != current.apn || updated.apn_user != current.apn_user ||
                       updated.apn_password != current.apn_password;
  bool apChanged = updated.wifi_ssid != current.wifi_ssid ||
                   updated.wifi_password != current.wifi_password;
  bool localChanged = updated.timezone != current.timezone ||
                      updated.battery_divider_ratio != current.battery_divider_ratio;
  bool telemetryChanged = updated.telemetry_enabled != current.telemetry_enabled ||
                          updated.mqtt_host != current.mqtt_host ||
                          updated.mqtt_port != current.mqtt_port ||
                          updated.mqtt_user != current.mqtt_user ||
                          updated.mqtt_password != current.mqtt_password ||
                          updated.telemetry_interval_s != current.telemetry_interval_s;

  String message = "Configuração salva.";
  if (uplinkChanged) {
    // Derrubar e resubir o PPP nao afeta a associacao dos clientes ao AP, entao isso
    // pode ser feito sem reboot. Ate um minuto porque inclui o power-on do modem, o
    // registro na rede e o IPCP.
    message += " APN alterado: reconectando o 4G agora, pode levar até 1 minuto.";
  }
  if (apChanged) {
    // Mudar SSID/senha derruba todo mundo que esta associado — inclusive quem acabou de
    // enviar este formulario. Fazer isso aqui cortaria a resposta antes dela chegar.
    message += " SSID e senha do Wi-Fi só valem após reiniciar a placa.";
  }

  if (localChanged) {
    // Fuso e divisor valem na proxima leitura, sem reconectar nem reiniciar nada. Dizer
    // isso evita que a pessoa fique esperando um efeito que ja aconteceu.
    message += " Fuso e calibração da bateria já valem.";
  }

  if (telemetryChanged) {
    // Honesto enquanto o cliente MQTT nao existe: a configuracao fica gravada e nada mais
    // acontece. Sem esta frase, quem liga a telemetria espera ver dado chegando no broker.
    message += " Telemetria gravada; o envio ao broker ainda não existe neste firmware.";
  }

  // Os flags acompanham a frase em vez de a pagina reler a frase: quem muda SSID precisa de
  // um aviso visivel de que a propria conexao vai cair no reboot, e procurar substring numa
  // mensagem em portugues para decidir isso quebra na primeira revisao do texto.
  JsonObject body;
  body.text("mensagem", message)
      .boolean("uplink_reconectando", uplinkChanged)
      .boolean("wifi_exige_reboot", apChanged)
      .boolean("local_ja_vale", localChanged);
  sendJson(200, body.finish());

  if (localChanged && localChanged_ != nullptr) {
    localChanged_(updated);
  }

  // Depois do send: a reconexao e assincrona, mas manter a resposta na frente evita que
  // qualquer mudanca futura nesse caminho segure o cliente esperando.
  if (uplinkChanged && uplinkChanged_ != nullptr) {
    uplinkChanged_(updated);
  }
}

// --- POST /update ---
//
// A credencial e conferida AQUI, no primeiro bloco do arquivo, e nao no handler que
// responde. O WebServer chama este upload de dentro do `_parseForm()`, enquanto le a
// requisicao (Parsing.cpp), bem antes de invocar o handler de POST. Conferir la na frente
// significaria gravar quase um megabyte no slot de OTA de quem nao se identificou, e so
// entao recusar — a senha de admin deixaria de valer "reconfigurar o roteador" e passaria
// a valer "trocar o firmware", mas sem a guarda nem senha seria preciso.
//
// O que nao da para evitar e o trafego: o parser le o corpo inteiro de qualquer jeito,
// tendo respondido 401 ou nao. O que se impede aqui e a escrita na flash.
void HttpConfigHandler::handleUpdateUpload() {
  HTTPUpload& upload = server_.upload();

  // Antes de qualquer decisao e de qualquer return: chegou pedaco, houve progresso. Vale
  // ate para o upload ja recusado, cujo corpo o parser continua lendo ate o fim — o
  // `grande-demais.bin` do TESTE_OTA gasta 2 MB assim, e isso e travessia legitima, nao
  // travamento.
  if (uploadProgress_ != nullptr) {
    uploadProgress_();
  }

  if (upload.status == UPLOAD_FILE_START) {
    // Armado no primeiro pedaco, que e a primeira vez que o socket desta requisicao esta ao
    // alcance daqui. O `server_.client()` e o mesmo objeto em que o parser do WebServer
    // espera os pedacos seguintes.
    enableKeepAlive(server_.client());

    updateAttempted_ = true;
    updateAuthorized_ = false;
    updateError_ = "";
    updateReason_ = nullptr;
    updateBytes_ = 0;

    RouterSettings current = loadUseCase_.execute();
    if (!authenticate(current)) return;  // o 401 ja saiu; nada sera gravado
    updateAuthorized_ = true;

    if (!firmwareWriter_.begin()) {
      log_->printf("OTA: abertura do slot falhou (%s)\n", firmwareWriter_.lastErrorText().c_str());
      updateError_ = "não foi possível abrir a partição de destino";
      updateReason_ = "abertura_do_slot_falhou";
    }
    return;
  }

  // Contado antes de qualquer retorno: o arquivo continua chegando mesmo recusado, e o total
  // e metade do veredito no serial. No WRITE o totalSize ainda nao inclui o bloco corrente.
  updateBytes_ = upload.totalSize +
                 (upload.status == UPLOAD_FILE_WRITE ? upload.currentSize : 0);

  if (upload.status == UPLOAD_FILE_ABORTED) {
    handleUpdateAborted();
    return;
  }

  if (!updateAuthorized_) return;
  // Ja falhou: o resto do arquivo continua chegando pela rede e e descartado aqui. A
  // primeira mensagem e a que descreve o defeito; as seguintes seriam consequencia dela.
  if (updateError_.length() > 0) return;

  if (upload.status == UPLOAD_FILE_WRITE) {
    // Primeiro bloco: o `totalSize` so e somado depois desta chamada (Parsing.cpp), entao
    // zero aqui significa que nada foi gravado ainda.
    if (upload.totalSize == 0) {
      FirmwareUpdateError verdict = inspectImageHead(upload.buf, upload.currentSize);
      if (verdict != FirmwareUpdateError::None) {
        firmwareWriter_.abort();
        updateError_ = to_string(verdict);
        updateReason_ = updateReasonToken(verdict);
        return;
      }
    }

    // Antes de cada escrita, e nao so no fim: o `Update` abre o slot com o tamanho da
    // particao e recusa o bloco que passa dela com "Not Enough Space". Conferido so no
    // UPLOAD_FILE_END, o arquivo grande demais nunca chegava la — morria aqui como falha de
    // escrita, e a pagina falava em "flash com defeito" para um arquivo errado. A linha do
    // serial do debito 23 foi o que mostrou isso, em 24/09/2026.
    FirmwareUpdateError sizeVerdict = inspectImageSize(
        upload.totalSize + upload.currentSize, firmwareWriter_.targetSlotSize());
    if (sizeVerdict == FirmwareUpdateError::TooLargeForSlot) {
      firmwareWriter_.abort();
      updateError_ = to_string(sizeVerdict);
      updateReason_ = updateReasonToken(sizeVerdict);
      return;
    }

    if (!firmwareWriter_.write(upload.buf, upload.currentSize)) {
      log_->printf("OTA: escrita falhou (%s)\n", firmwareWriter_.lastErrorText().c_str());
      firmwareWriter_.abort();
      updateError_ = "falha ao gravar no slot de destino: arquivo grande demais ou flash com defeito";
      updateReason_ = "escrita_falhou";
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    // O tamanho final. O "grande demais" ja foi pego bloco a bloco no WRITE; aqui fica o
    // caminho que pega o formulario enviado com um arquivo de zero bytes, que chega como uma
    // parte vazia e nunca passa pelo WRITE.
    FirmwareUpdateError verdict =
        inspectImageSize(upload.totalSize, firmwareWriter_.targetSlotSize());
    if (verdict != FirmwareUpdateError::None) {
      firmwareWriter_.abort();
      updateError_ = to_string(verdict);
      updateReason_ = updateReasonToken(verdict);
      return;
    }

    // Aqui dentro roda a verificacao da imagem inteira, com o SHA-256 que ela carrega:
    // arquivo truncado ou corrompido morre neste ponto, sem trocar o slot de boot.
    if (!firmwareWriter_.finish()) {
      log_->printf("OTA: ativacao do slot falhou (%s)\n", firmwareWriter_.lastErrorText().c_str());
      updateError_ = "a imagem enviada não passou na verificação";
      updateReason_ = "verificacao_falhou";
    }
  }
}

// Conexao caiu no meio do envio. O slot fica com lixo, mas nao bootavel: o `Update` so
// grava os primeiros bytes da imagem no fim, justamente para este caso.
//
// A linha do serial sai daqui porque o handleUpdateDone() nao vai rodar: o
// `_parseFormUploadAborted()` do core chama este handler com ABORTED e devolve falso, e o
// `_handleRequest()` nem e chamado. Sem isto o abort era o unico desfecho de POST /update
// sem linha no serial, o furo que o debito 27 deixou no contrato do debito 23. Vem antes das
// guardas de credencial e de falha anterior porque aqui nao ha mais o que descartar — e a
// ultima chamada desta requisicao, entao e a ultima chance de a linha sair.
//
// O motivo segue a regra do resto do arquivo, a primeira falha descreve o defeito: sem
// credencial continua `sem_credencial`, arquivo ja recusado continua com o motivo dele, e
// `interrompido` so aparece quando o upload estava indo bem. O que separa os dois casos de
// recusa do desfecho completo e o total de bytes, menor que o arquivo.
void HttpConfigHandler::handleUpdateAborted() {
  const char* reason = "sem_credencial";
  if (updateAuthorized_) {
    if (updateError_.length() == 0) {
      firmwareWriter_.abort();
      updateReason_ = "interrompido";
    }
    reason = updateReason_ != nullptr ? updateReason_ : "desconhecido";
  }
  log_->println(describeUpdateOutcome(reason, updateBytes_));

  // Nada responde a esta requisicao, entao ninguem mais limparia o estado. Sujo, ele seria
  // lido pelo proximo POST /update que chegasse sem parte de arquivo.
  resetUpdateState();
}

void HttpConfigHandler::resetUpdateState() {
  updateAttempted_ = false;
  updateAuthorized_ = false;
  updateError_ = "";
  updateReason_ = nullptr;
  updateBytes_ = 0;
}

// Roda uma vez, depois do corpo inteiro. A gravacao ja aconteceu: aqui so se responde e,
// se deu certo, se pede o reboot.
void HttpConfigHandler::handleUpdateDone() {
  if (!updateAttempted_) {
    // POST sem parte de arquivo nenhuma — nem passou pelo upload, entao a credencial ainda
    // nao foi conferida.
    RouterSettings current = loadUseCase_.execute();
    if (!authenticate(current)) {
      log_->println(describeUpdateOutcome("sem_credencial", 0));
      return;
    }
    log_->println(describeUpdateOutcome(updateReasonToken(FirmwareUpdateError::EmptyImage), 0));
    sendJsonError(400, to_string(FirmwareUpdateError::EmptyImage));
    return;
  }

  const bool authorized = updateAuthorized_;
  const String error = updateError_;
  const char* reason = updateReason_;
  const uint32_t bytes = updateBytes_;
  resetUpdateState();

  // Sem credencial o 401 ja saiu no primeiro bloco; responder de novo colocaria duas
  // respostas na mesma conexao. A linha do serial sai assim mesmo: e a prova de que a
  // requisicao chegou, que e o que quem esta com o cabo nao consegue ver de outro jeito.
  if (!authorized) {
    log_->println(describeUpdateOutcome("sem_credencial", bytes));
    return;
  }

  if (error.length() > 0) {
    // Toda falha acima grava o motivo junto da mensagem; o "desconhecido" so apareceria se
    // um caminho novo esquecesse de fazer isso.
    log_->println(describeUpdateOutcome(reason != nullptr ? reason : "desconhecido", bytes));
    sendJsonError(400, error);
    return;
  }

  log_->println(describeUpdateOutcome(nullptr, bytes));

  // O prazo vem do dominio e nao de um numero digitado aqui: e o mesmo que o loop() usa
  // para decidir, e duas redacoes do mesmo prazo divergem na primeira vez que uma delas
  // mudar.
  String message = "Firmware gravado. A placa reinicia agora e a página volta assim que o AP subir. ";
  message += "Abra esta página de novo e clique em Confirmar atualização em até ";
  message += numberToString(kConfirmationDeadlineMs / 60000);
  message += " minutos. Sem confirmação a placa volta sozinha para o firmware anterior.";
  JsonObject done;
  done.text("mensagem", message);
  sendJson(200, done.finish());

  // Depois do send, e de fora: um esp_restart() aqui dentro cortaria a resposta antes de
  // ela sair da fila do socket.
  if (restartRequested_ != nullptr) restartRequested_();
}
