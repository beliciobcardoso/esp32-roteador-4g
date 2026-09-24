#include "modem_ppp.h"

#include <Arduino.h>
#include <esp_event.h>
#include <esp_log.h>

#include <esp_modem_api.h>
#include <esp_netif.h>
#include <esp_netif_ppp.h>

#include <string_view>

#include "../../include/config.h"

namespace {

// Ton(uart) da SIMCom: a UART do A7670 so fica pronta ~8s depois do pulso de PWRKEY.
// O orcamento precisa passar disso com folga, senao o sync falha por pura pressa.
constexpr int kAtSyncMaxAttempts = 20;
constexpr int kAtSyncRetryDelayMs = 1000;
// Passou de Ton(uart) sem responder: o pulso provavelmente nao pegou. Tenta de novo.
constexpr int kAtSyncRepulseAttempt = 12;
// Ton(pwrkey) do A7670: a SIMCom exige o pino segurado por ~1s. Os 100ms que
// estavam aqui ficavam abaixo do minimo e o primeiro pulso simplesmente nao ligava
// o modulo — o sync AT so passava depois do re-pulso. Valor conferido contra o
// LilyGo-Modem-Series (examples/Network), que usa delay(1000).
constexpr int kPwrKeyPulseMs = 1000;
// O modem responde AT antes de terminar de inicializar o SIM; o orcamento cobre
// essa janela sem confundir "SIM acordando" com "SIM ausente".
constexpr int kSimReadyMaxAttempts = 20;
constexpr int kSimReadyRetryDelayMs = 1000;
constexpr int kSimReadyLogEvery = 5;
constexpr int kRegistrationMaxAttempts = 60;
constexpr int kRegistrationRetryDelayMs = 1000;
// Despejo de diagnostico a cada N tentativas — o suficiente pra acompanhar a evolucao
// do attach sem afogar o serial.
constexpr int kRegistrationLogEvery = 10;
constexpr int kGotIpPollIntervalMs = 250;

// O IP da operadora chega num evento tratado pela tarefa do event loop, nao pela tarefa
// que chamou start(). Estado de arquivo em vez de membro porque o handler e uma funcao
// livre e a placa tem exatamente um modem — passar `this` no `arg` custaria expor os
// tipos do esp_event no header de uma classe que nao fala de eventos.
// volatile basta: e uma palavra alinhada, escrita por uma tarefa e lida por outra, sem
// leitura-modificacao-escrita envolvida.
volatile bool gPppHasIp = false;

// esp_event_handler_register com o mesmo (base, id, handler, arg) cria uma entrada nova a
// cada chamada e o handler passa a ser invocado N vezes. Como start() roda de novo a cada
// reconexao, o registro precisa acontecer uma unica vez na vida do processo.
bool gPppEventsRegistered = false;

// Nomes dos codigos de NETIF_PPP_STATUS (esp_netif_ppp.h:50-62). Vale o log literal: a
// diferenca entre ERRORAUTHFAIL (senha do APN errada, retry nao resolve) e ERRORPEERDEAD
// (operadora sumiu, retry e exatamente o que resolve) muda o diagnostico em campo.
const char* pppStatusName(int32_t id) {
  switch (id) {
    case NETIF_PPP_ERRORNONE: return "ERRORNONE";
    case NETIF_PPP_ERRORPARAM: return "ERRORPARAM";
    case NETIF_PPP_ERROROPEN: return "ERROROPEN";
    case NETIF_PPP_ERRORDEVICE: return "ERRORDEVICE";
    case NETIF_PPP_ERRORALLOC: return "ERRORALLOC";
    case NETIF_PPP_ERRORUSER: return "ERRORUSER";
    case NETIF_PPP_ERRORCONNECT: return "ERRORCONNECT";
    case NETIF_PPP_ERRORAUTHFAIL: return "ERRORAUTHFAIL";
    case NETIF_PPP_ERRORPROTOCOL: return "ERRORPROTOCOL";
    case NETIF_PPP_ERRORPEERDEAD: return "ERRORPEERDEAD";
    case NETIF_PPP_ERRORIDLETIMEOUT: return "ERRORIDLETIMEOUT";
    case NETIF_PPP_ERRORCONNECTTIME: return "ERRORCONNECTTIME";
    case NETIF_PPP_ERRORLOOPBACK: return "ERRORLOOPBACK";
    default: return "?";
  }
}

void onPppEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
  if (base == IP_EVENT && id == IP_EVENT_PPP_GOT_IP) {
    Serial.println("PPP: IP recebido da operadora");
    gPppHasIp = true;
    return;
  }

