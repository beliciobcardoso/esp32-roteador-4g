# PRD 02 — WiFi AP

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 2.

## Objetivo

Subir SoftAP com SSID/senha vindos de `load_settings`, IP fixo, pronto pra clientes se conectarem via DHCP.

## Escopo

- `infra/wifi_ap.{h,cpp}` — wrapper fino sobre API de SoftAP do ESP-IDF/Arduino: `start(const RouterSettings&)`, IP fixo (ex: `192.168.4.1`), `WIFI_AUTH_WPA2_WPA3_PSK`, limite de 20 clientes simultâneos

## Depende de

PRD 01 (settings persistidas/defaults disponíveis).

## Fora de escopo

Servidor HTTP de config, PPP, NAT.

## Critérios de aceite

- Celular conecta no AP e recebe IP por DHCP
- SSID/senha exibidos batem com o que está salvo na NVS (ou default de fábrica, se NVS vazia)
- IP do gateway fixo e acessível
