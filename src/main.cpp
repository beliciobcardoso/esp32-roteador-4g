#include <Arduino.h>
#include <WiFi.h>

#include "../include/config.h"
#include "adapters/http_config_handler.h"
#include "adapters/nvs_settings_repository.h"
#include "domain/battery.h"
#include "infra/battery_adc.h"
#include "infra/link_supervisor.h"
#include "infra/modem_ppp.h"
#include "infra/nat_bridge.h"
#include "infra/wifi_ap.h"
#include "usecases/load_settings.h"
#include "usecases/save_settings.h"

#define TEST_LED_PIN 32 // GPIO32 para testar led externo

// Persistentes por toda a vida do firmware: o WebServer precisa sobreviver entre
// chamadas de loop(), e o modem/AP guardam os handles de esp_netif que o NAT usa e
// que a reconexao automatica (Fase 6) vai precisar.
NvsSettingsRepository settingsRepository;
LoadSettingsUseCase loadSettingsUseCase(settingsRepository);
SaveSettingsUseCase saveSettingsUseCase(settingsRepository);
HttpConfigHandler httpConfigHandler(loadSettingsUseCase, saveSettingsUseCase);

BatteryAdc batteryAdc;
WifiAp wifiAp;
ModemPpp modemPpp;
NatBridge natBridge;
LinkSupervisor linkSupervisor(modemPpp, natBridge);

// Ponte entre o adaptador HTTP e o supervisor: o handler nao conhece o modem, e o
// supervisor nao conhece HTTP.
void onUplinkSettingsChanged(const RouterSettings& updated) {
  linkSupervisor.applySettings(updated);
}

// Sobe o roteador na ordem que as dependencias exigem: config -> AP -> supervisao do
// uplink. O AP e sincrono porque a pagina de configuracao depende dele; o 4G fica com o
// supervisor, que conecta em background e reconecta sozinho depois.
//
// O AP fica no ar mesmo se o 4G nunca conectar — e por ele que se chega na pagina de
// configuracao pra corrigir o APN, entao derrubar tudo deixaria a placa inacessivel.
bool startRouting() {
  RouterSettings settings = loadSettingsUseCase.execute();
  Serial.printf("Roteamento: SSID \"%s\" | APN \"%s\"\n",
                settings.wifi_ssid.c_str(), settings.apn.c_str());

  if (!wifiAp.start(settings)) {
    Serial.println("Roteamento: parou no SoftAP — nem o AP nem a config HTTP vao responder");
    return false;
  }
  Serial.printf("Roteamento: AP no ar em %s\n", WiFi.softAPIP().toString().c_str());

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
  batteryAdc.begin();

  startRouting();
  httpConfigHandler.onUplinkSettingsChanged(&onUplinkSettingsChanged);
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