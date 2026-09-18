#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>

#include "../include/config.h"
#include "adapters/http_config_handler.h"
#include "adapters/nvs_settings_repository.h"
#include "infra/modem_ppp.h"
#include "infra/nat_bridge.h"
#include "infra/wifi_ap.h"
#include "usecases/load_settings.h"
#include "usecases/save_settings.h"

#define BATTERY_PIN 35
#define TEST_LED_PIN 32 // GPIO32 para testar led externo

// Ratio calibrado com multimetro em 16/09:
// Tensao real bateria: 4.16V | Tensao no pino (calculada): 1.90V
// ratio = 4.16 / 1.90 = 2.19
// Se recalibrar depois, so trocar esta constante.
#define VOLTAGE_DIVIDER_RATIO 2.19

#define ADC_MAX 4095.0
#define ADC_REF_VOLTAGE 3.3

// Quantidade de leituras para media - reduz ruido do ADC do ESP32
#define NUM_SAMPLES 20

// Curva de descarga Li-ion 1S (nao-linear).
struct BatteryPoint {
  float voltage;
  int percent;
};

BatteryPoint curve[] = {
  {4.20, 100},
  {4.15,  95},
  {4.11,  90},
  {4.08,  85},
  {4.02,  80},
  {3.98,  75},
  {3.95,  70},
  {3.91,  65},
  {3.87,  60},
  {3.85,  55},
  {3.84,  50},
  {3.82,  45},
  {3.80,  40},
  {3.79,  35},
  {3.77,  30},
  {3.75,  25},
  {3.73,  20},
  {3.71,  15},
  {3.69,  10},
  {3.61,   5},
  {3.27,   0}
};

const int curveSize = sizeof(curve) / sizeof(curve[0]);

float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(5);
  }
  float avgRaw = sum / (float)NUM_SAMPLES;
  float pinVoltage = (avgRaw / ADC_MAX) * ADC_REF_VOLTAGE;
  return pinVoltage * VOLTAGE_DIVIDER_RATIO;
}

int voltageToPercent(float voltage) {
  if (voltage >= curve[0].voltage) return 100;
  if (voltage <= curve[curveSize - 1].voltage) return 0;

  for (int i = 0; i < curveSize - 1; i++) {
    if (voltage <= curve[i].voltage && voltage >= curve[i + 1].voltage) {
      float v1 = curve[i].voltage;
      float v2 = curve[i + 1].voltage;
      int p1 = curve[i].percent;
      int p2 = curve[i + 1].percent;
      float frac = (voltage - v2) / (v1 - v2);
      return p2 + frac * (p1 - p2);
    }
  }
  return 0;
}

// Persistentes por toda a vida do firmware: o WebServer precisa sobreviver entre
// chamadas de loop(), e o modem/AP guardam os handles de esp_netif que o NAT usa e
// que a reconexao automatica (Fase 6) vai precisar.
NvsSettingsRepository settingsRepository;
LoadSettingsUseCase loadSettingsUseCase(settingsRepository);
SaveSettingsUseCase saveSettingsUseCase(settingsRepository);
HttpConfigHandler httpConfigHandler(loadSettingsUseCase, saveSettingsUseCase);

WifiAp wifiAp;
ModemPpp modemPpp;
NatBridge natBridge;

// Attach LTE ja terminou quando chegamos aqui; o que falta e so LCP/IPCP, questao de
// segundos. Um minuto e folga pra rede ruim, nao expectativa.
constexpr uint32_t kPppIpTimeoutMs = 60000;

// Sobe o roteador na ordem que as dependencias exigem: config -> AP -> modem -> NAT.
// Cada etapa so faz sentido com a anterior de pe, entao para na primeira falha e diz
// onde parou. O AP fica no ar mesmo se o 4G falhar — e por ele que se chega na pagina
// de configuracao pra corrigir o APN, entao derrubar tudo deixaria a placa inacessivel.
bool startRouting() {
  RouterSettings settings = loadSettingsUseCase.execute();
  Serial.printf("Roteamento: SSID \"%s\" | APN \"%s\"\n",
                settings.wifi_ssid.c_str(), settings.apn.c_str());

  if (!wifiAp.start(settings)) {
    Serial.println("Roteamento: parou no SoftAP — nem o AP nem a config HTTP vao responder");
    return false;
  }
  Serial.printf("Roteamento: AP no ar em %s\n", WiFi.softAPIP().toString().c_str());

  if (!modemPpp.start(settings)) {
    Serial.println("Roteamento: parou no modem — AP continua no ar so pra configuracao");
    return false;
  }

  if (!modemPpp.waitForIp(kPppIpTimeoutMs)) {
    Serial.println("Roteamento: parou esperando IP da operadora — sem uplink, sem NAT");
    return false;
  }

  if (!natBridge.enable(modemPpp.netif())) {
    Serial.println("Roteamento: parou no NAT — clientes do AP nao vao sair pra internet");
    return false;
  }

  Serial.println("Roteamento: completo — clientes do AP saem pelo 4G");
  return true;
}

void setup() {
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
  pinMode(TEST_LED_PIN, OUTPUT);

  delay(100); // pequena margem para o circuito de power estabilizar
  Serial.begin(115200);
  analogReadResolution(12);

  // Cada analogRead reconfigura o pino e o driver loga em INFO — 20 linhas por
  // leitura de bateria, o que afoga o resto do serial.
  esp_log_level_set("gpio", ESP_LOG_WARN);

  startRouting();
  httpConfigHandler.begin();
}

void loop() {
  httpConfigHandler.handleClient();

  digitalWrite(TEST_LED_PIN, HIGH);
  delay(500);
  digitalWrite(TEST_LED_PIN, LOW);
  delay(500);

  float batteryVoltage = readBatteryVoltage();
  int percent = voltageToPercent(batteryVoltage);

  Serial.print("Bateria: ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V | ~");
  Serial.print(percent);
  Serial.println("%");

  delay(2000);
}