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

## 5. Limite real de clientes do AP é 10, não 20

**Onde:** [src/infra/wifi_ap.cpp:11](../src/infra/wifi_ap.cpp:11)

`kMaxClients = 20`, mas o driver corta em 10 e avisa no boot:

```
W (1209) wifi:Affected by the ESP-NOW encrypt num, set the max connection num to 10
```

As chaves reservadas ao ESP-NOW consomem os slots. `AGENTS.md` e `PLANO_ROTEADOR.md`
prometem "até 20 clientes simultâneos" — hoje o firmware não cumpre.

**Ação:** não usamos ESP-NOW; `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=0` deve liberar os
10 slots restantes. Validar na Fase 6 (testes de carga), que é quando isso é exercitado
de verdade.

## 6. Rollback de OTA ainda desabilitado

**Onde:** [sdkconfig.defaults](../sdkconfig.defaults), [partitions.csv](../partitions.csv)

A tabela de partições já tem os dois slots (`app0`/`app1`) e `otadata`, mas
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` está intencionalmente desligado: sem um cliente OTA
que chame `esp_ota_mark_app_valid_cancel_rollback()`, ligar o rollback faria o bootloader
reverter toda imagem nova como se tivesse falhado.

**Ação:** habilitar junto com a implementação do cliente OTA — os dois são indissociáveis.

## 7. Partição `spiffs` reservada mas não montada

**Onde:** [partitions.csv](../partitions.csv)

256 KB em `0x3C0000` reservados e nunca montados. Custo zero hoje (é só endereço não usado),
mas é espaço parado se nada for escrito ali.

**Ação:** decidir na Fase 6 — montar para logs/telemetria persistente, ou devolver o espaço
aos slots de app.

## 8. `infra/modem_ppp` escreve diagnóstico direto no `Serial`

**Onde:** [src/infra/modem_ppp.cpp](../src/infra/modem_ppp.cpp)

Toda saída de diagnóstico usa `Serial.printf` direto. Funciona e foi essencial na
depuração, mas acopla a camada de infra ao transporte de log — não dá para redirecionar
para a página web, um buffer em RAM ou telemetria sem reescrever as chamadas.

**Ação:** avaliar uma abstração de log quando houver um segundo consumidor real. Hoje há
apenas um; criar a abstração agora seria abstração especulativa.
