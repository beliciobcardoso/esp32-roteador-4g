#pragma once

// Defaults de fabrica — usados no primeiro boot, quando a NVS ainda nao tem config salva.
#define DEFAULT_AP_SSID "esp32-roteador-4g"
// Nao ha DEFAULT_AP_PASSWORD nem DEFAULT_ADMIN_PASSWORD, e a ausencia e a correcao: as
// duas eram iguais em toda unidade e publicas para quem tem o repositorio. Agora sao
// sorteadas por placa no primeiro boot (usecases/provision_settings) e impressas uma vez
// no serial. Consequencia: placa gravada sem o monitor aberto nao mostra a senha, e a
// saida e apagar a NVS e reprovisionar (debito 10, PRD 08).
// APN da Vivo (MCC 724 / MNC 11), conferido nas definicoes do proprio chip no celular.
// "internet" nao existe nessa rede: em LTE o attach carrega um PDN Connectivity Request
// junto, entao APN desconhecido faz a operadora recusar o attach inteiro (EMM cause #27)
// e o CEREG fica preso em 3 (registro negado), mesmo com sinal cheio.
#define DEFAULT_APN "zap.vivo.com.br"
#define DEFAULT_APN_USER "vivo"
#define DEFAULT_APN_PASSWORD "vivo"
#define DEFAULT_ADMIN_USER "admin"

// Namespace usado na NVS para as chaves de configuracao do roteador.
#define SETTINGS_NVS_NAMESPACE "router_cfg"

// --- Bateria ---
// Ratio do divisor resistivo, calibrado com multimetro em 16/09 numa placa especifica:
// tensao real 4.16 V, tensao no pino 1.90 V, 4.16 / 1.90 = 2.19. Resistores variam dentro
// da tolerancia, entao este numero NAO e universal — e default de fabrica, como os de
// cima, e nao constante de firmware. Sobrescrever por configuracao ainda nao existe: falta
// campo na NVS, e isso espera o schema 3 da Fase 7 (debito 1). Recalibrou? troca aqui.
#define BATTERY_VOLTAGE_DIVIDER_RATIO 2.19f
#define BATTERY_ADC_PIN 35
#define BATTERY_ADC_MAX 4095.0f
#define BATTERY_ADC_REF_VOLTAGE 3.3f
// Leituras por amostragem — media reduz o ruido do ADC do ESP32.
#define BATTERY_ADC_SAMPLES 20

// Pinagem do modem A7670E na LilyGO T-A7670E R2, conferida contra o utilities.h
// oficial (Xinyuan-LilyGO/LilyGO-T-A76XX). RX/TX sao do ponto de vista do ESP32
// (MODEM_RX_PIN = pino que o ESP32 usa pra RECEBER, ligado ao TX do modem).
// Valor anterior tinha RX/TX trocados entre si, e faltava o POWERON — motivo
// provavel do modem nunca responder AT (sem alimentacao nenhuma).
// GPIO12 habilita a alimentacao dos perifericos da placa (modem e cartao SD).
// Precisa ficar HIGH logo apos o boot do ESP32 — setado uma unica vez no setup().
// GPIO4 (PWRKEY) e GPIO5 (RESET) passam por transistores NPN inversores (Q2 e Q1
// no esquematico T-A7670X-V1.4): nivel ALTO no ESP32 puxa o pino do modulo pra GND,
// ou seja, HIGH = acionado nos dois.
#define BOARD_POWERON_PIN 12
#define MODEM_PWRKEY_PIN 4
#define MODEM_RESET_PIN 5
#define MODEM_RESET_LEVEL HIGH
#define MODEM_DTR_PIN 25
#define MODEM_RX_PIN 27
#define MODEM_TX_PIN 26
#define MODEM_UART_BAUD 115200
