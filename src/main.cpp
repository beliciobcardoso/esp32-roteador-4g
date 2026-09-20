#include <Arduino.h>
#include <WiFi.h>

#include "../include/config.h"
#include "adapters/http_config_handler.h"
#include "adapters/nvs_settings_repository.h"
#include "domain/battery.h"
#include "infra/battery_adc.h"
#include "infra/clock.h"
#include "infra/dns_forwarder.h"
#include "infra/entropy.h"
#include "infra/link_supervisor.h"
#include "infra/modem_ppp.h"
#include "infra/nat_bridge.h"
#include "infra/wifi_ap.h"
#include "usecases/load_settings.h"
#include "usecases/provision_settings.h"
#include "usecases/save_settings.h"

#define TEST_LED_PIN 32 // GPIO32 para testar led externo

// Persistentes por toda a vida do firmware: o WebServer precisa sobreviver entre
// chamadas de loop(), e o modem/AP guardam os handles de esp_netif que o NAT usa e
// que a reconexao automatica (Fase 6) vai precisar.
NvsSettingsRepository settingsRepository;
ProvisionSettingsUseCase provisionSettingsUseCase(settingsRepository, &fillRandomBytes);
LoadSettingsUseCase loadSettingsUseCase(settingsRepository);
SaveSettingsUseCase saveSettingsUseCase(settingsRepository);
HttpConfigHandler httpConfigHandler(loadSettingsUseCase, saveSettingsUseCase);

BatteryAdc batteryAdc;
// `systemClock` e nao `clock`: <time.h> ja declara ::clock() no escopo global, e uma
// variavel com esse nome conflita na hora do link.
Clock systemClock;
WifiAp wifiAp;
ModemPpp modemPpp;
NatBridge natBridge;
DnsForwarder dnsForwarder;
LinkSupervisor linkSupervisor(modemPpp, natBridge);

// Ponte entre o adaptador HTTP e o supervisor: o handler nao conhece o modem, e o
// supervisor nao conhece HTTP.
void onUplinkSettingsChanged(const RouterSettings& updated) {
  linkSupervisor.applySettings(updated);
}

// Mudanca que vale a quente e nao passa pelo uplink. Nao derruba o PPP de proposito:
// reconectar o 4G por causa de um fuso seria estrago sem motivo.
void onLocalSettingsChanged(const RouterSettings& updated) {
  systemClock.applyTimezone(updated.timezone);
  batteryAdc.applyDividerRatio(updated.battery_divider_ratio);
}

// Gatilho da sincronizacao: o unico momento em que se sabe que ha rota para fora. Roda na
// task do supervisor, nao na do loop() — por isso nao toca em nada do HTTP.
void onUplinkOnline() {
  systemClock.onUplinkOnline();
}

// A pagina pergunta a hora sem conhecer quem responde. String vazia enquanto nao houve
// sincronizacao nenhuma; quem formata a desculpa e o adaptador.
String currentClockText() {
  return systemClock.nowText();
}

// Mesma ponte, no sentido contrario: a pagina pergunta como esta o 4G sem conhecer quem
// responde. Chamada da task do loop(), enquanto o supervisor roda na dele.
UplinkStatus currentUplinkStatus() {
  return linkSupervisor.status();
}

// Sobe o roteador na ordem que as dependencias exigem: config -> AP -> supervisao do
// uplink. O AP e sincrono porque a pagina de configuracao depende dele; o 4G fica com o
// supervisor, que conecta em background e reconecta sozinho depois.
//
// O AP fica no ar mesmo se o 4G nunca conectar — e por ele que se chega na pagina de
// configuracao pra corrigir o APN, entao derrubar tudo deixaria a placa inacessivel.
// Unico momento em que as credenciais sorteadas podem ser lidas. Quem gravou a placa sem o
// monitor serial aberto perde este bloco, e a saida e apagar a NVS e reprovisionar — e o
// preco de nao existir senha padrao. Em campo, e aqui que se gera a etiqueta da unidade.
void reportProvisioning(const ProvisionResult& result) {
  if (!result.provisioned) return;

  Serial.println("=== Provisionamento (primeiro boot) ===");
  Serial.printf("AP    \"%s\"  senha: %s\n", result.settings.wifi_ssid.c_str(),
                result.settings.wifi_password.c_str());
  Serial.printf("Admin \"%s\"  senha: %s (troca obrigatoria no primeiro acesso)\n",
                result.settings.admin_user.c_str(), result.settings.admin_password.c_str());

  if (!result.persisted) {
    // Grave: o AP sobe com esta senha, mas o proximo boot sorteia outra. Quem anotar a
    // senha agora perde o acesso no reset seguinte sem nenhum sinal de que algo falhou.
    Serial.println("ATENCAO: o sorteio NAO foi gravado na NVS — estas senhas valem so ate o");
    Serial.println("proximo boot, que vai sortear outras. Verificar a particao nvs.");
  }
  Serial.println("=======================================");
}

