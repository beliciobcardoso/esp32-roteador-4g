#include "battery_adc.h"

#include <Arduino.h>
#include <esp_log.h>

#include "../../include/config.h"

namespace {
// Espacamento entre amostras. Cada leitura do ADC carrega ruido; espacar evita medir 20
// vezes o mesmo transiente e chamar isso de media.
const int kSampleIntervalMs = 5;
}  // namespace

void BatteryAdc::applyDividerRatio(float dividerRatio) {
  dividerRatio_ = dividerRatio;
}

void BatteryAdc::begin(float dividerRatio) {
  applyDividerRatio(dividerRatio);
  analogReadResolution(12);

  // Cada analogRead reconfigura o pino e o driver de GPIO loga em INFO — 20 linhas por
  // leitura de bateria, o que afoga o resto do serial. Mudanca global de nivel de log,
  // mora aqui porque a causa e esta: se a leitura de bateria sair, a gambiarra sai junto.
  esp_log_level_set("gpio", ESP_LOG_WARN);
}

float BatteryAdc::readVoltage() {
  long sum = 0;
  for (int i = 0; i < BATTERY_ADC_SAMPLES; ++i) {
    sum += analogRead(BATTERY_ADC_PIN);
    delay(kSampleIntervalMs);
  }

  const float averageRaw = sum / static_cast<float>(BATTERY_ADC_SAMPLES);
  const float pinVoltage = (averageRaw / BATTERY_ADC_MAX) * BATTERY_ADC_REF_VOLTAGE;
  // O ratio vem da configuracao, nao mais do #define: os resistores variam por placa
  // dentro da tolerancia, entao recalibrar deixou de exigir regravar o firmware (debito 1).
  // O default de fabrica continua no config.h, e e ele que o provisionamento grava.
  return pinVoltage * dividerRatio_;
}
