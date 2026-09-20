#pragma once

// INFRA — leitura do ADC ligado ao divisor resistivo da bateria. Amostra o pino e devolve
// tensao; converter isso em percentual e do dominio (domain/battery.h).
class BatteryAdc {
 public:
  // Configura o ADC e fixa a calibracao do divisor. Chamar uma vez no setup(), antes da
  // primeira leitura.
  void begin(float dividerRatio);

  // Troca a calibracao a quente, quando a configuracao muda. Sem lock: quem grava e o
  // handler HTTP, que roda dentro do handleClient() do loop() — a mesma task que faz a
  // leitura. Se o HTTP sair do loop(), isto precisa de proteccao.
  void applyDividerRatio(float dividerRatio);

  // Tensao da bateria em volts, ja com o divisor compensado. Bloqueia ~100 ms: sao
  // BATTERY_ADC_SAMPLES leituras espacadas, e a media e o que tira o ruido do ADC do
  // ESP32. Nao chamar de dentro de caminho sensivel a latencia.
  float readVoltage();

 private:
  // Sem default valido de proposito: begin() e obrigatorio, e 0.0 faz a leitura sair 0 V
  // em vez de sair errada por um fator plausivel.
  float dividerRatio_ = 0.0f;
};