  if (base == IP_EVENT && id == IP_EVENT_PPP_LOST_IP) {
    Serial.println("PPP: perdeu o IP da operadora");
    gPppHasIp = false;
    return;
  }

  // ERRORNONE (id 0) e o fechamento limpo da sessao, nao um erro — mas tambem significa
  // que o enlace nao esta mais de pe, entao derruba o estado igual aos outros.
  if (base == NETIF_PPP_STATUS) {
    Serial.printf("PPP: enlace caiu | NETIF_PPP_STATUS=%s (%d)\n", pppStatusName(id),
                  static_cast<int>(id));
    gPppHasIp = false;
  }
}

// O A7670E leva alguns segundos alem do pulso de PWRKEY pra comecar a responder AT,
// e o esp_modem nao tenta de novo sozinho: sem essa espera o set_mode(DATA) seguinte
// falha silenciosamente. Tempo medido na T-A7670E R2: sync passa na 3a tentativa.
//
// Pulso de PWRKEY isolado: usado no power-on e de novo se o modem nao responder AT,
// espelhando o retry do exemplo oficial. Desligar exigiria segurar >=2.5s (Toff da
// SIMCom), entao repetir um pulso de 100ms nunca derruba um modem ja ligado.
void pulsePwrKey() {
  digitalWrite(MODEM_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY_PIN, HIGH);
  delay(kPwrKeyPulseMs);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);
}

// O A7670E leva ~8s (Ton(uart) da SIMCom) alem do pulso de PWRKEY pra comecar a
// responder AT, e o esp_modem nao tenta de novo sozinho: sem essa espera o
// set_mode(DATA) seguinte falha silenciosamente.
bool waitForAtReady(esp_modem_dce_t* dce) {
  for (int attempt = 1; attempt <= kAtSyncMaxAttempts; attempt++) {
    if (esp_modem_sync(dce) == ESP_OK) {
      Serial.printf("Modem: respondeu AT na tentativa %d\n", attempt);
      return true;
    }

    if (attempt == kAtSyncRepulseAttempt) {
      Serial.println("Modem: sem resposta AT apos Ton(uart), repetindo pulso de PWRKEY");
      pulsePwrKey();
    }
    delay(kAtSyncRetryDelayMs);
  }
  Serial.println("Modem: nao respondeu AT — verificar alimentacao e pinagem da UART");
  return false;
}

// Estados do +CREG (3GPP TS 27.007): 1 = registrado na rede local, 5 = em roaming.
bool isRegistered(int state) {
  return state == 1 || state == 5;
}

// Callback de esp_modem_command: recebe cada linha crua da resposta. ESP_OK encerra o
// comando com sucesso, ESP_FAIL com erro, e qualquer outro valor pede mais linhas ate
// estourar o timeout. Imprimir a linha aqui e o unico jeito pratico de ver o dialogo
// AT sem depender dos ESP_LOGV do componente.
esp_err_t onAtResponseLine(uint8_t* data, size_t len) {
  // Corta o CR/LF do fim so pra nao picotar o log em linhas vazias.
  while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n')) {
    len--;
  }
  if (len > 0) {
    Serial.printf("    AT< %.*s\n", static_cast<int>(len), reinterpret_cast<const char*>(data));
  }

  std::string_view line(reinterpret_cast<const char*>(data), len);
  if (line.find("ERROR") != std::string_view::npos) {
    return ESP_FAIL;
  }
  if (line.find("OK") != std::string_view::npos) {
    return ESP_OK;
  }
  return ESP_ERR_NOT_FINISHED;
}

// esp_modem_command e uma das poucas funcoes da C API escritas a mao (nao geradas pelo
// .inc): a assinatura do header bate com a implementacao, sem std::string no meio.
// esp_modem_set_pdp_context, gerada, declara PdpContext& em C++ mas recebe o struct C
// de const char* na implementacao — tipos de layout incompativel, nao da pra usar.
bool runAtCommand(esp_modem_dce_t* dce, const char* command, uint32_t timeoutMs) {
  Serial.printf("    AT> %s\n", command);
  esp_err_t result = esp_modem_command(dce, command, &onAtResponseLine, timeoutMs);
  if (result != ESP_OK) {
    Serial.printf("    AT! falhou [%s]\n", esp_err_to_name(result));
  }
  return result == ESP_OK;
}

// Resposta do AT+SIMCOMATI, copiada pelo callback. Estado de arquivo pelo mesmo motivo do
// gPppHasIp: o callback do esp_modem_command e funcao livre, sem ponteiro de contexto.
String gSimcomatiResponse;

