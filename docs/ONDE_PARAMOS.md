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

Última atualização: 29/09/2026, madrugada, bancada em casa (depois do merge #87).

## Ao abrir numa máquina

1. `git fetch && git switch developer && git pull` — `main` e `developer` iguais em
   29/09/2026 (última promoção: #87)
2. Porta serial diferente da versionada? `platformio_override.ini` na raiz, ignorado pelo git,
   com `upload_port` e `monitor_port` em `[env:esp-wrover-kit]` (débito 20). O
   `scripts/serial_monitor.py` lê a mesma chave. **Abrir a porta reinicia a placa** (ver
   "Bancada"): não reabrir o monitor no meio de um teste
3. `pio test -e native` (270 testes) e `pio run`. **O #56 mudou o `sdkconfig.defaults`**: na
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
- **Fase 9, PR 1** (#56): convenção de nomes das métricas fixada no PRD 14 (critério 16),
  chaves do payload renomeadas (`router_*`, unidades base) e buffers TLS em 8 KB/2 KB

Fechado em 26/09 (#58 a #70, tudo promovido):

- **Fase 9, PR 2** (#60): configuração do broker em `RouterSettings`, NVS e página — desligada
  de fábrica, senha só de escrita, validação no domínio
- **Porta padrão 443** (#62), não 8883; o servidor divide a 443 com o HTTPS por SNI
- **Flags da telemetria como `0`/`1`** (#63): o parser `json` do Telegraf descarta booleano em
  silêncio, e `router_uplink_reboot_budget_exhausted` teria sumido. Regra corrigida no PRD 14
- **Fase 9, PR 3** (#65): `infra/mqtt_client` e `infra/telemetry_publisher`, validados contra o
  broker de produção. `unit_id` é o código da unidade dado pelo operador, não o MAC
- **Critério 3 fechado** (#68): OTA com amostras no anel, `SW_CPU_RESET`, `anel com 3
  amostra(s) do boot anterior`, publicadas depois. Rodada inteira no PRD 14, "Validação em
  hardware"

Fechado em 27–28/09 (#73 a #75, tudo promovido):

- **Contador de bytes do enlace 4G** (#73): `infra/uplink_byte_counter`, pelo `--wrap` de
  `esp_netif_receive`/`esp_netif_transmit`; telemetria publica `router_uplink_rx_bytes_total` e
  `router_uplink_tx_bytes_total`, e a linha `PPP:` do serial mostra os totais
- **Critério 8 medido** (#74): **~2,0 MiB/dia a 60 s, ~64 MB/mês** (27–28/09), 2,5× a
  estimativa do PRD. "Custo de dado" do PRD 14 corrigido
- **Custo decomposto** (#78, janelas curtas de 10 min): com a telemetria desligada, o envio é
  exatamente o **LCP echo** (`CONFIG_LWIP_LCP_ECHOINTERVAL=3`, 320 B/min), que vai da placa ao
  modem e **não cruza o rádio**. Keepalive do MQTT ~235 B/min de envio, amostra ~374 B. Custo
  cobrado estimado **~35–50 MB/mês a 60 s**; os 64 MB são o teto do lado da placa. A linha
  `PPP:` do serial passou a mostrar bytes exatos

- **Keepalive do MQTT fica em 60 s** (#82, decisão do usuário): o critério 6 continua valendo.
  A dica de custo da página e os comentários de `router_settings` passaram a citar o medido

Fechado em 28–29/09 (#86 e #87, promovido):

- **`HW FIFO Overflow` resolvido** (#86): a causa medida é a escrita na flash (OTA, `otadata`,
  NVS) segurando o ISR da UART do modem, e não CPU nem rede — 69 avisos num OTA de 1,2 MB e
  zero num upload de 4 MiB recusado sem gravar. `CONFIG_UART_ISR_IN_IRAM=y`: depois dela, OTA,
  confirmação e 4,5 h com zero avisos. Custo: ~3 KB de heap interno. Detalhe no débito 13.
  **O #86 mudou o `sdkconfig.defaults`**: primeira compilação em qualquer máquina falha pelo
  débito 26, como deve

A placa da bancada roda **`f8a4f9c`** (o `developer`), com a telemetria ligada como `bancada1`
**a 3600 s** — sobra do teste; voltar pela página ao intervalo desejado. O `App version` do boot
vale como prova de versão. Heap interno com PPP, AP e TLS de pé: livre ~170 KB, mínimo desde o
boot ~156 KB (29/09, com o ISR da UART em IRAM; ~3 KB abaixo de antes). Depois de um OTA, mínimo
de 150 212 B.

## Próximo passo recomendado

**Fechar a Fase 9** ([PRD 14](prd/14-telemetria-mqtt.md)) — faltam só os critérios **2** e **4**,
e sem placa: no Grafana, `router_battery_volts{unit_id="bancada1"}` de **26/09 20:31 a 20:36**,
em escala de minutos — um ponto a cada ~30 s, sem buraco e sem amontoado em 20:35. As 12
amostras drenadas ali incluem 9 colhidas antes do SNTP. Se a conferência não bastar para o 2,
queda completa do 4G por outro método: tirar a antena **não** derrubou o enlace em 26/09.

Custo de dado: decidido e fechado (keepalive 60 s, #82). Número firme de custo cobrado, se um
dia for preciso, pede o consumo do chip no portal da operadora.

Servidor, painéis e alertas: [telemetria-mqtt](https://github.com/beliciobcardoso/telemetria-mqtt).
Cadastro de unidade pelo `registrar-unidade` no container do broker (Terminal do Coolify).
A `bancada1` já está cadastrada; a senha está na NVS da placa da bancada.

**No trabalho (com placa), se a antena de GNSS tiver chegado:** Fase 11 pela captura de
`+CGNSSINFO`. Sem antena, a Fase 10 (acesso remoto, [PRD 13](prd/13-acesso-remoto.md)) é a
próxima fase grande.

## Pendências

### Fase 9 — telemetria MQTT (PRD 14): código pronto, faltam os critérios 2 e 4

**Broker no Coolify, 27/09/2026:** a deploy key `coolify cockpit-basic` do `telemetria-mqtt` foi
**removida** a pedido do usuário, e uma GitHub App (`drab-donkey-…`) foi criada como source no
Coolify. Enquanto o recurso do broker não trocar a source para a App, **todo redeploy falha ao
clonar** — o broker em execução não é afetado. A troca é tarefa da sessão do `telemetria-mqtt`
(prompt entregue ao usuário); conferir que a App tem acesso só ao `telemetria-mqtt`.


Decidido em 25/09/2026, e registrado no PRD 14 (seções "Segurança" e "Convenção de nomes"):

- **O broker existe desde 26/09/2026**, em `mqtt.belloinfo.com.br:443` (repositório
  `telemetria-mqtt`)
- **A credencial do broker é digitada pelo operador**, num campo só de escrita da página. O
  sorteio na placa, do desenho original, contradizia o critério 7
- **TLS pelo pacote de CAs públicas** (`esp_crt_bundle`, já ligado no `sdkconfig`), não por CA
  própria embutida
- Risco anotado no `sdkconfig.defaults`: cadeia de certificados do broker acima de 8 KB derruba
  o handshake


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

### Débitos abertos

| Débito | Situação |
|---|---|
| 10 — TLS na página, NVS criptografada | depende de decisão do usuário; NVS exige queimar eFuse, permanente. Com a Fase 9 a NVS passa a guardar credencial do broker |
| 5 — clientes navegando juntos | medir com vários aparelhos |
| 11 — NAPT entre sessões PPP | medir antes de mexer |
| 3, 7, 8 | aceitos, esperando gatilho |
| 24 | é a Fase 10 inteira (PRD 13) |
| 13 | fechado; o resíduo de `HW FIFO Overflow` foi medido e corrigido em 29/09 (#86) |

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
  boot inteiro. O `pio device monitor` não aceita stdin redirecionado. A porta vem do
  `monitor_port` do override ou do `platformio.ini`
- **Abrir a porta reinicia a placa, mesmo sem `--reset`.** Em 26/09 o script abriu com
  `dtr`/`rts` em `False` e o boot seguinte foi `POWERON_RESET`: o CH340 pulsa o EN na abertura.
  O comentário do script diz o contrário e está errado para este adaptador. Na prática: abrir o
  monitor **antes** do teste e não fechar até o fim
- **Reset pelo EN não conta como reset por software** para o anel da telemetria, e em 26/09 ele
  não apagou o anel (ver PRD 14) — uma ocorrência, não regra
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
