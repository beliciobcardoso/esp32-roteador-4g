#pragma once

// Defaults de fabrica — usados no primeiro boot, quando a NVS ainda nao tem config salva.
#define DEFAULT_AP_SSID "esp32-roteador-4g"
#define DEFAULT_AP_PASSWORD "roteador4g"
// APN da Vivo (MCC 724 / MNC 11), conferido nas definicoes do proprio chip no celular.
// "internet" nao existe nessa rede: em LTE o attach carrega um PDN Connectivity Request
// junto, entao APN desconhecido faz a operadora recusar o attach inteiro (EMM cause #27)
// e o CEREG fica preso em 3 (registro negado), mesmo com sinal cheio.
#define DEFAULT_APN "zap.vivo.com.br"
#define DEFAULT_APN_USER "vivo"
#define DEFAULT_APN_PASSWORD "vivo"
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASSWORD "admin1234"

// Namespace usado na NVS para as chaves de configuracao do roteador.
#define SETTINGS_NVS_NAMESPACE "router_cfg"

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
