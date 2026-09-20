# PRD 01 — Storage (NVS)

**Status: concluída** (commit `d92fb39`)

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 1.

## Objetivo

Persistir e recuperar configurações do roteador (SSID/senha WiFi, APN, usuário/senha admin) na NVS do ESP32, com validação de domínio isolada de infra.

## Escopo

- `domain/router_settings.{h,cpp}` — struct `RouterSettings` + validação de formato (ssid não vazio, senha WiFi ≥ 8 chars, APN/admin_user não vazios, admin_password ≥ 8 chars)
- `adapters/settings_repository.h` — interface abstrata (`load`/`save`)
- `adapters/nvs_settings_repository.{h,cpp}` — implementação concreta sobre NVS (via `Preferences`, já disponível no framework Arduino)
- `usecases/load_settings.{h,cpp}` — lê config salva ou aplica defaults de fábrica (`include/config.h`)
- `usecases/save_settings.{h,cpp}` — valida via domain e persiste via repository
- `include/config.h` — defaults de fábrica (SSID/senha/APN/admin padrão, namespace NVS)

## Fora de escopo

WiFi AP, HTTP, PPP, NAT — tudo isso é fase posterior.

## Critérios de aceite

- Gravar `RouterSettings` e ler de volta via serial (sem rede/HTTP), confirmando persistência entre boots
- `save_settings` rejeita payload inválido (ex: ssid vazio) sem gravar na NVS
- `load_settings` retorna defaults de fábrica quando NVS está vazia (primeiro boot)
- `usecases` não dependem de `nvs_settings_repository.cpp` diretamente — só da interface `settings_repository.h`
