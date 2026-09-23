# PRD 15 — Posição por GNSS

**Status: proposto** — não implementado. Fase 11.

Fonte: conversa de 23/09/2026, logo depois do [PRD 14](14-telemetria-mqtt.md). Depende dele:
posição sem canal para sair da placa não serve para nada, e o canal é a telemetria MQTT.

## Objetivo

Três perguntas, na ordem em que aparecem na operação:

1. **Onde esta unidade está instalada?** Hoje a resposta mora numa planilha que alguém
   atualizou na instalação, ou não atualizou. Inventário errado custa visita ao endereço errado.
2. **Esta unidade saiu do lugar?** A unidade é fixa. Mudar de posição é anomalia, e a única
   leitura razoável de "andou 8 km" é furto. Hoje isso é descoberto quando alguém liga
   reclamando.
3. **Onde cada unidade da frota está, agora, num mapa?** É o painel de geomap do Grafana
   alimentado pela mesma amostra do PRD 14.

As três se resolvem com fix esparso — nenhuma exige rastreamento contínuo. Isso define o
consumo, a cadência e metade das decisões abaixo.

## Pré-requisito bloqueante: qual A7670E é este

**A fase não começa sem essa resposta.** Nem todo A7670E tem GNSS, e a diferença não está no
nome do produto — está no modelo do módulo, segundo o repo canônico da LilyGO
(`docs/en/esp32/a7670-esp32/README.MD`; o wiki não serve, ver AGENTS.md):

| Modelo | GNSS | Observação |
|---|---|---|
| **A7670E-FASE** | ✅ | a única variante "E" com GNSS |
| A7670E-LASE | ❌ | |
| A7670E-LNXY-UBL | ❌ | só 4G, sem voz e sem SMS |
| A7670SA-FASE | ✅ | |
| A7670SA-LASE / LASC | ❌ | |
| A7670G-LLSE / LABE | ❌ | não tem GNSS interno **mesmo comprado "com GPS"** — nesse caso a placa vem com um módulo externo soldado na lateral |

A documentação ainda avisa que **o conector IPEX de GNSS só existe na PCB conforme a versão
do módulo**. Ou seja: a ausência do conector é sinal, mas a presença não é prova.

### Como descobrir, e por que é barato

`AT+SIMCOMATI` devolve modelo e versão de firmware. Rodar isso **não exige CMUX**: o modem
passa por modo comando no `ModemPpp::start()` antes de entrar em modo dados, e é ali que a
consulta cabe — uma chamada a `esp_modem_at()`, antes da troca de modo.

**Essa consulta deve virar permanente, independente do resto desta fase.** Saber o modelo e a
versão de firmware do modem de cada unidade em campo é inventário que hoje não existe, sai de
graça no boot e vai junto no `router_build_info` do PRD 14. Se a fase de GNSS for adiada ou
recusada, esta parte continua valendo.

### Os dois caminhos, conforme a resposta

- **É `-FASE`** → o caminho é este PRD: GNSS interno, lido por AT sobre CMUX
- **Não é** → GNSS interno não existe, e a alternativa é módulo GPS externo em UART própria.
  O orçamento de pinos desta placa está apertado — modem em 4, 5, 12, 25, 26, 27; SD em 2, 13,
  14, 15; bateria em 35; RING em 33; solar em 36 na V1.4 — e o custo passa a incluir BOM,
  antena e montagem por unidade. **Isso seria outro PRD**, não uma variação deste

## A decisão: CMUX

O modem está em modo dados PPP o tempo todo. GNSS no A76xx é lido por AT (`AT+CGNSSPWR`,
`AT+CGNSSINFO`), e AT não passa por uma UART ocupada com quadros PPP. Três saídas, e só uma
sobrevive aos três objetivos:

| Caminho | Como | Por que não / por que sim |
|---|---|---|
| Sair do modo dados a cada fix (`+++`/DTR) | derruba o PPP, lê, volta | **Recusado.** Um fix por hora seria uma queda de internet por hora, para os clientes do AP. Trocar o serviço pelo diagnóstico |
| Fix só no boot, em modo comando, antes do PPP | lê antes de subir o enlace | **Recusado, mas quase.** Resolve o objetivo 1 sem CMUX nenhum e com risco zero. Morre nos outros dois: não detecta movimento com a placa ligada, e ainda **atrasa a internet no boot** pelo tempo do cold start — de 30 s a alguns minutos, num boot que hoje entrega rota em muito menos. O roteador existe para rotear |
| **CMUX** | AT e PPP multiplexados na mesma UART | **Escolhido.** `esp_modem` 1.4.3 suporta (`ESP_MODEM_MODE_CMUX`), e é a única forma de ler o modem com o enlace de pé |

