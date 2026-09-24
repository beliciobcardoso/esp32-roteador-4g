# Onde paramos

Arquivo de continuação entre sessões e entre máquinas. O projeto é trabalhado em mais de um
computador (trabalho e casa), e o que um agente guarda localmente — memória, `git stash`,
`platformio_override.ini`, `sdkconfig` gerado — **não viaja**. Este arquivo viaja com o
código. Quem abre uma sessão nova lê isto logo depois do AGENTS.md.

**Ao encerrar um dia de trabalho, atualize este arquivo** (estado, pendências, próximo passo)
e publique junto com o resto. Os comandos do Claude Code fazem isso: **`/continuar`** ao
chegar, **`/encerrar`** ao sair — definidos em `.claude/commands/`, versionados, iguais nas
duas máquinas. Se ele estiver velho, confie no `git log` e nos docs, e diga ao
usuário que ele estava desatualizado.

Última atualização: 24/09/2026, fim do dia no trabalho.

## Ao abrir numa máquina

1. `git fetch && git switch developer && git pull` — `main` e `developer` ficaram iguais no
   fim de 24/09/2026 (último merge: #32)
2. Porta serial diferente da versionada? `platformio_override.ini` na raiz, ignorado pelo git,
   com `upload_port` e `monitor_port` em `[env:esp-wrover-kit]` (débito 20)
3. `pio test -e native` (221 testes) e `pio run`. Se o build falhar dizendo que o
   `sdkconfig.esp-wrover-kit diverge do sdkconfig.defaults`, é o débito 26 fazendo o trabalho
   dele: `rm -f sdkconfig.esp-wrover-kit && rm -rf .pio && pio run`
4. Relatar o resultado ao usuário e sugerir o próximo trabalho — **sem começar antes de ele
   escolher**

## Estado em 24/09/2026

Fechado no dia (PRs #18 a #32, tudo em `main`):

- Janela silenciosa do descarte PPP: 2 min sem descarte viram uma linha com o zero escrito
- **Débito 13 fechado**: download completo com `CORE_LOCKING`, zero descartes
- **Débito 26 fechado**: build falha se o `sdkconfig` gerado diverge do defaults
- Contador acumulado de descartes PPP em `/api/status` e na página
- Identificação do modem por `AT+SIMCOMATI`: **`A7670E-FASE`**, com GNSS interno
- Versão do firmware acompanha o `git describe` em build incremental
- **Débito 25 fechado**: `pio run -e release` recusa árvore suja
- **Débito 23 fechado**: uma linha `OTA:` por upload no serial; arquivo maior que o slot
  recusado antes de gravar

A placa da bancada do trabalho roda `3072737`.

## Pendências

### Fase 11 — GNSS (PRD 15): pausada, esperando hardware

Falta comprar uma **antena de GNSS ativa**, conector IPEX/U.FL, com cabo que alcance o lado de
fora da janela. O IPEX marcado `GNSS` existe na PCB e estava vazio.

A captura de bancada está na branch **`tmp/captura-gnss`** (commit `1722f0c`, "wip … do not
merge", sobre `86357f4`). **Nunca mergear.** Para usar: branch `tmp/` nova a partir do
`developer` atual e cherry-pick do `1722f0c`; o `developer` andou depois, então pode haver
conflito. Ela segura o PPP por até 15 min no boot e mostra o resultado numa linha temporária
da página, porque a placa vai para a janela na bateria, longe do USB.

Achados de bancada **ainda não registrados no PRD 15** — registrar junto com o primeiro fix:

- `AT+CGNSSPWR=1` aceito; `+CGNSSPWR: READY!` chega em ~10 s. A T-A7670 não usa pino de
  habilitação de GPS (`MODEM_GPS_ENABLE_GPIO = -1` no `utilities.h` da LilyGO)
- O NMEA é 4.1, com campo de signal ID: `$GPGSV,1,1,00,0*65`. Sai na UART com
  `AT+CGNSSNMEA=0,0,0,1,0,0,0,0` (só GSV), `AT+CGNSSPORTSWITCH=0,1` e `AT+CGNSSTST=1`.
  **Desligar com `AT+CGNSSTST=0` antes do modo dados**, ou o NMEA corrompe o PPP
- Na bancada, 0 satélites. Na janela, 17 vistos com SNR máximo de 16 dB e nenhum fix — mas com
  uma antena que **não era de GNSS**, então o número não mede o receptor
- O PPP subiu normalmente com o GNSS ligado. Não é o teste de convivência completo, que pede
  fix e tráfego ao mesmo tempo

Próximo passo com a antena: capturar `+CGNSSINFO` real em modo comando, antes do PPP — ela vira
o caso de teste do parser —, e só depois a migração para CMUX.

### Fase 9 — telemetria MQTT (PRD 14)

Domínio pronto e contador acumulado feito. Falta `infra/mqtt_client` e tudo o que toca rede.

### Débitos abertos

| Débito | Situação |
|---|---|
| 10 — TLS na página, NVS criptografada | depende de decisão do usuário; NVS exige queimar eFuse, permanente |
| 5 — clientes navegando juntos | medir com vários aparelhos |
| 11 — NAPT entre sessões PPP | medir antes de mexer |
| 3, 7, 8 | aceitos, esperando gatilho |
| 24 | é a Fase 10 inteira (PRD 13) |

Resíduo anotado no débito 13: `uart_terminal: Ring Buffer Full` e `HW FIFO Overflow`, uma vez
cada, sob carga pesada (UART do modem a 115200). Não mexer sem recorrência.

## Bancada — o que funcionou

- Gravar: `pio run -t upload`. Monitor: `pio device monitor`, fechado antes de gravar. Porta
  presa: `fuser <porta>` dá o PID
- A linha `cpu_start: App version:` do boot prova qual firmware está na placa
- O usuário valida pelo celular no AP (`http://192.168.10.1`) e manda print ou o JSON do
  `/api/status`. A placa tem bateria e funciona sem USB
- Testes que o usuário faz pelo celular (OTA, página): gravar o serial em arquivo durante o
  teste e filtrar depois (`grep -a "OTA:"`)
- Arquivos de teste de OTA: `docs/TESTE_OTA.md`. Recopiar o `firmware.bin` depois de cada build
