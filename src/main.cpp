#include <Arduino.h>
#include <WiFi.h>

#include "adapters/nvs_settings_repository.h"
#include "infra/wifi_ap.h"
#include "usecases/load_settings.h"
#include "usecases/save_settings.h"

#define BATTERY_PIN 35
#define BOARD_POWERON_PIN 12
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

// Teste isolado da Fase 1 (storage): grava config default no primeiro boot,
// depois so le e confirma persistencia. Sem WiFi/HTTP ainda (fases futuras).
void testSettingsStorage() {
  NvsSettingsRepository repository;
  LoadSettingsUseCase loadUseCase(repository);
  SaveSettingsUseCase saveUseCase(repository);

  RouterSettings loaded = loadUseCase.execute();
  Serial.println("--- Router settings (apos load) ---");
  Serial.printf("SSID: %s | APN: %s | Admin user: %s\n",
                loaded.wifi_ssid.c_str(), loaded.apn.c_str(), loaded.admin_user.c_str());

  SaveSettingsResult result = saveUseCase.execute(loaded);
  if (!result.success) {
    Serial.printf("Falha ao salvar settings: %s\n", to_string(result.error));
    return;
  }

  RouterSettings reloaded = loadUseCase.execute();
  bool persisted = reloaded.wifi_ssid == loaded.wifi_ssid && reloaded.apn == loaded.apn;
  Serial.printf("Persistencia confirmada: %s\n", persisted ? "sim" : "nao");
}

// Teste isolado da Fase 2 (WiFi AP): sobe o AP com as settings persistidas/default
// e confirma via serial o SSID e IP fixo. Sem HTTP/PPP/NAT ainda (fases futuras).
void testWifiAp() {
  NvsSettingsRepository repository;
  LoadSettingsUseCase loadUseCase(repository);
  RouterSettings settings = loadUseCase.execute();

  WifiAp wifiAp;
  bool started = wifiAp.start(settings);

  Serial.println("--- WiFi AP ---");
  if (!started) {
    Serial.println("Falha ao subir o AP");
    return;
  }
  Serial.printf("AP ativo | SSID: %s | IP: %s\n",
                settings.wifi_ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void setup() {
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
  pinMode(TEST_LED_PIN, OUTPUT);

  delay(100); // pequena margem para o circuito de power estabilizar
  Serial.begin(115200);
  analogReadResolution(12);

  testSettingsStorage();
  testWifiAp();
}

void loop() {
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