### O CMUX paga por duas features

O `AT+CSQ` ficou fora do PRD 14 pelo mesmo motivo que o GNSS ficaria: sem CMUX não há como
perguntar nada ao modem com o PPP no ar. Feito o CMUX aqui, **a exclusão de RSSI/CSQ do PRD 14
cai** e a qualidade de sinal entra na telemetria como qualquer outro campo — o que, junto com
o `ppp_drops`, finalmente separa "enlace ruim" de "placa com problema".

Não fazer as duas na mesma fase seria pagar o risco do CMUX duas vezes.

## O que se lê, e o que é regra

`AT+CGNSSINFO` devolve uma linha com modo do fix, satélites, latitude, longitude, altitude,
data, hora, velocidade e HDOP. **O parsing dessa linha é domínio puro** — texto para struct,
sem hardware, com teste nativo. É a mesma escolha do `domain/dns_message` e do
`domain/uplink_status`: o que decide algo tem teste; o que fala com o periférico é wrapper fino.

O projeto não usa TinyGSM ([AGENTS.md](../../AGENTS.md)), então não há `modem.getGPS()` pronto:
a linha é obtida por `esp_modem_at()` e interpretada aqui. Isso é bom para esta fase — o
formato do `+CGNSSINFO` é o tipo de coisa que quebra em versão nova de firmware do modem, e
um teste nativo com a linha real capturada em bancada torna essa quebra visível no CI, não em
campo.

### Fix ruim não pode virar alarme de furto

Um receptor parado oscila. Sem filtro, a oscilação normal do GNSS dispara "a unidade
andou" toda madrugada, e o alerta morre por descrédito na primeira semana — que é o pior
destino possível para um alerta de furto.

Três regras, todas em `domain/`, todas com teste nativo:

1. **Fix reprovado não entra.** Sem `fix mode` válido, com poucos satélites ou com HDOP acima
   do limite, a leitura é descartada antes de qualquer conta. Descartar é diferente de
   "posição zero" — `0,0` é um ponto no Golfo da Guiné, e já colocou frota inteira no mar em
   mais de um sistema
2. **Distância haversine contra a posição de referência**, com limiar em dezenas de metros —
   valor a calibrar com a dispersão medida em bancada, não escolhido no papel
3. **N fixes consecutivos acima do limiar** antes de afirmar movimento. Um fix ruim que passou
   pelas duas primeiras regras ainda não é suficiente

### A posição de referência é do operador, não do primeiro fix

Tentador: gravar como referência o primeiro fix bom e comparar contra ele. **Não serve.** Uma
placa furtada e religada em outro lugar gravaria a referência no cativeiro e nunca mais
acusaria nada — o mecanismo se desarmaria exatamente no cenário que existe para detectar.

