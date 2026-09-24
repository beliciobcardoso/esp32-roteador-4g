# PRD 02 — WiFi AP

**Status: concluída com ressalvas** (validado em hardware pelo usuário)

⚠️ Revisado na Fase 4: WPA2/WPA3 misto é inalcançável no ESP32 clássico com IDF 4.4 (virou WPA2-PSK), e o limite de clientes entregue é 10, não 20 — ver débito 5 em [DEBITOS_TECNICOS.md](../DEBITOS_TECNICOS.md).

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 2.

## Objetivo

Subir SoftAP com SSID/senha vindos de `load_settings`, IP fixo, pronto pra clientes se conectarem via DHCP.

## Escopo

- `infra/wifi_ap.{h,cpp}` — wrapper fino sobre API de SoftAP do ESP-IDF/Arduino: `start(const RouterSettings&)`, IP fixo (ex: `192.168.10.1`), `WIFI_AUTH_WPA2_WPA3_PSK`, limite de 15 clientes simultâneos (teto do driver)

## Depende de

PRD 01 (settings persistidas/defaults disponíveis).

## Fora de escopo

Servidor HTTP de config, PPP, NAT.

## Critérios de aceite

- Celular conecta no AP e recebe IP por DHCP
- SSID/senha exibidos batem com o que está salvo na NVS (ou default de fábrica, se NVS vazia)
- IP do gateway fixo e acessível