bool startRouting(const RouterSettings& settings) {
  Serial.printf("Roteamento: SSID \"%s\" | APN \"%s\"\n",
                settings.wifi_ssid.c_str(), settings.apn.c_str());

  // Sem AP nao ha pagina de configuracao para desfazer nada, entao configuracao invalida
  // para aqui em vez de virar softAP em estado indefinido. Acontece com NVS ilegivel: o
  // fallback do LoadSettingsUseCase devolve senhas vazias de proposito.
  SettingsValidationError invalid = validate(settings);
  if (invalid != SettingsValidationError::None) {
    Serial.printf("Roteamento: configuracao invalida (%s) — AP nao vai subir\n",
                  to_string(invalid));
    return false;
  }

  if (!wifiAp.start(settings)) {
    Serial.println("Roteamento: parou no SoftAP — nem o AP nem a config HTTP vao responder");
    return false;
  }
  Serial.printf("Roteamento: AP no ar em %s\n", WiFi.softAPIP().toString().c_str());

  // Depois do AP e antes do uplink: e o endereco do AP que o forwarder ocupa, e os clientes
  // ja recebem esse endereco como DNS no primeiro lease. Falhar aqui nao derruba o
  // roteamento — quebra a resolucao de nomes, que e grave o bastante para ir pro log e
  // leve o bastante para nao valer perder a pagina de configuracao junto.
  if (!dnsForwarder.begin(static_cast<uint32_t>(WiFi.softAPIP()))) {
    Serial.println("Roteamento: DNS local nao subiu — clientes vao rotear, mas nao resolver");
  }

  if (!linkSupervisor.begin(settings)) {
    Serial.println("Roteamento: parou na supervisao do uplink — AP so pra configuracao");
    return false;
  }

  Serial.println("Roteamento: AP no ar, uplink 4G conectando em background");
  return true;
}

void setup() {
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
  pinMode(TEST_LED_PIN, OUTPUT);

  delay(100); // pequena margem para o circuito de power estabilizar
  Serial.begin(115200);

  // Antes do batteryAdc.begin() e antes de qualquer radio, e a ordem nao e arbitraria: o
  // sorteio usa o SAR ADC como fonte de entropia (infra/entropy.cpp) e o contrato da IDF
  // exige fechar essa janela antes de inicializar ADC ou RF.
  ProvisionResult provision = provisionSettingsUseCase.execute();
  reportProvisioning(provision);

  batteryAdc.begin(provision.settings.battery_divider_ratio);

  // Antes do startRouting(): begin() so aplica o fuso e prepara o SNTP, mas o supervisor
  // pode entrar em Online logo depois de subir, e o callback precisa achar tudo pronto.
  systemClock.begin(provision.settings.timezone);
  linkSupervisor.onUplinkOnline(&onUplinkOnline);

  startRouting(provision.settings);
  httpConfigHandler.onUplinkSettingsChanged(&onUplinkSettingsChanged);
  httpConfigHandler.onUplinkStatusRequested(&currentUplinkStatus);
  httpConfigHandler.onLocalSettingsChanged(&onLocalSettingsChanged);
  httpConfigHandler.onClockTextRequested(&currentClockText);
  httpConfigHandler.begin();
}

// Cadencias do loop. Antes eram delay() em sequencia, o que segurava o loop inteiro por
// ~3s: o handleClient() so rodava uma vez a cada 3s (a pagina de config demorava a
// responder) e a supervisao do modem so reagiria a uma queda com ate 3s de atraso.
constexpr unsigned long kLedBlinkIntervalMs = 500;
constexpr unsigned long kBatteryReportIntervalMs = 3000;

// Marcos da ultima execucao de cada tarefa periodica. unsigned long com aritmetica de
// subtracao trata o overflow de millis() (~49 dias) corretamente — nao trocar por
// comparacao direta de instantes, que quebra na virada.
unsigned long lastLedToggleMs = 0;
unsigned long lastBatteryReportMs = 0;
bool ledOn = false;

void blinkLed(unsigned long now) {
  if (now - lastLedToggleMs < kLedBlinkIntervalMs) {
    return;
  }
  lastLedToggleMs = now;
  ledOn = !ledOn;
  digitalWrite(TEST_LED_PIN, ledOn ? HIGH : LOW);
}

void reportBattery(unsigned long now) {
  if (now - lastBatteryReportMs < kBatteryReportIntervalMs) {
    return;
  }
  lastBatteryReportMs = now;

  // Ainda bloqueia ~100ms dentro do readVoltage(). Mantido: a media e o que tira o ruido
  // do ADC, e 100ms a cada 3s nao atrapalha nem o HTTP nem a supervisao do modem.
  float batteryVoltage = batteryAdc.readVoltage();
  int percent = voltageToPercent(batteryVoltage);

  Serial.print("Bateria: ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V | ~");
  Serial.print(percent);
  Serial.println("%");
}

void loop() {
  unsigned long now = millis();

  httpConfigHandler.handleClient();
  blinkLed(now);
  reportBattery(now);
}