#include "http_config_handler.h"

#include "../domain/timezone.h"
#include "html_page.h"

namespace {
const int kServerPort = 80;
const char* kAuthRealm = "roteador-4g";
}  // namespace

HttpConfigHandler::HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase)
    : loadUseCase_(loadUseCase), saveUseCase_(saveUseCase), server_(kServerPort) {}

void HttpConfigHandler::begin() {
  server_.on("/", HTTP_GET, [this]() { handleGetRoot(); });
  server_.on("/", HTTP_POST, [this]() { handlePostRoot(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();
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
  server_.send(404, "text/plain", "nao encontrado");
}

// Texto do bloco de status. Sem provider registrado a pagina diz isso em vez de omitir o
// bloco: um bloco ausente parece pagina antiga, e a frase aponta para o defeito de
// montagem em vez de deixar quem le achando que o 4G esta bem.
String HttpConfigHandler::uplinkStatusText() const {
  if (uplinkStatus_ == nullptr) {
    return "Estado do uplink indisponivel: nenhuma fonte de status foi registrada.";
  }
  return describeUplinkStatus(uplinkStatus_());
}

// Aviso da troca obrigatoria. Devolve string vazia quando nao ha pendencia — a alternativa
// (esconder por CSS) deixaria o texto no fonte da pagina de quem ja trocou.
//
// A mensagem sai do to_string() do dominio, a mesma que o POST recusado devolve: duas
// fontes de texto para a mesma regra divergem na primeira vez que uma delas e editada.
String HttpConfigHandler::adminNoticeHtml(const RouterSettings& current) const {
  if (!current.admin_password_pending) return "";
  return String("<p class=\"alert\">") +
         escapeForHtmlAttribute(to_string(SettingsValidationError::AdminPasswordMustChange)) +
         "</p>";
}

// Linha do relogio. Sem provider registrado, ou com o NTP ainda sem sincronizar, a pagina
// diz isso em vez de omitir a linha ou mostrar a epoch de 1970 formatada — a regra da Fase
// 7 e que o firmware nunca finge um horario.
String HttpConfigHandler::clockTextOrExcuse() const {
  if (clockText_ == nullptr) {
    return "indisponivel: nenhuma fonte de hora foi registrada.";
  }
  String text = clockText_();
  if (text.length() == 0) return "ainda nao sincronizado (precisa do uplink 4G).";
  return text;
}

// As <option> saem da tabela do dominio, nao de uma lista repetida aqui: duas listas para
// o mesmo conjunto divergem na primeira vez que uma for editada, e a que o validate() usa
// e a do dominio — a pagina e que ficaria oferecendo um fuso recusado na gravacao.
//
// O `selected` compara com o valor gravado. Registro cujo fuso nao esta na tabela (POST
// forjado antes desta validacao existir, downgrade de firmware) nao seleciona nada, e o
// navegador mostra a primeira opcao — gravar a pagina como esta conserta o registro.
String HttpConfigHandler::timezoneOptionsHtml(const RouterSettings& current) const {
  String html;
  for (size_t i = 0; i < timezoneOptionCount(); ++i) {
    const TimezoneOption& option = timezoneOptions()[i];
    html += "<option value=\"";
    html += escapeForHtmlAttribute(option.posix);
    html += "\"";
    if (current.timezone == option.posix) html += " selected";
    html += ">";
    html += escapeForHtmlAttribute(option.label);
    html += "</option>";
  }
  return html;
}

void HttpConfigHandler::handleGetRoot() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  String page = kConfigPageTemplate;
  page.replace("{{SSID}}", escapeForHtmlAttribute(current.wifi_ssid));
  page.replace("{{APN}}", escapeForHtmlAttribute(current.apn));
  page.replace("{{APN_USER}}", escapeForHtmlAttribute(current.apn_user));
  page.replace("{{ADMIN_USER}}", escapeForHtmlAttribute(current.admin_user));
  // O status sai de literais nossos e de um numero, entao nao ha o que escapar hoje. Passa
  // pelo escape mesmo assim: no dia em que a mensagem incluir um valor gravado — APN, por
  // exemplo — a defesa ja esta no caminho, em vez de depender de alguem lembrar dela.
  page.replace("{{UPLINK_STATUS}}", escapeForHtmlAttribute(uplinkStatusText()));
  page.replace("{{ADMIN_NOTICE}}", adminNoticeHtml(current));
  page.replace("{{CLOCK}}", escapeForHtmlAttribute(clockTextOrExcuse()));
  page.replace("{{BATTERY_RATIO}}", escapeForHtmlAttribute(String(current.battery_divider_ratio, 2)));
  // Depois dos outros replaces, e sem passar pelo escape: aqui o valor E marcacao, montada
  // por nos a partir de literais do dominio. Os textos que vao dentro dela ja foram
  // escapados um a um no timezoneOptionsHtml().
  page.replace("{{TIMEZONE_OPTIONS}}", timezoneOptionsHtml(current));
  server_.send(200, "text/html", page);
}

void HttpConfigHandler::handlePostRoot() {
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
    server_.send(400, "text/plain", "use ponto e nao virgula no divisor da bateria");
    return;
  }
  updated.battery_divider_ratio = rawRatio.toFloat();

  // A pendencia cai sozinha quando a senha muda, e sobrevive a qualquer outra gravacao.
  // Checar aqui em vez de dentro do validate() porque a regra compara duas configuracoes e
  // o validate() ve uma sozinha.
  updated.admin_password_pending = adminPasswordChangeStillRequired(current, updated);
  if (updated.admin_password_pending) {
    server_.send(400, "text/plain", to_string(SettingsValidationError::AdminPasswordMustChange));
    return;
  }

  SaveSettingsResult result = saveUseCase_.execute(updated);
  if (!result.success) {
    server_.send(400, "text/plain", to_string(result.error));
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

  String message = "Configuracao salva.";
  if (uplinkChanged) {
    // Derrubar e resubir o PPP nao afeta a associacao dos clientes ao AP, entao isso
    // pode ser feito sem reboot. Ate um minuto porque inclui o power-on do modem, o
    // registro na rede e o IPCP.
    message += " APN alterado: reconectando o 4G agora, pode levar ate 1 minuto.";
  }
  if (apChanged) {
    // Mudar SSID/senha derruba todo mundo que esta associado — inclusive quem acabou de
    // enviar este formulario. Fazer isso aqui cortaria a resposta antes dela chegar.
    message += " SSID e senha do Wi-Fi so valem apos reiniciar a placa.";
  }

  if (localChanged) {
    // Fuso e divisor valem na proxima leitura, sem reconectar nem reiniciar nada. Dizer
    // isso evita que a pessoa fique esperando um efeito que ja aconteceu.
    message += " Fuso e calibracao da bateria ja valem.";
  }

  server_.send(200, "text/plain", message);

  if (localChanged && localChanged_ != nullptr) {
    localChanged_(updated);
  }

  // Depois do send: a reconexao e assincrona, mas manter a resposta na frente evita que
  // qualquer mudanca futura nesse caminho segure o cliente esperando.
  if (uplinkChanged && uplinkChanged_ != nullptr) {
    uplinkChanged_(updated);
  }
}