// Diferente do onAtResponseLine: nao imprime a resposta, porque ela traz o IMEI. E o
// esp_modem entrega o buffer acumulado desde o comando a cada chamada, nao so a parte nova
// (por isso o eco aparece repetido no serial dos outros AT) — entao cada chamada substitui
// a copia inteira em vez de concatenar.
esp_err_t captureSimcomati(uint8_t* data, size_t len) {
  gSimcomatiResponse = String();
  for (size_t i = 0; i < len; i++) {
    gSimcomatiResponse += static_cast<char>(data[i]);
  }

  std::string_view text(reinterpret_cast<const char*>(data), len);
  if (text.find("ERROR") != std::string_view::npos) {
    return ESP_FAIL;
  }
  // "\nOK" e nao "OK": o eco do comando vem antes, entao o OK final sempre segue uma quebra.
  if (text.find("\nOK") != std::string_view::npos) {
    return ESP_OK;
  }
  return ESP_ERR_NOT_FINISHED;
}

// ATENCAO: nao usar esp_modem_at nem nenhum comando com saida de string (get_imsi,
// get_operator_name). O header do componente, compilado em C++, declara o parametro
// de saida como `std::string&`, mas a implementacao em esp_modem_c_api.cpp e
// `char *p_out` e faz strlcpy ate CONFIG_ESP_MODEM_C_API_STR_MAX nele. Passar um
// std::string arrasa a stack e derruba a placa no interrupt watchdog. Os comandos
// com saida int/bool nao tem esse problema: ali a ABI de referencia e ponteiro
// coincide de verdade.
void dumpNetworkDiagnostics(esp_modem_dce_t* dce) {
  int rssi = 0;
  int ber = 0;
  esp_err_t csqResult = esp_modem_get_signal_quality(dce, rssi, ber);
  // RSSI 99 no 3GPP TS 27.007 significa "desconhecido ou nao detectavel", nao sinal zero.
  Serial.printf("    CSQ [%s] rssi=%d ber=%d\n", esp_err_to_name(csqResult), rssi, ber);

  // Resposta crua do registro e da operadora: o parser do esp_modem pega so o campo
  // depois da primeira virgula, e ja devolveu estado=11 (que nao existe no 3GPP
  // TS 27.007). Ver o texto literal e a unica forma de saber o que o modulo respondeu.
  runAtCommand(dce, "AT+CEREG?\r", 2000);
  runAtCommand(dce, "AT+COPS?\r", 5000);
  runAtCommand(dce, "AT+CGDCONT?\r", 2000);
}

// Logo depois do boot o modulo ja responde AT mas ainda esta lendo o cartao: nessa
// janela AT+CPIN? volta "+CME ERROR: 14 (SIM busy)" e o esp_modem so propaga
// ESP_FAIL, indistinguivel de cartao ausente. Uma sondagem unica dava falso negativo
// — insiste dentro de um orcamento antes de culpar o hardware.
// ESP_OK com pinOk=false e outra coisa: o cartao respondeu e esta pedindo PIN, o que
// nenhum retry resolve.
bool waitForSimReady(esp_modem_dce_t* dce) {
  for (int attempt = 1; attempt <= kSimReadyMaxAttempts; attempt++) {
    bool pinOk = false;
    esp_err_t pinResult = esp_modem_read_pin(dce, pinOk);
    if (pinResult == ESP_OK && pinOk) {
      Serial.printf("Modem: SIM pronto na tentativa %d\n", attempt);
      return true;
    }
    if (pinResult == ESP_OK && !pinOk) {
      Serial.println("Modem: SIM pede PIN — desbloquear o cartao antes de usar");
      return false;
    }
    if (attempt % kSimReadyLogEvery == 1) {
      Serial.printf("Modem: aguardando SIM (%ds) | CPIN -> [%s]\n",
                    attempt, esp_err_to_name(pinResult));
    }
    delay(kSimReadyRetryDelayMs);
  }
  Serial.println("Modem: SIM nao respondeu ao AT+CPIN? — conferir encaixe do cartao");
  return false;
}

// O esp_modem so manda AT+CGDCONT dentro de setup_data_mode(), que roda no
// set_mode(DATA) — ou seja, depois da espera por registro. Tarde demais: em LTE o
// attach ja carrega um PDN Connectivity Request, entao o modulo tentava registrar
// com o contexto de fabrica dele e a operadora recusava (CEREG=3). Gravar o APN
// aqui, antes do CEREG, e o que faz o attach usar o APN certo.
bool applyPdpContext(esp_modem_dce_t* dce, const String& apn) {
  // esp_modem_set_apn() nao serve aqui: ele so troca o PdpContext guardado em memoria,
  // que continua sendo enviado la no setup_data_mode(). Precisa ser o AT cru agora.
  String command = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"\r";
  Serial.printf("Modem: gravando APN \"%s\" no contexto 1\n", apn.c_str());
  return runAtCommand(dce, command.c_str(), 3000);
}

