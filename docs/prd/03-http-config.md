# PRD 03 — Servidor de Configuração HTTP

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 3.

## Objetivo

Página web acessível no IP fixo do AP pra editar SSID, senha WiFi, APN e usuário/senha admin, protegida por Basic Auth.

## Escopo

- `adapters/html_page.h` — página HTML embutida (`const char*`), formulário com os campos acima
- `adapters/http_config_handler.{h,cpp}` — rotas GET/POST, HTTP Basic Auth, parsing de formulário, chama `save_settings`/`load_settings`

## Depende de

PRD 01 (usecases de load/save), PRD 02 (AP ativo servindo o IP).

## Fora de escopo

PPP, NAT.

## Critérios de aceite

- Sem Basic Auth válido → 401, sem vazar detalhe interno no corpo da resposta
- GET renderiza formulário com valores atuais (via `load_settings`)
- POST com payload válido → persiste via `save_settings`, confirma persistência após reboot
- POST com payload inválido → rejeitado antes de chegar na NVS, erro claro pro usuário (sem stack trace/detalhe de implementação)
