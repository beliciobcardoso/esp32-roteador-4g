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

Última atualização: 25/09/2026, noite no trabalho (depois do merge #45).

## Ao abrir numa máquina

1. `git fetch && git switch developer && git pull` — `main` e `developer` ficaram iguais em
   25/09/2026 no código (última promoção: #44; o #45 só mexeu neste arquivo)
2. Porta serial diferente da versionada? `platformio_override.ini` na raiz, ignorado pelo git,
   com `upload_port` e `monitor_port` em `[env:esp-wrover-kit]` (débito 20)
3. `pio test -e native` (229 testes) e `pio run`. Se o build falhar dizendo que o
   `sdkconfig.esp-wrover-kit diverge do sdkconfig.defaults`, é o débito 26 fazendo o trabalho
   dele: `rm -f sdkconfig.esp-wrover-kit && rm -rf .pio && pio run`
4. Relatar o resultado ao usuário e sugerir o próximo trabalho — **sem começar antes de ele
   escolher**

## Estado em 25/09/2026

Fechado em 24/09 (PRs #18 a #36, tudo em `main`):

- Janela silenciosa do descarte PPP: 2 min sem descarte viram uma linha com o zero escrito
- **Débito 13 fechado**: download completo com `CORE_LOCKING`, zero descartes
- **Débito 26 fechado**: build falha se o `sdkconfig` gerado diverge do defaults
- Contador acumulado de descartes PPP em `/api/status` e na página
- Identificação do modem por `AT+SIMCOMATI`: **`A7670E-FASE`**, com GNSS interno
- Versão do firmware acompanha o `git describe` em build incremental
- **Débito 25 fechado**: `pio run -e release` recusa árvore suja
- **Débito 23 fechado**: uma linha `OTA:` por upload no serial; arquivo maior que o slot
  recusado antes de gravar

Fechado na virada de 24 para 25/09 (#41 e #43, promovidos em #42 e #44):

- **Débito 27 aberto, corrigido e validado no mesmo ciclo.** Subir firmware pela página e sair
  de alcance antes do fim **parava a placa**, e ela não voltava sozinha — medido em 137 s de
  serial mudo, que não terminaram nem quando o celular voltou. A causa é o
  `WebServer::_uploadReadByte` do core, que espera em `while(!client.available() &&
  client.connected()) delay(2);`: desligar o Wi-Fi não manda FIN, e sem keepalive
  `connected()` nunca vira falso. Corrigido em duas camadas — keepalive no socket do upload
  (~11 s) ataca a causa, `infra/loop_watchdog` + `domain/loop_health` (30 s) é a rede de
  segurança. Na validação, 348 s com o cliente fora e a placa seguiu viva, 113 batidas de
  bateria, sem reiniciar. Quem agiu foi o keepalive
- **O `AGENTS.md` afirmava uma proteção que não existia.** `CONFIG_ESP_TASK_WDT_PANIC=y` não
  cobre o `loop()`: o core deixa `loopTaskWDTEnabled = false` e o projeto nunca chamou
  `enableLoopWDT()`. A frase foi corrigida
- **O critério de bancada aberto da Fase 8 foi respondido** — era exatamente o upload
  interrompido. E o desfecho "não confirmar" do OTA foi exercitado de verdade no caminho: uma
  imagem antiga subiu por engano, ninguém confirmou, e o rollback do bootloader devolveu a
  placa ao slot anterior sozinho, como documentado
- **O `/continuar` rodou pela primeira vez numa sessão nova** e funcionou fim a fim
- Este arquivo (#43, promovido em #44) e a placa regravada a partir dele. As branches do
  ciclo foram apagadas; sobrou só a `tmp/captura-gnss`, que é para durar

Fechado na noite de 25/09:

- **O `LoopWatchdog` disparou em placa.** Firmware descartável `a31eb2c` travou o `loop()` em
  `for (;;) delay(2);` aos 120 s; a linha `LoopWatchdog: loop parado ha 30393 ms` saiu,
  o reset veio como `SW_CPU_RESET` e o boot seguinte rodou normal, uplink online. Detalhes no
  débito 27. A branch `tmp/prova-loop-watchdog` ficou **só local**, na máquina do trabalho

A placa fica na bancada do **trabalho** e roda **`c7ec57e`** — o `developer`, mesmo código do
`00d8844` (a diferença é só este arquivo), regravado de árvore limpa em 25/09 depois da prova,
com uplink online. O `App version` do boot vale como prova de versão. Em casa, sem placa, só vale trabalho que se prove
com `pio test -e native` e `pio run`.

## Próximo passo recomendado

**Em casa (sem placa): Fase 9, `infra/mqtt_client`** ([PRD 14](prd/14-telemetria-mqtt.md)). O
domínio já está pronto e testado (`domain/telemetry`, `telemetry_buffer`, `mqtt_backoff`), o
contador acumulado de descartes e a identificação do modem já existem para irem no payload, e
a fase destrava a publicação da posição da Fase 11. Dá para escrever e compilar sem placa;
validar exige a bancada e o broker. Referência colhida em 24/09 para o orçamento de memória:
heap interno livre em ~206–210 KB com PPP e AP de pé.

**No trabalho (com placa):** se a antena de GNSS tiver chegado, retomar a Fase 11 pela
captura de `+CGNSSINFO` — destrava uma fase inteira. Sem antena, a linha `OTA:` que falta no
abort do upload (ressalva do débito 27): pequena, e só se valida abortando um upload de
verdade na bancada.

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

### Débito 27 — o que ficou aberto depois da correção

- ~~O `LoopWatchdog` nunca disparou em placa~~ — provado em 25/09/2026 (ver débito 27)
- **No abort não sai linha `OTA:`.** `_parseForm` devolve `false` e o `_handleRequest()` nem é
  chamado, então `handleUpdateDone()` não roda. É um furo no contrato do débito 23 — hoje
  cosmético, porque o caminho se recupera, mas real

### Débitos abertos

| Débito | Situação |
|---|---|
| 10 — TLS na página, NVS criptografada | depende de decisão do usuário; NVS exige queimar eFuse, permanente |
| 5 — clientes navegando juntos | medir com vários aparelhos |
| 11 — NAPT entre sessões PPP | medir antes de mexer |
| 3, 7, 8 | aceitos, esperando gatilho |
| 24 | é a Fase 10 inteira (PRD 13) |
| 27 | corrigido e validado, watchdog incluso; resta a linha `OTA:` do abort |

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
- **`pio device monitor` não aceita stdin redirecionado** — para gravar o serial em arquivo,
  falar direto com a porta por pyserial. Abrir a porta com `dtr`/`rts` em `False` não reinicia
  a placa; para reiniciar de propósito, pulso de `rts = True` por 200 ms com `dtr` em `False`
  (EN baixo, IO0 alto). Anexar a captura **antes** do pulso é a única forma de o log pegar o
  boot inteiro
- **A barra de progresso do upload não serve de cronômetro.** Ela é o `upload.onprogress` do
  XHR, que mede o buffer do socket do celular, não a rede: um `firmware.bin` de ~1 MB cabe
  quase inteiro nesse buffer e a barra vai a 100% enquanto a placa ainda está recebendo. Para
  cronometrar uma interrupção, arquivo bem maior — 4 MiB funcionou —, e ele precisa **começar
  com `0xE9`**, senão a placa recusa no primeiro bloco e o caminho que se queria medir nem
  roda. O limite do slot (1.900.544 B) cai em 45% de um arquivo de 4 MiB, então cortar abaixo
  disso dá abort puro
