# PRD 04 — Modem PPP

**Status: concluída** (validado em hardware pelo usuário — registro LTE em ~3 s na Vivo, PAP aceito, IP e DNS da operadora atribuídos)

Percalços e diagnósticos descartados: [DEPURACAO_FASE_4_MODEM_PPP.md](../DEPURACAO_FASE_4_MODEM_PPP.md)

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 4.

## Objetivo

Estabelecer sessão PPPoS com o modem A7670E usando `esp_modem` (componente oficial ESP-IDF), expondo uma interface `esp_netif` PPP roteável — não relay de comandos AT (motivo de não usar TinyGSM, ver `PLANO_ROTEADOR.md`).

## Escopo

- `infra/modem_ppp.{h,cpp}` — sequência de power-on do A7670E (PWRKEY/`BOARD_POWERON_PIN`), integração `esp_modem`, sobe PPPoS com o APN salvo (via `load_settings`)

## Depende de

PRD 01 (APN persistido).

## Fora de escopo

NAT/roteamento entre interfaces (fase seguinte), retry/backoff de reconexão (fase 6).

## Critérios de aceite

- Interface PPP recebe IP da operadora, confirmado via log serial
- Falha de power-on ou registro na rede loga erro claro, sem travar o firmware indefinidamente
