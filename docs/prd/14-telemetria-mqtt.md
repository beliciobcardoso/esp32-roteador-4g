# PRD 14 — Telemetria MQTT e painéis no Grafana

**Status: proposto** — não implementado.

Fonte: conversa de 23/09/2026. Nasce da mesma restrição do
[PRD 13](13-acesso-remoto.md) — CGNAT — mas resolve um pedaço diferente dela, e por um
caminho muito mais barato.

## Objetivo

A unidade em campo publica o próprio estado num broker MQTT, e esse estado vira série
temporal **guardada por anos** e painel no Grafana. Quem pergunta "como está a unidade 3?"
responde de um navegador, sem visita e sem estar no Wi-Fi dela — e quem pergunta "como ela
estava em março?" também.

Dois objetivos, e é o segundo que define o desenho:

1. **Ver o estado agora**, sem visita. É o que paga a fase no primeiro dia.
2. **Guardar o histórico por muito tempo**, para responder pergunta que ainda não foi feita.
   Defeito intermitente, bateria que degrada em seis meses, enlace que piora depois de uma
   mudança da operadora — nenhuma dessas aparece num gráfico de 15 dias.

E uma direção declarada desde já, que não é objetivo desta fase mas restringe as decisões
dela:

- **A placa vai ganhar sensores externos.** Temperatura, corrente, porta, o que o caso pedir
  — grandezas que o dispositivo não mede sozinho hoje. O canal, o formato e o armazenamento
  têm que aceitar isso sem refazer nada. O que muda em consequência está em
  "[Sensores externos](#sensores-externos-a-direção-declarada)".

O nome da fase é "telemetria do roteador", mas o que está sendo construído é o **caminho de
dado da unidade**. O roteador é só o primeiro a usá-lo.

## Por que esta fase vem antes do PRD 13

As duas contornam o CGNAT pelo mesmo princípio — a conexão nasce na placa. O custo é que
difere:

- O túnel WireGuard do PRD 13 põe **uma terceira interface** no mesmo lwIP que hoje faz
  NAT dos clientes do AP. O próprio PRD 13 lista isso como "risco técnico número um",
  junto com MTU e heap.
- MQTT é **um socket TCP de saída**. Não entra no roteamento, não muda MTU de ninguém,
  não tem `AllowedIPs` para errar.

E cobre o chamado mais frequente. Ler status é o que se quer quase sempre; abrir a página
e subir firmware é o que se quer de vez em quando. Esta fase não substitui o PRD 13 —
telemetria não sobe `firmware.bin` nem corrige um APN. Ela reduz o que sobra para o túnel,
e entrega antes.

## A decisão

**Publicação MQTT sobre TLS, só de subida, contra broker próprio.** Ingestão por Telegraf
(`inputs.mqtt_consumer`) e escrita por `remote_write` no Prometheus já existente.

### O lado do servidor está pronto — e não é o assunto desta fase

Grafana e Prometheus rodam em produção, com retenção acima de 365 dias, teto de 100 GB e
backup. Nada disso é escopo aqui, e o dimensionamento não preocupa: a frota do piloto gera
~60 MB/ano, e cem unidades com sensores gerariam ~2,8 GB/ano — ruído dentro do teto que já
existe.

Sobram **duas chaves do Prometheus** a conferir antes da primeira amostra, porque nenhuma
delas vem de graça com a retenção:

```bash
curl -s localhost:9090/api/v1/status/flags  | grep remote-write-receiver
curl -s localhost:9090/api/v1/status/config | grep -i out_of_order
```

- **`--web.enable-remote-write-receiver`** (2.33+) — sem ele o Telegraf não tem onde escrever
- **`out_of_order_time_window`** em `storage.tsdb` (2.39+), dimensionado para cobrir a maior
  janela de buffer da placa (**12 h** basta) — sem ele, a amostra da unidade que ficou offline
  é aceita pela rede e descartada na gravação, **sem erro dos dois lados**

Versão anterior à 2.39 derruba só a segunda, e a saída é VictoriaMetrics single-node como
destino do `remote_write`, sem trocar o datasource do Grafana.

Duas decisões de ingestão que ficam registradas só para não serem reabertas: o **datasource
MQTT do Grafana** foi recusado (é streaming ao vivo, sem histórico — não responde "caiu de
madrugada?"), e o caminho é **`remote_write` e não exporter com scrape** (`mqtt2prometheus` e
`outputs.prometheus_client` carimbam a hora do scrape, não a da amostra, e matam o backfill).

### O firmware não conhece o banco

