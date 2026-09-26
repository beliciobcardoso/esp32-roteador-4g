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

Última atualização: 26/09/2026, manhã em casa, sessão rodando na máquina do trabalho (depois do merge #57).

## Ao abrir numa máquina

1. `git fetch && git switch developer && git pull` — `main` e `developer` iguais no código em
   26/09/2026 (última promoção: #57)
2. Porta serial diferente da versionada? `platformio_override.ini` na raiz, ignorado pelo git,
   com `upload_port` e `monitor_port` em `[env:esp-wrover-kit]` (débito 20). O
   `scripts/serial_monitor.py` lê a mesma chave
3. `pio test -e native` (231 testes) e `pio run`. **O #56 mudou o `sdkconfig.defaults`**: na
   primeira compilação depois dele, em qualquer máquina, o build falha pelo débito 26, como
   deve. Rodar o que a mensagem indica: `rm -f sdkconfig.esp-wrover-kit && rm -rf .pio && pio run`
4. Relatar o resultado ao usuário e sugerir o próximo trabalho — **sem começar antes de ele
   escolher**

## Estado em 26/09/2026

Até 25/09 de manhã (#18 a #45, detalhes nos débitos 13, 23, 25, 26 e 27): descarte PPP resolvido
com `CORE_LOCKING`, build que recusa `sdkconfig` divergente e árvore suja, veredito `OTA:` no
serial, modem identificado como **`A7670E-FASE`** (GNSS interno), e o débito 27 — upload
interrompido parava a placa — corrigido com keepalive no socket e `LoopWatchdog`.

Fechado na noite de 25/09 (#46 a #57, tudo promovido):

- **Débito 27 fechado por inteiro.** O `LoopWatchdog` disparou em placa (#46, #48): trava
  artificial aos 120 s, `LoopWatchdog: loop parado ha 30393 ms`, `SW_CPU_RESET` e boot normal.
  E o abort de upload passou a deixar linha `OTA:` (#52), validado por modo avião e por aba
  fechada; o estado do upload é limpo no abort
- **Linha `Bateria:` a cada 30 s** (#50); a leitura segue a cada 3 s, então página e
  `/api/status` continuam atuais. A bancada perdeu o sinal de vida de 3 s — travamento acima
  de 30 s aparece pelo `LoopWatchdog`
- **Hora `HH:MM:SS` nas linhas do projeto** (#54), `--:--:--` antes do SNTP, e
  **`scripts/serial_monitor.py`** com `--log` e `--reset`. Ver "Bancada"
- **Fase 9, PR 1 de 4** (#56): convenção de nomes das métricas fixada no PRD 14 (critério 16),
  chaves do payload renomeadas (`router_*`, unidades base) e buffers TLS em 8 KB/2 KB

A placa fica na bancada do **trabalho** e roda **`e77d718`** — o `developer`, regravado de árvore
limpa em 25/09 às 23:38, uplink online. O `App version` do boot vale como prova de versão.
Heap interno livre com PPP e AP de pé: **212432 B** (25/09), sem TLS aberto — os buffers
menores só aparecem com o cliente MQTT de pé.

## Próximo passo recomendado

**Fase 9, PR 2 — configuração** ([PRD 14](prd/14-telemetria-mqtt.md), "Configuração nova").
Host, porta, usuário, senha, intervalo e liga/desliga em `RouterSettings`, NVS e página;
**desligado de fábrica**; campo novo com default seguro, sem subir `kCurrentSchema`. Prova-se
com `pio test -e native` e `pio run`; ver a página exige a bancada. Não depende do broker.

Depois: **PR 3**, `infra/mqtt_client` e fiação — anel em `RTC_NOINIT`, amostra no `loop()`, LWT
retido, `enqueue` (nunca `publish`), conexão comandada pelo uplink com o backoff do domínio,
`unit_id` do MAC e `roteador/<id>/info` com a versão. **PR 4**, validação contra o broker real.

**No trabalho (com placa), se a antena de GNSS tiver chegado:** Fase 11 pela captura de
`+CGNSSINFO`. Sem antena, medir o `HW FIFO Overflow` durante OTA (ver pendências).

## Pendências

### Fase 9 — telemetria MQTT (PRD 14): PR 1 de 4 feito

Decidido em 25/09/2026, e registrado no PRD 14 (seções "Segurança" e "Convenção de nomes"):

- **Não existe broker ainda.** Os PRs 2 e 3 são escritos e compilados sem ele; o PR 4 espera
- **A credencial do broker é digitada pelo operador**, num campo só de escrita da página. O
  sorteio na placa, do desenho original, contradizia o critério 7
- **TLS pelo pacote de CAs públicas** (`esp_crt_bundle`, já ligado no `sdkconfig`), não por CA
  própria embutida
- Risco anotado no `sdkconfig.defaults`: cadeia de certificados do broker acima de 8 KB derruba
  o handshake

A seção "Estado da implementação" do PRD 14 ainda não cita o #56 — atualizar junto com o PR 2.

### Fase 11 — GNSS (PRD 15): pausada, esperando hardware

Falta comprar uma **antena de GNSS ativa**, conector IPEX/U.FL, com cabo que alcance o lado de
fora da janela. O IPEX marcado `GNSS` existe na PCB e estava vazio.

A captura de bancada está na branch **`tmp/captura-gnss`** (commit `1722f0c`, "wip … do not
merge", sobre `86357f4`). **Nunca mergear.** Para usar: branch `tmp/` nova a partir do
`developer` atual e cherry-pick do `1722f0c`; o `developer` andou muito depois (#54 trocou
todos os `Serial.print*` por `logSerial`), então conta com conflito. Ela segura o PPP por até
15 min no boot e mostra o resultado numa linha temporária da página, porque a placa vai para a
janela na bateria, longe do USB.

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

### `HW FIFO Overflow` durante OTA — achado de 25/09, ainda não no débito 13

O débito 13 anota `uart_terminal: HW FIFO Overflow` como "uma vez, sob carga pesada, não mexer
sem recorrência". **Recorreu**: ~200 linhas em 25 s durante um upload de 4 MiB pela página, em
25/09/2026, parando junto com o upload. Suspeita, não confirmada: as escritas na flash do OTA
seguram a interrupção da UART do modem. Primeiro passo é medir, sem código: um upload válido
com o serial gravado, vendo se os avisos acompanham as escritas. Registrar no débito 13 junto
com a medição.

### Débitos abertos

| Débito | Situação |
|---|---|
| 10 — TLS na página, NVS criptografada | depende de decisão do usuário; NVS exige queimar eFuse, permanente. Com a Fase 9 a NVS passa a guardar credencial do broker |
| 5 — clientes navegando juntos | medir com vários aparelhos |
| 11 — NAPT entre sessões PPP | medir antes de mexer |
| 3, 7, 8 | aceitos, esperando gatilho |
| 24 | é a Fase 10 inteira (PRD 13) |
| 13 | fechado, mas o resíduo de `HW FIFO Overflow` recorreu (acima) |

## Bancada — o que funcionou

- Gravar: `pio run -t upload`. Monitor: `python3 scripts/serial_monitor.py` ou `pio device
  monitor`, **fechado antes de gravar**. Porta presa: `fuser <porta>` dá o PID
- A linha `cpu_start: App version:` do boot prova qual firmware está na placa
- O usuário valida pelo celular no AP (`http://192.168.10.1`) e manda print ou o JSON do
  `/api/status`. A placa tem bateria e funciona sem USB
- Testes que o usuário faz pelo celular (OTA, página): gravar o serial em arquivo durante o
  teste e filtrar depois (`grep -a "OTA:"`). **Antes, confirmar no serial o `wifi:station: …
  join` do celular**: em 25/09 duas rodadas de teste não chegaram à placa porque o celular não
  estava no AP, e o serial só mostrava bateria
- Arquivos de teste de OTA: `docs/TESTE_OTA.md`. Recopiar o `firmware.bin` depois de cada build
- **Gravar o serial em arquivo: `python3 scripts/serial_monitor.py --log <arquivo>`**, e
  `--reset` para reiniciar a placa com a captura já aberta — a única forma de o log pegar o
  boot inteiro. O `pio device monitor` não aceita stdin redirecionado. O script abre a porta
  com `dtr`/`rts` em `False`, que não reinicia a placa, e o `--reset` é um pulso de `rts` de
  200 ms (EN baixo, IO0 alto). A porta vem do `monitor_port` do override ou do `platformio.ini`
- **As linhas do projeto saem com `HH:MM:SS`** (`infra/timestamped_serial`), no fuso
  configurado; antes da primeira sincronização do relógio, `--:--:--`. As do ESP-IDF e do core
  Arduino seguem com o tick delas (`I (56015)`)
- **A barra de progresso do upload não serve de cronômetro.** Ela é o `upload.onprogress` do
  XHR, que mede o buffer do socket do celular, não a rede: um `firmware.bin` de ~1 MB cabe
  quase inteiro nesse buffer e a barra vai a 100% enquanto a placa ainda está recebendo. Para
  cronometrar uma interrupção, arquivo bem maior — 4 MiB funcionou —, e ele precisa **começar
  com `0xE9`**, senão a placa recusa no primeiro bloco e o caminho que se queria medir nem
  roda. O limite do slot (1.900.544 B) cai em 45% de um arquivo de 4 MiB, então cortar abaixo
  disso dá abort puro