A referência é gravada na NVS por ação autenticada na página ("fixar a posição atual como
posição de instalação"), como parte do procedimento de instalação. Enquanto não houver
referência, a unidade publica posição e **não** avalia movimento.

## Cadência e custo

Para instalação fixa, fix contínuo é desperdício em três moedas: dado móvel, consumo (o GNSS
do módulo puxa corrente a mais, e a placa tem bateria) e ruído no banco.

- **GNSS ligado por janela**, não permanente: acorda, tenta fix por um tempo limitado, publica
  e desliga. Cadência default na casa de uma vez por hora, configurável pela página
- **Publicação por mudança**, seguindo a regra do PRD 14: posição que não mudou além do limiar
  não vira amostra nova a cada janela — vai um heartbeat esparso para o painel saber que a
  medição está viva
- Lat/lon acrescentam ~40 B ao payload quando vão. No piloto isso não decide nada; a regra
  existe porque a frota cresce

**Cold start é caro e precisa de céu.** Primeiro fix após power-on leva de ~30 s a alguns
minutos. A instalação declarada para esta frota é externa, com vista do céu, que é a condição
em que isso funciona — dentro de armário ou galpão o fix não vem, ou vem com centenas de
metros de erro. O firmware trata "sem fix" como estado normal e permanente, nunca como falha:
é o mesmo princípio do `describeUplinkStatus()` — dizer se esperar resolve.

## Estrutura

```
domain/gnss_fix.{h,cpp}       # parsing do +CGNSSINFO e validação do fix (satélites, HDOP)
domain/geo_distance.{h,cpp}   # haversine e a decisão de movimento (limiar + N confirmações)
infra/modem_gnss.{h,cpp}      # liga/desliga o GNSS e pede a linha, por esp_modem_at()
infra/modem_ppp.cpp           # passa a subir em CMUX; expõe o canal de AT
adapters/http_config_handler  # posição atual, botão de fixar referência, cadência
main.cpp                      # injeção
```

`RouterSettings` ganha: GNSS ligado/desligado, cadência, latitude e longitude de referência,
limiar de movimento. Campo novo com default seguro **não sobe `kCurrentSchema`** — regra do
AGENTS.md. Default de fábrica: GNSS **desligado** e sem referência.

## Escopo

- Consulta de `AT+SIMCOMATI` no boot, com o modelo publicado na telemetria — **vale mesmo se o
  resto da fase for recusado**
- Migração do `ModemPpp` para CMUX, com revalidação das Fases 4 a 6
- Leitura de GNSS, parsing e validação do fix
- Detecção de movimento e posição de referência
- RSSI/CSQ na telemetria, aproveitando o mesmo canal de AT
- Campos novos na página e na NVS
- Painel de geomap e alerta de movimento no Grafana

## Fora de escopo

- **Rastreamento contínuo.** Nenhum dos três objetivos pede, e habilitar mudaria consumo,
  cadência e custo de dado
- **GNSS como fonte de hora.** O SNTP da Fase 7 já resolve e não depende de céu aberto
- **Módulo GPS externo.** Se o modem não for `-FASE`, é PRD próprio
- **Geofence com polígono.** Distância de um ponto cobre "saiu do lugar"; área desenhada é
  outro problema
- **AGPS / A-GNSS** para encurtar o cold start. Exige dado de assistência do servidor, e a
  cadência de uma vez por hora não sofre com cold start longo

## Riscos

- **CMUX é o risco número um, e ele mora debaixo de tudo que já funciona.** O PPP validado em
  campo nas Fases 4 a 6 passa a rodar sobre uma camada nova, na mesma UART. Não é uma feature
  ao lado do enlace: é o enlace mudando de transporte. Revalidar reconexão, queda de RF e
  perda de SIM não é opcional
- **A UART a 115200 já é gargalo, e o CMUX cobra enquadramento em cima disso.** O débito 13
  mostrou que o caminho de entrada do PPP satura sob tráfego; somar uma camada de multiplexação
  pode reabrir o que o `CORE_LOCKING` fechou. Subir o baud (`AT+IPR`) é a mitigação óbvia e
  **precisa ser medida junto**, com o mesmo instrumento do débito 13
- **GNSS e dados ao mesmo tempo.** Em alguns módulos SIMCom os dois não coexistem — o exemplo
  oficial do SIM7080G desativa a rede antes de ligar o GPS. Para o A76xx a documentação não
  impõe isso, e o exemplo de GNSS não desativa nada, mas **isso é leitura de documentação, não
  medição**. Se coexistirem mal nesta placa, a fase inteira muda de forma
- **Alarme de furto que grita errado.** Coberto pelas três regras de domínio, e é a parte com
  mais chance de precisar de recalibração depois de dados reais
- **Posição da frota é dado sensível.** A ACL por unidade do PRD 14 cobre a publicação; o
  acesso ao painel passa a valer mais do que valia
- **Heap.** CMUX aloca buffers por canal, e o PRD 14 já registrou que a PSRAM desta placa não
  está compilada

## Critérios de aceite

1. Modelo do modem lido no boot e visível na telemetria, sem CMUX e sem atrasar o enlace
2. Com `CONFIG` de CMUX ativo, o roteador continua fazendo tudo o que fazia: cliente do AP
   navega, DNS resolve, reconexão após queda de RF e após perda de SIM funcionam
3. `rate(router_ppp_drops_total)` sob a mesma carga de antes do CMUX **não piora** — medido,
   não estimado
4. Fix válido aparece no painel de geomap na posição certa, conferida contra o endereço real
5. Fix inválido (poucos satélites, HDOP alto) é descartado e **nunca** vira ponto no mapa
6. `0,0` não aparece em nenhuma circunstância
7. Unidade parada por 24 h não dispara alerta de movimento nenhuma vez
8. Unidade deslocada de verdade dispara o alerta, e a distância relatada bate com a real
9. Sem posição de referência gravada, a unidade publica posição e não avalia movimento
10. RSSI/CSQ publicado junto, fechando a exclusão registrada no PRD 14
11. Testes nativos do parsing do `+CGNSSINFO` — com linha real capturada em bancada —, da
    validação do fix, do haversine e da regra de movimento
12. Consumo de bateria medido com GNSS na cadência escolhida, contra a linha de base sem GNSS

## Validação em hardware

Pendente — fase não iniciada, e bloqueada pelo `AT+SIMCOMATI`.