A placa publica MQTT e nada mais. Trocar o backend é mudança de uma linha no Telegraf, sem
tocar em firmware e sem visita a unidade nenhuma. É o que mantém o resto deste PRD — que é
sobre o **dispositivo** — independente do que roda do outro lado.

## O problema central: a métrica que importa é a que não pode ser enviada

Uplink caído é o evento que se quer ver, e é exatamente quando não há como publicar. Três
mecanismos, e os três são necessários — nenhum cobre o outro:

1. **LWT retido.** `roteador/<id>/status` com Last Will `offline`, retido. O broker publica
   por conta própria quando o TCP morre. Não é instantâneo: depende do keepalive.
2. **Buffer local com timestamp**, drenado na reconexão. Sem ele a queda vira buraco no
   gráfico em vez de evento com começo e fim.
3. **Ausência como sinal.** Alerta por `absent()`/`up == 0` cobre o caso em que a placa
   morreu de um jeito que nem LWT nem buffer alcançam — bateria acabou, travou antes do
   WDT, alguém desligou.

### Onde o buffer mora

`RTC_NOINIT_ATTR`, como já faz o contador de reboot do `LinkSupervisor`. Sobrevive a reset e
ao reboot do supervisor, que é o caso comum; não sobrevive a queda de alimentação, que é o
caso em que a própria placa não estava medindo nada mesmo.

Orçamento: ~4 KB dos 8 KB de RTC slow RAM, em struct binária de **20 B** — cerca de
**204 amostras**, ou ~3,4 h a 60 s de intervalo. Descarte é do mais velho: amostra recente
vale mais que amostra de três horas atrás.

```c
struct TelemetrySample {        // 20 bytes
  uint32_t ts;                  // epoch, ou millis/1000 se ainda sem relogio
  uint32_t ppp_drops_total;     // cumulativo desde o boot — uint16 estoura em minutos
  uint16_t battery_mv;
  uint16_t free_heap_kb;
  uint16_t uptime_min;
  uint8_t  uplink_state;
  uint8_t  flags;               // bit0 sem relogio, bit1 rebooted, bit2 exhausted
  uint32_t reserved;            // um campo futuro sem mudar o tamanho nem a versao
};

struct TelemetryRing {
  uint32_t magic;               // 'TLM1' — pega RAM aleatoria de power-on
  uint16_t layout;              // sobe quando a struct muda — mudanca intencional
  uint16_t sample_size;         // sizeof(TelemetrySample) — quem esqueceu de subir layout
  uint16_t head, count;
  TelemetrySample samples[kCapacity];
};
```

**As três guardas do cabeçalho não são redundantes.** `magic` cobre RTC RAM com lixo de
power-on; `layout` cobre mudança deliberada da struct; `sample_size` cobre o erro realista —
alguém acrescenta um campo e esquece de subir a versão. Qualquer uma falhando, o anel é
zerado no boot. O `reserved` existe para que o primeiro campo novo não custe nem bump nem
buffer, e tem 4 bytes e não 2 por um motivo mecânico: com 2 o compilador acrescentaria
preenchimento invisível no fim para alinhar a struct em 4, e a primeira adição gastaria
buffer mesmo assim. Explícito, o espaço é utilizável.

**A partição `spiffs` foi considerada e recusada nesta fase.** Ela está parada desde a Fase 1
(débito 7) e é o candidato óbvio para sobreviver à queda de energia, mas telemetria periódica
em flash é escrita contínua num setor pequeno — desgaste em troca de cobrir o caso que o
buffer em RTC já não precisa cobrir. Fica registrado para quando houver requisito de
sobreviver a bateria zerada.

### Timestamp antes do primeiro SNTP

Amostra colhida antes da primeira sincronização não tem hora. Gravar `0` contaminaria a série
com dados em 1970; descartar perderia justamente os primeiros minutos de uma placa que
reiniciou.

A regra é: gravar o instante monotônico e uma marca de "sem relógio", e **converter no
flush**, quando já há hora — `ts_real = agora_epoch - (agora_ms - amostra_ms) / 1000`. É
aritmética pura, mora em `domain/`, e tem teste nativo. O latch de "já sincronizou alguma
vez" da Fase 7 é a entrada que decide qual caminho tomar.

## Identidade, tópicos e payload

Tópicos:

```
roteador/<unit_id>/tel      # telemetria, QoS 0, sem retain
roteador/<unit_id>/status   # online/offline, QoS 1, retido, é o LWT
```

`unit_id` derivado do MAC. **Isso não contradiz o PRD 08**, que rejeitou derivar a *senha* do
MAC: o MAC é o BSSID e vai em todo beacon, o que o desqualifica como segredo e o qualifica
como identificador estável.

