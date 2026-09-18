# PRD 07 — Relógio (NTP + fuso configurável)

**Status: não iniciada**

Fonte: ideia do usuário, 18/09/2026.

## Objetivo

Dar ao firmware uma noção de hora real. Hoje todo log sai com tick do IDF (`I (56015)`),
que zera a cada boot e não diz nada depois do fato.

É infra base, não feature de ponta: histórico de quedas do uplink, agendamento de reboot,
expiração de sessão e carimbo de OTA dependem todos disso e não têm por onde começar
enquanto não houver relógio.

## Escopo

- `infra/clock.{h,cpp}` — SNTP (`esp_sntp`), sincroniza ao subir o uplink e a cada
  reconexão; expõe hora corrente e se já sincronizou alguma vez
- Campo `timezone` na `RouterSettings`, persistido na NVS
- `<select>` de fuso na página de configuração (4 opções do Brasil), aplicado a quente

## Depende de

PRD 06 (uplink de pé e evento de reconexão, que é o gatilho da sincronização).

## Fora de escopo

Consumidores da hora. Nenhum log, página ou arquivo passa a exibir data nesta fase — o
relógio entra sozinho, e quem usa vem depois. Misturar as duas coisas faria a fase render
um consumidor meia-boca e um relógio amarrado a ele.

## Decisões já tomadas

### Fonte é NTP, não `AT+CCLK?`

O A7670E sabe a hora pelo NITZ da operadora e responderia a `AT+CCLK?` no mesmo trecho em
que o código já roda `CEREG`/`COPS`/`CGDCONT`, antes do modo DATA — funcionaria até sem
PPP. Mas NITZ depende de a operadora entregar, e NTP não depende de operadora nenhuma.

NTP sozinho por ora. NITZ como fallback é abstração antes da segunda ocorrência: entra se
aparecer operadora que não entregue.

### Fuso é `<select>`, não campo de texto

O ESP-IDF não embarca tzdata — só aceita string POSIX TZ (`setenv("TZ", ...)` + `tzset()`).
Expor isso como texto livre é convite a erro de digitação que só aparece semanas depois num
timestamp torto. Quatro opções cobrem o país, e o Brasil não tem horário de verão desde
2019, então nenhuma precisa de regra de transição:

| Rótulo | POSIX TZ |
| --- | --- |
| Brasília (UTC−3) | `<-03>3` |
| Manaus (UTC−4) | `<-04>4` |
| Acre (UTC−5) | `<-05>5` |
| Fernando de Noronha (UTC−2) | `<-02>2` |

### `kCurrentSchema` sobe para 3 e a configuração é descartada

Campo novo na NVS exige subir o schema, e não há migração campo a campo — `load()` volta
aos defaults de fábrica e leva SSID, senhas e APN junto. Aceito: estamos em bancada, e
reconfigurar uma placa custa menos que escrever migração para um dado que ninguém ainda
depende de preservar. Ver [CONFIGURACAO_NVS.md](../CONFIGURACAO_NVS.md).

Reavaliar quando houver unidade em campo.

## A decidir na implementação

- Servidores NTP: `pool.ntp.org` ou `a.st1.ntp.br` (o brasileiro tem RTT menor e é mantido
  pelo NIC.br). Provavelmente os dois, com o nacional primeiro
- Onde fica o gatilho de sincronização: dentro do `LinkSupervisor` ao entrar em `Online`,
  ou um handler próprio de `IP_EVENT_PPP_GOT_IP`. O segundo desacopla, o primeiro é uma
  linha
- O que a página mostra quando ainda não sincronizou — "sincronizando" ou nada

## Riscos conhecidos

- **Drift sem uplink.** O RTC do ESP32 usa RC interno por padrão, que erra minutos por dia.
  Se a T-A7670E tiver cristal de 32,768 kHz (provável — confirmar na placa), cai para
  segundos por dia. Ressincronizar a cada reconexão mascara o problema de qualquer jeito
- **Power-off zera.** O RTC atravessa `esp_restart()` e deep sleep, mas não queda de
  alimentação. Com a bateria ligada, mantém
- **UDP 123 tem que sair pelo NAT.** Sai, mas é o primeiro tráfego originado pelo próprio
  ESP32 (tudo até aqui foi forward de cliente) — pode revelar problema que o NAPT esconde

## Critérios de aceite

- Boot com uplink → hora correta no fuso configurado, confirmado via serial
- Troca de fuso pela página vale a quente, sem reboot
- Uplink cai e volta → relógio ressincroniza sozinho
- Sem uplink, o firmware sobe normalmente e diz que a hora não está sincronizada — nunca
  finge um horário
