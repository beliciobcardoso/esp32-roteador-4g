# PRD 05 — NAT / Roteamento

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 5.

## Objetivo

Rotear tráfego dos clientes WiFi (AP) pra internet através da interface PPP, via NAPT (já habilitado em `sdkconfig.defaults`: `LWIP_IPV4_NAPT`, `LWIP_IP_FORWARD`, `LWIP_NAPT`).

## Escopo

- `infra/nat_bridge.{h,cpp}` — habilita NAPT entre `esp_netif` da AP e `esp_netif` do PPP
- `usecases/start_routing.{h,cpp}` — orquestra ordem: settings → AP → modem → NAT

## Depende de

PRD 01, 02, 04.

## Fora de escopo

Testes de carga com múltiplos clientes, reconexão automática (fase 6).

## Critérios de aceite

- Celular conectado no AP navega na internet através do modem 4G
- Ordem de subida (`start_routing`) respeitada: settings antes de AP, AP antes de modem, modem antes de NAT
- Falha em qualquer etapa da orquestração não deixa o sistema em estado parcial silencioso (loga em qual etapa falhou)
