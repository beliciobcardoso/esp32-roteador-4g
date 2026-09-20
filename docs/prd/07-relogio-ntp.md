# PRD 07 — Relógio (NTP + fuso configurável)

**Status: implementada em 19/09/2026 — validação em hardware pendente**

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

### `kCurrentSchema` sobe para 3, com degrau de migração

**Revisado em 19/09/2026.** O texto original aceitava descartar a configuração: não havia
migração campo a campo, e reconfigurar uma placa de bancada custava menos que escrevê-la.
Isso mudou duas vezes. Primeiro o provisionamento por unidade (débito 10) tornou o descarte
bem mais caro: `load()` falso faz o `ProvisionSettingsUseCase` **sortear senha nova**, então
o bump derrubaria todos os clientes e a senha nova só existiria no serial. Depois a
migração passou a existir ([PRD 10](10-migracao-de-schema.md)).

O que a Fase 7 precisa fazer, então: subir `kCurrentSchema` para 3 **e** acrescentar o
degrau 2 → 3 em `domain/settings_migration`, que para um campo novo com default seguro é
só preencher `timezone` com o default. Com teste nativo do degrau. Ver
[CONFIGURACAO_NVS.md](../CONFIGURACAO_NVS.md).

## Decidido na implementação

- **Servidores: os dois, `a.st1.ntp.br` primeiro.** E foi preciso mexer no Kconfig:
  `CONFIG_LWIP_SNTP_MAX_SERVERS` vale `1` por padrão, e com ele o segundo
  `esp_sntp_setservername()` é **ignorado sem nenhum erro** — a placa ficaria dependendo só
  do ntp.br achando que tinha fallback. Subiu para `2` no `sdkconfig.defaults`
- **Gatilho no `LinkSupervisor`, ao entrar em `Online`.** Um handler próprio de
  `IP_EVENT_PPP_GOT_IP` desacoplaria mais, mas o supervisor já é o dono desse evento e já
  tem o ponteiro de função para notificar o adaptador HTTP — seria a segunda forma de dizer
  a mesma coisa. O `main.cpp` faz a ponte, como faz para o resto
- **A página diz que não sincronizou, com o motivo.** Nem "sincronizando" (que promete
  chegada) nem nada (que parece página quebrada): `ainda nao sincronizado (precisa do
  uplink 4G)`. Mostrar a epoch formatada seria fingir horário, que é exatamente o que o
  critério de aceite proíbe
- **"Já sincronizou alguma vez" é um latch, não `sntp_get_sync_status()`.** A função volta
  a `SNTP_SYNC_STATUS_RESET` depois que a atualização completa (documentado no `esp_sntp.h`
  instalado), então ela responde "está sincronizando agora?", não "a hora vale?". O latch é
  ligado pelo callback de `sntp_set_time_sync_notification_cb`, e mora fora da classe
  porque a assinatura do callback do lwIP (`void(struct timeval*)`) não carrega contexto
- **O fuso default mora em `domain/timezone.h`, não no `config.h`.** É a primeira entrada da
  tabela por construção (`kOptions[0].posix`): um default fora da tabela faria o `validate()`
  reprovar a própria configuração de fábrica, e sem configuração válida o AP não sobe —
  não sobraria página para corrigir de onde

## Caronas do mesmo degrau de schema

O degrau 2 → 3 fechou junto a metade em aberto do débito 1: `battery_divider_ratio` virou
campo configurável, com faixa validada no domínio e aplicação a quente. Fazer separado
custaria dois degraus, dois testes e dois bumps, e a unidade em campo migraria 1 → 2 → 3 de
qualquer jeito. A faixa `[1.4, 10.0]` sai da física, não de gosto — ver o comentário em
`domain/router_settings.cpp`.

## Riscos conhecidos

- **Drift sem uplink.** O RTC do ESP32 usa RC interno por padrão, que erra minutos por dia.
  Se a T-A7670E tiver cristal de 32,768 kHz (provável — confirmar na placa), cai para
  segundos por dia. Ressincronizar a cada reconexão mascara o problema de qualquer jeito
- **Power-off zera.** O RTC atravessa `esp_restart()` e deep sleep, mas não queda de
  alimentação. Com a bateria ligada, mantém
- **UDP 123 tem que sair pelo NAT.** Sai, mas é o primeiro tráfego originado pelo próprio
  ESP32 (tudo até aqui foi forward de cliente) — pode revelar problema que o NAPT esconde

## Critérios de aceite

- [x] `<select>` de fuso montado da tabela do domínio, e POST com fuso fora dela é recusado
      pelo `validate()` — o formulário não é a defesa
- [x] Degrau 2 → 3 preenche `timezone` e `battery_divider_ratio` sem desfazer nada do
      registro, e encadeia a partir do schema 1 numa passada só
- [x] Sem uplink, o firmware sobe normalmente e a página diz que a hora não está
      sincronizada — nunca finge um horário
- [x] `pio test -e native` cobre a tabela de fusos e a faixa do divisor — 89 testes
- [x] Boot com uplink → hora correta no fuso configurado, confirmado via serial —
      validado em hardware em 20/09/2026
- [ ] Troca de fuso pela página vale a quente, sem reboot
- [ ] Uplink cai e volta → relógio ressincroniza sozinho
- [x] UDP 123 sai pelo NAT — é o primeiro tráfego originado pelo próprio ESP32, e pode
      revelar problema que o NAPT esconde no forward de cliente — validado em hardware em
      20/09/2026

Os dois restantes são de bancada e ficam abertos.

## Validação em hardware — 20/09/2026

A primeira gravação desta versão não subiu: **boot loop**, com o log repetindo

```
assert failed: tcpip_callback IDF/components/lwip/lwip/src/api/tcpip.c:319 (Invalid mbox)
```

O backtrace apontava `Clock::begin()` → `esp_sntp_setoperatingmode()` → `tcpip_callback`.
As funções do `esp_sntp` entram pela task tcpip do lwIP, que só existe depois do
`esp_netif_init()` — e quem chama esse `init` é o `wifiAp.start()`, que rodava *depois* do
`systemClock.begin()`. A ordem escolhida aqui ("antes do `startRouting()`, porque o
supervisor pode entrar em `Online` logo depois de subir") acertou a corrida com o supervisor
e errou a dependência com o lwIP. O defeito não era intermitente: a placa nunca chegava a
subir o AP, então não sobrava nem a página para consertar — só o cabo.

Correção: a chamada passou para dentro do `startRouting()`, depois do AP e antes do
`linkSupervisor.begin()`, o que preserva a garantia original.

A sincronização não deixava rastro no serial — o latch só alimentava a página, e o critério
pedia confirmação por serial. O callback do SNTP agora imprime uma linha por sincronização
(cadência do `CONFIG_LWIP_SNTP_UPDATE_DELAY`, 1 h). Com isso o boot mede:

```
Uplink: online — clientes do AP saem pelo 4G
Relogio: sincronizado — 20/09/2026 07:30:04
```

17 s do reset até a hora certa, conferida contra o relógio do host, no fuso de Brasília.
Fecha também o critério do UDP 123: o SNTP é o primeiro tráfego que a própria placa origina,
e saiu de primeira.

Seguem em aberto a troca de fuso a quente (precisa de cliente na página) e a
ressincronização depois de uma queda de uplink (precisa tirar o SIM ou blindar a antena).
