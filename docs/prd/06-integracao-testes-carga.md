# PRD 06 — Integração e Testes de Carga

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 6.

## Objetivo

Validar o roteador sob condições reais de uso: múltiplos clientes, queda de sinal do modem, mudança de config sem reboot manual.

## Escopo

- Testar com até 20 dispositivos WiFi simultâneos navegando através do modem
- Reconexão automática do modem em caso de queda de sinal (retry/backoff — estratégia em aberto, decidir durante implementação da Fase 4/6)
- Definir e validar: salvar config nova reconecta a quente, ou reboot é necessário (e a página avisa o usuário nesse caso)

## Depende de

PRD 01 a 05 completos.

## Em aberto (decidir durante implementação, não bloqueia início das fases anteriores)

- Estratégia de retry/backoff pra reconexão do modem
- Se reboot é exigido após salvar config, e como isso é comunicado na UI
- Payload exato do formulário HTML (campos extras futuros mencionados no plano original)

## Critérios de aceite

- 20 clientes simultâneos conseguem navegar sem falha de DHCP/roteamento
- Queda simulada de sinal do modem → sistema se recupera sozinho ou loga claramente que não se recuperou
- Mudança de config validada end-to-end (reconexão a quente OU aviso explícito de reboot necessário — comportamento documentado, não ambíguo)