// Discar antes do modem registrar na rede faz o PPP subir e nunca receber IP.
// Attach LTE leva bem mais que o handshake AT, entao precisa de espera propria.
// Os parametros OUT do esp_modem sao referencia (nao ponteiro) quando o header
// e incluido em C++.
bool waitForNetwork(esp_modem_dce_t* dce, const String& apn) {
  if (!waitForSimReady(dce)) {
    return false;
  }

  if (!applyPdpContext(dce, apn)) {
    return false;
  }

  for (int attempt = 1; attempt <= kRegistrationMaxAttempts; attempt++) {
    int state = 0;
    esp_err_t result = esp_modem_get_network_registration_state(dce, state);
    if (result == ESP_OK && isRegistered(state)) {
      int rssi = 0;
      int ber = 0;
      esp_modem_get_signal_quality(dce, rssi, ber);
      Serial.printf("Modem: registrado (CEREG=%d) apos %ds | RSSI=%d\n", state, attempt, rssi);
      return true;
    }

    if (attempt % kRegistrationLogEvery == 1) {
      Serial.printf("Modem: aguardando registro (%ds) | CEREG -> [%s] estado=%d\n",
                    attempt, esp_err_to_name(result), state);
      dumpNetworkDiagnostics(dce);
    }
    delay(kRegistrationRetryDelayMs);
  }

  Serial.println("Modem: nao registrou na rede — verificar antena LTE, SIM e cobertura");
  return false;
}

}  // namespace

void ModemPpp::powerOnSequence() {
  // Ordem da sequencia oficial da LilyGO (LilyGo-Modem-Series, examples/Network):
  // POWERON HIGH -> pulso de RESET -> DTR LOW -> pulso de PWRKEY.
  // POWERON (GPIO12) ja foi ligado no setup(), antes de tudo.

  // O modem tem alimentacao propria e sobrevive a um reboot do ESP32 — sem reset ele
  // pode continuar em modo de dados ou dormindo, e a UART fica muda. O reset e o unico
  // jeito de partir de um estado conhecido. 2600ms segue o Treset da SIMCom (min 2s).
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);

  // DTR em nivel alto mantem o modulo em sleep, ignorando AT.
  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);

  pinMode(MODEM_PWRKEY_PIN, OUTPUT);
  pulsePwrKey();
}

// Uma vez por boot: o modem nao troca de modelo entre reconexoes, e o start() de cada
// reconexao ja carrega esperas de SIM e registro o bastante. Roda em modo comando, antes do
// PPP — e o unico momento em que AT passa pela UART sem CMUX (PRD 15). Falhar aqui so deixa
// a identidade vazia; o enlace segue, porque inventario nao pode custar internet.
void ModemPpp::readIdentityOnce() {
  if (identityRead_) {
    return;
  }
  identityRead_ = true;

  gSimcomatiResponse = String();
  esp_err_t result = esp_modem_command(dce_, "AT+SIMCOMATI\r", &captureSimcomati, 3000);
  if (result != ESP_OK) {
    Serial.printf("Modem: AT+SIMCOMATI falhou [%s]\n", esp_err_to_name(result));
  }
  identity_ = parseSimcomati(gSimcomatiResponse);
  gSimcomatiResponse = String();
  Serial.println(describeModem(identity_));
}