Payload JSON, montado com `domain/json` — o mesmo que já serve o `/api/status`, e pelo mesmo
motivo de existir: escape correto sem arrastar um alocador de documento para um objeto plano
de campos conhecidos. Campo `ts` explícito em cada amostra, sempre, inclusive na que é publicada na hora —
é o que faz amostra ao vivo e amostra do buffer terem o mesmo formato, em vez de dois
caminhos de código.

## O que é publicado

Tudo já existe no firmware. Nenhuma medição nova precisa ser inventada:

Os nomes abaixo são os finais, pela [convenção de nomes](#convenção-de-nomes) fixada em
25/09/2026. Os da primeira linha de cada grupo já saem do `buildTelemetryPayload()`; os
demais entram com o `infra/mqtt_client`.

| Campo | Fonte | Tipo no Prometheus |
|---|---|---|
| `router_battery_volts`, `router_battery_charge_ratio` | `BatteryAdc` + `domain/battery` | gauge |
| `router_uplink_state` | `LinkSupervisor::status()` | gauge (enum numérico) |
| `router_uplink_failures` | idem | gauge |
| `router_uplink_rebooted`, `router_uplink_reboot_budget_exhausted` | idem | gauge 0/1 |
| `router_ppp_drops_total` | `infra/ppp_drop_counter` | **counter** |
| `router_heap_internal_free_bytes`, `router_heap_internal_min_free_bytes` | `esp_heap_caps` | gauge |
| `router_uptime_seconds`, `router_reset_reason` | `esp_system` | gauge |
| `router_ap_clients` | `WifiAp` | gauge |
| `router_build_info{fw_version, image_state}` | `domain/firmware_update` | info, em tópico próprio |

Três observações que mudam código ou schema:

- **`ppp_drops` precisa virar cumulativo desde o boot.** Hoje o `PppDropCounter` conta por
  janela de 30 s e zera — é o que a linha de serial do débito 13 precisa. Contador de janela
  publicado como counter quebra `rate()`. A saída é o contador guardar também o acumulado; o
  relato no serial continua usando a janela. O Prometheus já sabe tratar a volta a zero de um
  reboot.
  **Esta é a medição que o débito 13 pede há duas fases**, e que hoje só se obtém com
  `grep -avE` num monitor serial ligado na placa. Em painel, ela passa a existir para a frota
  inteira e ao longo do tempo.
- **`fw_version` não entra como label das outras métricas.** Um OTA criaria série nova para
  cada métrica de cada unidade. Vai num `router_build_info{unit_id, fw_version} 1` separado,
  que é o padrão para isto.
- **`reboot_budget_exhausted` é o alerta mais importante da fase.** É o único estado em que
  esperar não resolve nada — a frase já mora em `domain/uplink_status` e diz isso. Hoje
  ninguém fica sabendo sem estar em frente à placa.

**RSSI/CSQ fica fora.** O modem está em modo dados PPP; `AT+CSQ` exige CMUX ou sair para modo
comando, ou seja, arriscar derrubar o enlace para medir o enlace. É investigação própria, com
risco próprio, e não cabe carregar na primeira fase de telemetria.

### Convenção de nomes

Fixada e revisada em **25/09/2026**, antes da primeira série — critério 16. É a única
decisão desta fase sem conserto barato depois: renomear métrica quebra painel e histórico
ao mesmo tempo.

**A chave do JSON é o nome final da métrica.** O serializer de `remote_write` do Telegraf
monta o nome como `<measurement>_<campo>`, com uma exceção documentada: measurement chamado
`prometheus` usa o campo como está (`plugins/serializers/prometheus/convert.go`, função
`MetricName`). O `mqtt_consumer` fica com `name_override = "prometheus"`, e a chave
`router_battery_volts` vira a métrica `router_battery_volts`. Sem isso ela sairia
`mqtt_consumer_router_battery_volts`, e `sensor_*` precisaria de configuração própria no
servidor — o contrário do critério 13.

Regras:

- **Prefixo da família:** `router_` para o diagnóstico da placa, `sensor_` para grandeza do
  ambiente, com o sensor no nome (`sensor_temperature_celsius`)
- **Unidade base no sufixo:** `_volts`, `_bytes`, `_seconds`, `_celsius`. Carga e fração em
  **razão 0–1** (`_ratio`), não porcentagem
- **`_total` só em counter.** Hoje é um só: `router_ppp_drops_total`
- **Booleano sai como `true`/`false`** e o Telegraf grava 1/0. Texto é descartado pelo
  serializer — nenhum campo de texto no payload de telemetria
- **`ts` não é métrica:** é o `json_time_key`, em segundos de epoch

Três decisões de identificação que saem das regras:

- **`unit_id` vem do tópico, não do payload** (`topic_parsing` do `mqtt_consumer`). A ACL
  do broker garante o tópico, não o conteúdo (critério 10): um `unit_id` no JSON poderia
  mentir, o do tópico não
- **A versão de firmware tem tópico próprio**, `roteador/<unit_id>/info`, retido e publicado
  a cada conexão: `router_build_info = 1` com `fw_version` e `image_state` como tags. Campo
  de texto no payload de telemetria só sobreviveria ao Telegraf como tag, e aí viraria
  label de todas as métricas — um OTA criaria série nova para cada uma
- **O anel guarda em unidade compacta, o payload publica em unidade base.** Heap em KB e
  uptime em minutos cabem nos 20 B da amostra; `buildTelemetryPayload()` converte para
  bytes e segundos na saída, e é coberto por teste nativo. A resolução do uptime continua
  de um minuto

A configuração do servidor que a convenção pressupõe:

```toml
[[inputs.mqtt_consumer]]
  topics = ["roteador/+/tel"]
  data_format = "json"
  json_time_key = "ts"
  json_time_format = "unix"
  name_override = "prometheus"
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "roteador/+/tel"
    tags = "_/unit_id/_"
```

### A lista é aberta — e o que isso custa em cada camada

Esta tabela vai crescer. Campo novo vai aparecer porque alguém quer investigar algo que hoje
não se enxerga, e o desenho tem que aceitar isso **sem reescrever a fase**. O requisito não é
"deixar genérico": é saber o que quebra ao adicionar, camada por camada.

**Servidor: não quebra, com duas condições.** Métrica nova vira série nova sozinha, sem
schema e sem migração — desde que a convenção de nome esteja fixada no primeiro campo
(`router_` para a placa, `sensor_` para o ambiente, unidade no sufixo, `_total` só em counter)
e que o Telegraf use `data_format = "json"` com `json_time_key = "ts"`, `name_override =
"prometheus"` e o `unit_id` tirado do tópico — ver [Convenção de nomes](#convenção-de-nomes). Com o parser `json_v2` enumerando nomes, campo novo é **descartado em
silêncio** e a adição parece não ter funcionado, sem erro dos dois lados. Duas linhas de
configuração, uma vez, e o servidor sai do caminho.

**Payload JSON: aditivo por natureza.** `domain/json` já monta campo a campo; adicionar é uma
linha no builder e uma no teste. Nenhuma abstração de registro de métricas: um mapa de
ponteiros de função para resolver o que hoje é uma linha de `body.number(...)` é abstração
especulativa, custa indireção e heap, e o AGENTS.md já proíbe criar antes de duas ocorrências
reais.

**O anel em RTC RAM: este é o que quebra de verdade.** A amostra é struct binária, e é o
único formato aqui que não é autodescritivo. Dois efeitos, os dois reais:

- **Depois de um OTA, o firmware novo lê buffer escrito pelo antigo.** `sizeof` diferente
  significa ler lixo e publicar uma série inteira de valores inventados — pior que perder o
  buffer, porque dado falso vira decisão. Exige o **cabeçalho versionado** descrito acima, e
  descarte do conteúdo quando não bate. Custo: a atualização perde o histórico em buffer, uma
  vez, o que é aceitável.
- **Campo novo encurta a janela de backfill.** 4 KB a 20 B/amostra são ~204 amostras (~3,4 h
  a 60 s); a 32 B caem para ~128 (~2 h). O orçamento é fixo — RTC slow RAM não cresce. O
  campo `reserved` absorve a primeira adição sem custo nenhum.

Daí a separação que o desenho adota:

| | o que carrega | quem decide |
|---|---|---|
| **Amostra em buffer** | núcleo fechado: `ts`, bateria, estado do uplink, `ppp_drops`, heap | muda só com bump de versão do anel, e é decisão consciente |
| **Payload publicado ao vivo** | núcleo **+** tudo o mais que for útil | aberto, campo entra sem cerimônia |

O backfill responde "o que aconteceu enquanto esteve fora", não "tudo com a mesma riqueza de
quando estava online". Aceitar essa assimetria é o que mantém a lista aberta sem espremer a
janela de buffer a cada campo novo.

**A regra que sobra para cada campo futuro:** todo campo publicado por intervalo custa dado em
toda unidade, para sempre. Campo que só muda em evento vai por mudança de estado, não no
intervalo — e campo que ninguém olhou em três meses sai da lista tão facilmente quanto entrou.

## Sensores externos: a direção declarada

Não é escopo desta fase. É restrição dela. Sensor externo mede o **lugar**, não a placa — e
isso muda cinco decisões que, tomadas erradas agora, custam caro depois de dois anos de
histórico gravado.

### 1. O prefixo `router_` deixa de servir

Temperatura da sala não é métrica do roteador. Misturar as duas famílias num prefixo só torna
impossível responder "o que esta unidade mede sobre si" separado de "o que ela mede sobre o
ambiente" — e é essa separação que decide se um alerta acorda alguém.

Fica decidido agora, antes da primeira série existir:

- `router_*` — diagnóstico da placa: bateria, uplink, heap, uptime, `ppp_drops`
- `sensor_*` — grandeza do ambiente, com o sensor no nome (`sensor_temperature_celsius`)

Renomear métrica depois quebra painel e histórico ao mesmo tempo. Este parágrafo existe para
que ninguém precise fazer isso.

### 2. O backfill do sensor importa mais que o do roteador

Aqui a premissa da seção anterior se inverte, e vale registrar a inversão. Heap livre de três
horas atrás é curiosidade. **Temperatura de três horas atrás é o dado**, e um registro
ambiental com buracos nas horas em que o 4G caiu vale bem menos que um registro contínuo.

Consequência: quando os sensores entrarem, o núcleo do anel em RTC RAM deixa de ser "o que
diagnostica a placa" e passa a ser "o que não pode ter buraco". Provavelmente troca campo de
diagnóstico por leitura de sensor dentro do mesmo orçamento de 4 KB — e é por isso que o anel
já nasce com **cabeçalho versionado**, em vez de ganhar um depois.

E é aqui que a partição `spiffs` recusada acima (débito 7) volta a fazer sentido: registro
ambiental que precisa sobreviver a bateria zerada é requisito diferente do de agora.

### 3. Payload continua plano

Sensores são o candidato natural a pedir aninhamento ou array no JSON — `domain/json` hoje só
faz objeto plano, e diz no próprio comentário que aninhamento entra "quando houver o segundo
caso que o peça".

**Não é o caso.** Um objeto plano com `sensor_temperature_celsius` ao lado de
`battery_volts` carrega N sensores sem estrutura nova, e é justamente o formato que o parser
genérico do Telegraf repassa sem conhecer os nomes. Aninhar obrigaria a configurar o servidor
a cada sensor novo — o contrário do que a seção anterior fixou.

### 4. Unidades deixam de ser idênticas

Hoje toda unidade publica o mesmo conjunto de campos. Com sensores, a unidade 3 tem um sensor
de temperatura e a unidade 7 não. Duas saídas, e a escolha é para a fase dos sensores:

- **Detecção em tempo de execução** — a placa varre o barramento no boot e publica o que
  achou. Sem configuração, mas esconde sensor com defeito: ele some do painel em vez de
  alarmar
- **Declaração na configuração** — a página diz quais sensores a unidade tem, e ausência de
  leitura vira falha visível

A segunda é a correta pelo mesmo princípio do resto do projeto: **ausência silenciosa não é
estado aceitável**. Fica registrada aqui para não ser redecidida do zero.

### 5. Pinos e ADC — a armadilha concreta

Duas restrições de hardware que eliminam opções antes de qualquer escolha de sensor:

- **ADC2 não funciona com o Wi-Fi ligado no ESP32.** O driver do rádio toma o periférico, e a
  leitura passa a falhar ou devolver lixo. Como o AP nunca desliga nesta placa, sensor
  analógico só em **ADC1** — GPIO 32–39, dos quais 34/36/39 são só entrada (o que serve bem
  para analógico). GPIO35 já é a bateria e GPIO32 é o LED de teste
- **O orçamento de pinos é pequeno e já comprometido.** Modem ocupa 4, 5, 12, 25, 26, 27, e o
  GPIO12 é pino de strapping da placa (`BOARD_POWERON`) que ainda alimenta o cartão SD.
  Levantar o que sobra contra o esquemático da LilyGO **antes** de prometer barramento I²C ou
  1-Wire

Nada disso é escopo desta fase. Está aqui porque a fase escolhe nomes, formato e
armazenamento que vão durar mais que ela.

## Segurança

1. **Só de subida.** A placa publica e não assina nada. Broker comprometido vê dados de
   bateria e de enlace; não manda comando, porque não há caminho para comando. É a diferença
   entre um incidente chato e um incidente que alcança a frota.
2. **TLS com verificação do servidor.** `esp-mqtt` sobre `esp-tls`/mbedTLS. Sem isso a
   senha do broker trafega em claro pela internet — e essa senha, num broker mal segregado,
   vale para todas as unidades.
   **Decidido em 25/09/2026: a confiança vem do pacote de CAs públicas do mbedTLS**
   (`esp_crt_bundle`, `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, já ligado no `sdkconfig`), e
   não de um PEM de CA própria embutido. Serve a qualquer broker com certificado de CA
   pública (Let's Encrypt), e a renovação do certificado do servidor não exige regravar a
   frota. O custo é confiar em qualquer CA do pacote, que é o mesmo modelo de um navegador
3. **Credencial por unidade, nunca em código versionado.**
   **Decidido em 25/09/2026: o operador cadastra a credencial no broker e a digita na
   página**, num campo só de escrita — a página mostra "definida", nunca o valor. O desenho
   original, sorteio no primeiro boot por `infra/entropy` como as senhas do AP e de admin
   (PRD 08), esbarrava no critério 7: se a senha não pode aparecer em log, serial nem
   página, o operador não tem como lê-la para cadastrar no broker. Um dos dois tinha de
   ceder, e cedeu o sorteio. O custo é o segredo passar pela mão de uma pessoa, uma vez por
   unidade
4. **ACL no broker por unidade**: cada credencial publica só no próprio prefixo de tópico.
   Sem isso, uma unidade comprometida falsifica os dados de todas as outras, e o painel vira
   fonte de conclusão errada — que é pior que painel nenhum.
5. **A senha fica na NVS sem criptografia** (débito 10). Antes disso era senha que dava
   acesso à placa; agora é credencial que alcança o servidor central. Não bloqueia o piloto,
   mas o débito 10 deixa de ser só sobre acesso local.

## Estrutura

Respeita a regra de dependência do [AGENTS.md](../../AGENTS.md):

```
domain/telemetry.{h,cpp}         # a amostra, a montagem do payload (reusa domain/json),
                                 # e a decisão "publica agora?" (intervalo + mudança de estado)
domain/telemetry_buffer.{h,cpp}  # política do anel: capacidade, descarte do mais velho,
                                 # correção do timestamp colhido antes do relógio
domain/mqtt_backoff.{h,cpp}      # nextBackoffMs() e shouldAttemptConnect() — aritmética pura
infra/mqtt_client.{h,cpp}        # wrapper fino sobre esp_mqtt_client
main.cpp                         # injeção, nada mais
```

Duas restrições de implementação que não são detalhe:

- **`esp_mqtt_client` já vem no IDF 4.4.7.** Nenhum componente novo, nenhuma entrada em
  `idf_component.yml`. Atenção à versão: o `esp_mqtt_client_config_t` da 4.4 é plano, e todo
  exemplo de internet escrito para a IDF 5.x usa a forma aninhada (`.broker.address.uri`) —
  não compila aqui.
- **`esp_mqtt_client_enqueue()`, nunca `esp_mqtt_client_publish()`.** O `loop()` já bloqueia
  ~3 s por ciclo (débito 4); somar espera de rede a ele congelaria a página de configuração
  pelo mesmo motivo que tirou a reconexão do uplink do `loop()` na Fase 6.

### Configuração nova

Host, porta, usuário, senha, intervalo e liga/desliga entram em `RouterSettings` e na página.
Campo novo com default seguro **não exige subir `kCurrentSchema`** — é a regra do AGENTS.md, e
foi assim com `admin_pend`. Default de fábrica: telemetria **desligada**, para que uma unidade
gravada sem configuração não fique tentando conectar em host inexistente e gastando dado.

## Custo de dado

Estimativa por unidade, TLS ligado, payload ~200 B: cerca de 330 B no fio por publicação, mais
~5 KB de handshake a cada reconexão.

- a 60 s → ~20–25 MB/mês
- a 300 s → ~3–6 MB/mês

Com o piloto de até 5 unidades o número não decide nada, e o default pode ser 60 s. Ele é
registrado aqui porque **decide o desenho quando a frota crescer**, e porque o critério de
aceite 8 manda medir em vez de acreditar nesta conta.

## Painéis e alertas

Entregáveis da fase, não sobra de fim de sprint:

**Painel de frota:** uma linha por unidade — online/offline, bateria, estado do uplink, versão
de firmware, último contato.

**Painel por unidade:** bateria e tensão no tempo, transições de estado do uplink,
`rate(ppp_drops_total)`, heap livre, uptime com os reboots visíveis.

**Alertas:**

| Alerta | Condição | Por quê |
|---|---|---|
| Unidade sumiu | `up == 0` ou `absent()` por 10 min | cobre o que LWT e buffer não alcançam |
| Esperar não resolve | `uplink_exhausted == 1` | único estado que exige ação humana |
| Bateria baixa | `battery_volts < 3.5` | visita antes do desligamento, não depois |
| Heap caindo | `free_internal_heap` abaixo do piso por 15 min | vazamento aparece antes do travamento |
| OTA pendente | `image_state` pendente há mais de 600 s | a imagem vai reverter sozinha; avisar antes |
| Enlace degradando | `rate(ppp_drops_total)` acima do normal | é a medição que o débito 13 pede |

## Escopo

- `domain/` — amostra, payload, anel e correção de timestamp, com testes nativos
- `infra/mqtt_client` — cliente sobre `esp_mqtt_client`, reconectando junto com o PPP
- `infra/ppp_drop_counter` — passa a manter o acumulado desde o boot
- `sdkconfig.defaults` — buffers do mbedTLS dimensionados para payload de telemetria
- `RouterSettings` + página de configuração — campos do broker
- Convenção de nome de métrica escrita **antes** do primeiro campo — `router_` e `sensor_`

Do lado do servidor, que já roda em produção, a fase só acrescenta:

- Broker (Mosquitto ou EMQX) com TLS e ACL por unidade, e o procedimento escrito de registro
  de uma unidade nova
- Telegraf `mqtt_consumer` → `remote_write`, com parser genérico (duas linhas de configuração)
- As duas chaves do Prometheus conferidas uma vez, conforme "A decisão"
- Painéis e alertas versionados como JSON no repositório, não clicados na interface

## Fora de escopo

- **Comandos de descida.** Nada de subscribe. Abre superfície de escrita do servidor para a
  placa e exige desenho de autorização próprio
- **RSSI/CSQ** — depende de CMUX, pelo motivo registrado em "O que é publicado"
- **Gatilho de OTA por MQTT.** É o canal de pull que o PRD 13 registrou; exige assinatura
  ECDSA junto, e é fase própria
- **Substituir a página por MQTT.** Configurar continua sendo pela página, e é o PRD 13 que
  a alcança de fora
- **Montar a `spiffs`** (débito 7) — segue em aberto, e volta à mesa com os sensores
- **Os sensores externos em si** — leitura, barramento, configuração por unidade. Esta fase
  decide os nomes, o formato e o armazenamento que eles vão usar; não lê sensor nenhum

## Riscos

- **Heap — o risco número um da fase.** O handshake TLS do mbedTLS tem pico de ~40–50 KB,
  disputando com PPP, NAT, DNS e servidor HTTP. **A PSRAM não socorre: ela existe na placa e
  não está compilada** (`# CONFIG_ESP32_SPIRAM_SUPPORT is not set` no sdkconfig gerado), então
  todo o heap é DRAM interna — a mesma que o projeto já considera apertada abaixo de 32 KB
  (`kTightInternalHeapBytes`). Some-se `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` e
  `OUT_CONTENT_LEN=4096`, que são 20 KB de buffer de registro por conexão TLS, dimensionados
  para HTTPS de página inteira e não para payload de 200 B.

  Ordem de ataque:

  1. **Encolher os buffers TLS já na abertura da fase** — `IN_CONTENT_LEN=8192` /
     `OUT_CONTENT_LEN=2048`. São ~10 KB de volta sem contrapartida, reversíveis numa linha, e
     o `IN` só precisa acomodar a cadeia de certificados do broker, que é nosso. Não depende
     de medição porque não tem lado negativo a pesar
  2. **Medir com o TLS carregado**, não antes. `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
     já é impresso a cada 30 s (`reportPppDrops` em `main.cpp`), mas medir sem o cliente MQTT
     de pé responde metade da pergunta: o piso que interessa é o de regime **com** handshake
     acontecendo, AP com tráfego e a página aberta
  3. **Ligar a PSRAM** (`CONFIG_ESP32_SPIRAM_SUPPORT` + `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`),
     tirando o mbedTLS da DRAM. Resolve de vez e é a mudança mais invasiva das três: mexe na
     alocação global de um firmware já validado em campo, então só entra se a medida do
     passo 2 disser que os 10 KB do passo 1 não bastaram
- **Reconexão em tempestade.** PPP cai e volta a cada ~16 s num cenário de RF ruim, e o
  auto-reconnect do `esp-mqtt` tenta a cada 10 s fixos sem saber se existe rota — handshake
  TLS condenado, caro em heap e em dado. Mitigação decidida: auto-reconnect **desligado**, a
  conexão é comandada pelo estado do uplink, com backoff 5 s→300 s, jitter de ±20% (`entropy`)
  e reset do backoff só após 60 s conectado. A decisão é aritmética pura e mora em `domain/`
- **Buffer que mente.** Um anel mal drenado publica a mesma amostra duas vezes ou perde a
  ordem; no Prometheus isso vira degrau falso no gráfico, e gráfico errado é pior que gráfico
  ausente porque produz decisão
- **Painel como fonte de verdade sem ACL.** Ver item 4 da segurança
- **Mais uma coisa no `loop()`.** O débito 4 já registra que ele bloqueia ~3 s por ciclo
- **Dado que some entre a placa e o painel.** As duas chaves do Prometheus e o parser do
  Telegraf falham do mesmo jeito: a placa publica, nada aparece, e **nenhum erro é registrado
  em lugar nenhum**. Conferidos uma vez no início, saem do caminho para sempre — não
  conferidos, o sintoma aparece meses depois e parece defeito do firmware

## Critérios de aceite

1. Unidade em bancada publica, e o painel mostra bateria e estado do uplink em até 2 min
2. Unidade fica 30 min sem uplink; ao voltar, o gráfico mostra o período **preenchido**, com
   as horas corretas, e não um buraco
3. Placa reiniciada perde no máximo a amostra em andamento — o buffer em RTC atravessa o reset
4. Amostra colhida antes do primeiro SNTP aparece com hora correta depois do flush
5. Broker derrubado à força: a unidade reconecta com backoff, sem tempestade de handshake
6. `roteador/<id>/status` retido mostra `offline` em até 2 min depois de a placa sumir
7. Credencial da unidade não aparece em log, nem no serial, nem na página
8. Consumo de dado da telemetria medido por 24 h e registrado aqui
9. Heap interno livre mínimo, com TLS ativo e tráfego de cliente no AP, medido e registrado
10. Uma unidade não consegue publicar no tópico de outra
11. Testes nativos da montagem do payload, da política do anel e da correção de timestamp
12. Painéis e alertas versionados no repositório e importáveis do zero
13. Campo novo adicionado ao payload aparece no Prometheus **sem nenhuma alteração no
    servidor** — só firmware
14. Firmware gravado por OTA sobre uma versão com struct de amostra diferente descarta o anel
    pelo cabeçalho, e não publica uma única amostra com valor lido de lixo
15. Backoff de reconexão testado nativamente, incluindo o teto e o reset após conexão estável
16. Convenção de nomes escrita e revisada **antes** da primeira série ir para o banco — é o
    único critério desta lista que não tem conserto barato depois

## Estado da implementação

**Domínio concluído em 23/09/2026** — `domain/mqtt_backoff`, `domain/telemetry` e
`domain/telemetry_buffer`, com 50 testes nativos (`pio test -e native`). Nenhuma linha toca
hardware, broker ou servidor; o firmware compila com eles dentro (RAM 10,6%, Flash 51,5%).

O **acumulado do `PppDropCounter`** entrou junto, em 23/09/2026, e não ficou sem
consumidor: o total desde o boot sai em `/api/status` e aparece na página de status. Isso
foi deliberado — acessor exposto que ninguém lê é exatamente o débito 22, e o consumidor
natural (`infra/mqtt_client`) ainda não existe. De quebra, o número do débito 13 deixa de
exigir um monitor serial aberto para ser lido.

Fecha os critérios 11 e 15 — com uma ressalva no 15: o que está testado é o **predicado**
`connectionIsStable()`. Zerar o contador de falhas a partir dele é fiação do
`infra/mqtt_client`, e entra junto com ele.

Três decisões que só apareceram na implementação:

- **Instante é `uint32_t`, não `unsigned long`.** Descoberto ao escrever o teste de virada
  de `millis()`: `unsigned long` tem 4 bytes na placa e 8 no host, e em 64 bits a conta não
  vira — o teste passaria sem exercitar nada. Virou regra no [AGENTS.md](../../AGENTS.md)
- **O backoff satura por comparação, não por deslocamento.** `kInitialBackoffMs << (falhas-1)`
  estoura o `uint32` sob falha permanente e devolve espera curta justamente quando ela
  deveria ser longa. Coberto por teste até `0xFFFFFFFF`
- **Os índices entraram na validação do cabeçalho.** `magic`, `layout` e `sample_size`
  corretos com `count` corrompido levariam o drenar a ler fora do array — pior que perder o
  buffer, porque é leitura de memória alheia

## Validação em hardware

Pendente — nada desta fase rodou em placa.
