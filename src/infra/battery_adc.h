#pragma once

// INFRA — leitura do ADC ligado ao divisor resistivo da bateria. Amostra o pino e devolve
// tensao; converter isso em percentual e do dominio (domain/battery.h).
class BatteryAdc {
 public:
  // Configura o ADC. Chamar uma vez no setup(), antes da primeira leitura.
  void begin();

  // Tensao da bateria em volts, ja com o divisor compensado. Bloqueia ~100 ms: sao
  // BATTERY_ADC_SAMPLES leituras espacadas, e a media e o que tira o ruido do ADC do
  // ESP32. Nao chamar de dentro de caminho sensivel a latencia.
  float readVoltage();
};
