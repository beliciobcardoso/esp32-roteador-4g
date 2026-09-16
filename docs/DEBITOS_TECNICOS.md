# Débitos Técnicos

## 1. `VOLTAGE_DIVIDER_RATIO` hardcoded e calibrado por placa

**Onde:** [src/main.cpp:11](../src/main.cpp:11)

Constante `2.19` calibrada com multímetro numa placa específica (16/09). Resistores do divisor variam por tolerância/placa — ratio não é universal.

**Ação:** mover para `config.h` (já previsto no plano, Fase 1) como default de fábrica, sobrescrevível via NVS/config, em vez de `#define` fixo no firmware.

## 2. Perda de precisão silenciosa em `voltageToPercent`

**Onde:** [src/main.cpp:73](../src/main.cpp:73)

`return p2 + frac * (p1 - p2);` retorna `float` em função `int` — trunca sem aviso. Funciona pra exibição de %, mas não está documentado como intencional (podia arredondar com `round()` ou já declarar o intuito no comentário).

**Ação:** decidir explicitamente — arredondar (`round()`) se quiser %, ou mudar assinatura pra `float` se precisão importar em algum consumidor futuro (ex: log/telemetria).

## 3. Sem suavização entre ciclos de leitura de bateria

**Onde:** [src/main.cpp:51-60](../src/main.cpp:51)

Cada `loop()` reamostra do zero (`NUM_SAMPLES` leituras), sem média móvel ou filtro entre ciclos anteriores. Ruído do ADC pode causar variação de % perceptível entre prints consecutivos.

**Ação:** avaliar filtro exponencial (EMA) entre leituras se oscilação incomodar na UI final; por ora é decisão aceita pro protótipo, não bug.

## 4. `loop()` bloqueia `httpConfigHandler.handleClient()` por ~3s por ciclo

**Onde:** [src/main.cpp](../src/main.cpp) — `loop()`

`delay(500)` x2 (blink LED) + `delay(2000)` (intervalo de leitura de bateria) somam ~3s de bloqueio síncrono antes do próximo `handleClient()`. Requisição HTTP à página de config pode demorar até 3s pra responder.

**Ação:** migrar blink/leitura de bateria pra non-blocking (`millis()` em vez de `delay()`), ou mover `WebServer` pra rodar em task própria do FreeRTOS. Aceito por ora — protótipo funcional, latência não trava a config, só atrasa.