bool ModemPpp::start(const RouterSettings& settings) {
  // Para depurar o dialogo AT cru: ligar CONFIG_ESP_MODEM_ADD_DEBUG_LOGS=y e
  // CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y, e subir os TAGs command_lib, modem_api e
  // uart_terminal para ESP_LOG_VERBOSE. Sao esses os TAGs reais do componente
  // ("esp-modem" nao e TAG de nada e nao produz saida nenhuma).
  powerOnSequence();

  // esp_netif e o event loop default sao pre-requisito de esp_netif_new/esp_event_handler_register
  // (todo exemplo oficial esp_modem chama isso antes). Idempotente: retorna ESP_ERR_INVALID_STATE se
  // ja inicializado (ex.: pelo WiFi arduino), o que e esperado aqui e nao indica falha.
  esp_netif_init();
  esp_event_loop_create_default();

  if (!gPppEventsRegistered) {
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &onPppEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &onPppEvent, nullptr);
    esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &onPppEvent, nullptr);
    gPppEventsRegistered = true;
  }

  esp_netif_config_t netifPppConfig = ESP_NETIF_DEFAULT_PPP();
  netif_ = esp_netif_new(&netifPppConfig);
  if (netif_ == nullptr) {
    return false;
  }

  // A Vivo exige PAP no APN (usuario/senha "vivo"). Autenticar na camada PPP evita
  // AT+CGAUTH: o esp_modem_at do componente declara o parametro de saida como
  // std::string& no header C++ mas faz strlcpy de char* na implementacao, e usa-lo
  // arrasa a stack. Operadora sem autenticacao so deixa os campos vazios.
  // Exige CONFIG_LWIP_PPP_PAP_SUPPORT=y: sem isso o esp_netif devolve
  // ESP_ERR_ESP_NETIF_IF_NOT_READY e a autenticacao e silenciosamente ignorada.
  if (settings.apn_user.length() > 0) {
    esp_err_t authResult = esp_netif_ppp_set_auth(netif_, NETIF_PPP_AUTHTYPE_PAP,
                                                  settings.apn_user.c_str(),
                                                  settings.apn_password.c_str());
    Serial.printf("PPP: auth PAP usuario=\"%s\" -> [%s]\n",
                  settings.apn_user.c_str(), esp_err_to_name(authResult));
    if (authResult != ESP_OK) {
      Serial.println("PPP: falha ao configurar PAP — conexao segue sem autenticacao");
    }
  }

  esp_modem_dte_config_t dteConfig = ESP_MODEM_DTE_DEFAULT_CONFIG();
  dteConfig.uart_config.tx_io_num = MODEM_TX_PIN;
  dteConfig.uart_config.rx_io_num = MODEM_RX_PIN;
  dteConfig.uart_config.baud_rate = MODEM_UART_BAUD;
  // A7670E aqui usa so TX/RX (sem flow control) — o default do esp_modem deixa
  // rts_io_num/cts_io_num apontando pra GPIOs reais (27/23), que colidem com o TX
  // (GPIO27) e causam falha silenciosa no uart_set_pin. Desliga os dois.
  dteConfig.uart_config.rts_io_num = UART_PIN_NO_CHANGE;
  dteConfig.uart_config.cts_io_num = UART_PIN_NO_CHANGE;

  esp_modem_dce_config_t dceConfig = ESP_MODEM_DCE_DEFAULT_CONFIG(settings.apn.c_str());

  // A7670E usa conjunto de comandos AT compativel com o perfil SIM7600 do esp_modem.
  dce_ = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dteConfig, &dceConfig, netif_);
  Serial.printf("Modem: esp_modem_new_dev -> %s\n", dce_ == nullptr ? "NULL" : "ok");
  if (dce_ == nullptr) {
    return false;
  }

  if (!waitForAtReady(dce_)) {
    return false;
  }

  readIdentityOnce();

  if (!waitForNetwork(dce_, settings.apn)) {
    return false;
  }

  // Zera antes de discar: um start() depois de uma queda nao pode herdar o "tem IP"
  // da sessao anterior.
  gPppHasIp = false;

  esp_err_t modeResult = esp_modem_set_mode(dce_, ESP_MODEM_MODE_DATA);
  Serial.printf("Modem: set_mode(DATA) -> %s\n", esp_err_to_name(modeResult));
  return modeResult == ESP_OK;
}

// Polling em vez de semaforo pra seguir o padrao das outras esperas deste arquivo
// (waitForAtReady, waitForSimReady, waitForNetwork) — e tudo boot sequencial, nada
// aqui disputa CPU com outra coisa.
bool ModemPpp::waitForIp(uint32_t timeoutMs) {
  for (uint32_t waited = 0; waited < timeoutMs; waited += kGotIpPollIntervalMs) {
    if (gPppHasIp) {
      return true;
    }
    delay(kGotIpPollIntervalMs);
  }
  Serial.printf("PPP: operadora nao entregou IP em %us — LCP/IPCP nao fechou\n",
                timeoutMs / 1000);
  return false;
}

// Nao tenta voltar pro modo de comando antes de destruir: a sequencia de escape leva
// segundos, falha justamente quando o modulo travou (que e quando isso e chamado), e o
// powerOnSequence do proximo start() da um reset por hardware que resolve de qualquer
// jeito. Ordem importa — o DCE referencia o netif, entao morre primeiro.
void ModemPpp::stop() {
  if (dce_ != nullptr) {
    esp_modem_destroy(dce_);
    dce_ = nullptr;
  }
  if (netif_ != nullptr) {
    esp_netif_destroy(netif_);
    netif_ = nullptr;
  }
  gPppHasIp = false;
}

bool ModemPpp::hasIp() const {
  return gPppHasIp;
}
