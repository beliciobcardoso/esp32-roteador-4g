# PRD 06 — Integração e Reconexão Automática

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 6.

**Status: concluída**

## Objetivo

Fazer o roteador se sustentar sem intervenção: sobreviver a uma queda do enlace 4G e
aplicar configuração nova sem exigir que alguém aperte o botão de reset.

## Escopo

- `loop()` non-blocking (fecha o débito 4)
- Reconexão automática do modem com backoff, rearmando o NAT a cada sessão
- Detecção de queda silenciosa de RF (LCP echo)
- Config nova: definir e comunicar o que vale a quente e o que exige reboot
- `kMaxClients` refletindo o teto real do driver

**Fora do escopo:** teste de carga com dispositivos físicos.

## Depende de

PRD 01 a 05 completos.

## Decisões tomadas na implementação

### 1. A reconexão vive numa task FreeRTOS, não no `loop()`

`ModemPpp::start()` bloqueia por até ~160 s no pior caso (power-on 2,7 s + sync AT 20 s +
SIM 20 s + registro 60 s + IPCP 60 s). Três caminhos foram considerados:

- **(a) dentro do `loop()`** — mais simples, zero concorrência nova, mas `handleClient()`
  ficaria mudo por até ~160 s durante a reconexão. Exatamente quando o usuário quer abrir
  a página pra descobrir por que a internet caiu. Teria reintroduzido, pior, o bloqueio de
  3 s que a própria fase acabou de remover
- **(b) task dedicada** — `loop()` segue servindo HTTP o tempo todo; a task é a única dona
  do modem/netif depois do `begin()`, e o `loop()` só lê um enum de estado
- **(c) refatorar `start()` em máquina de estados não-bloqueante** — sem task e sem
  concorrência, mas quebraria `waitForAtReady`/`waitForSimReady`/`waitForNetwork` em steps
  com estado próprio cada, e o `esp_modem` já roda em tasks próprias de qualquer jeito

**Escolhido (b).** O RTOS já está ali e o esp_modem já é multi-task; (a) e (c) só compram
o direito de não criar uma task, que não é o custo dominante.

### 2. Reboot após 10 falhas seguidas, no máximo 2 vezes

Dez tentativas passam de 4 minutos, com reset por hardware do modem em cada uma. O que
sobra de causa plausível é estado travado que só o boot limpa. Derruba o AP junto, mas um
AP no ar sem internet nenhuma já não está servindo pra nada. Decisão do usuário.

O teto de 2 reinícios veio do teste de bancada com o SIM removido (ver Validação em
hardware). Sem teto, uma falha **permanente** — SIM fora, sem cobertura, sem crédito —
reinicia a placa a cada ~12 min para sempre, e cada reinício derruba quem está associado no
AP. O log do teste mostrou exatamente isso: um cliente associou durante a oitava falha, com
o 4G morto havia 9 minutos, e o reboot seguinte o desconectou sem nenhum ganho.

O reboot cura estado travado de software. Se dois ciclos completos não curaram, a causa é
externa e o terceiro reinício não muda nada — só tira do ar a única coisa que ainda
funciona. Depois do teto, o supervisor segue tentando indefinidamente com o backoff no topo
(60 s) e o AP nunca mais cai sozinho.

O contador vive em `RTC_NOINIT_ATTR` porque precisa atravessar o próprio `esp_restart()`
que ele dispara — a RTC RAM sobrevive ao reset por software. Depois de um power-on real o
conteúdo é lixo, e quem separa os dois casos é `esp_reset_reason() != ESP_RST_SW` no
`begin()`, não um magic word: mesmo trabalho, e o magic word ainda erraria 1 em 2^32. O
orçamento zera quando o uplink sobe, para que quedas separadas por dias de funcionamento
normal não somem no mesmo contador.

### 3. Teardown completo a cada tentativa

`ModemPpp::stop()` destrói DCE e netif (nessa ordem — o DCE referencia o netif) em vez de
tentar reaproveitar a sessão. Não tenta voltar ao modo de comando antes: a sequência de
escape leva segundos e falha justamente quando o módulo travou, que é quando isso é
chamado. O `powerOnSequence()` do próximo `start()` dá um reset por hardware de qualquer
jeito.

### 4. `esp_event_handler_register` passou a ser único

Registrar o mesmo `(base, id, handler, arg)` cria uma entrada nova a cada chamada, e o
handler passa a ser invocado N vezes. Como `start()` roda de novo a cada reconexão, isso
viraria N logs e N escritas por evento. Guardado atrás de uma flag de arquivo.

### 5. Escopo do NAT ficou com a reconexão

`NatBridge::enable()` é refeito a cada sessão: o netif é novo e o DNS da operadora pode
mudar entre sessões.

## Critérios de aceite

- [x] `kMaxClients` reflete o teto real do driver (15) e o boot não loga corte do ESP-NOW
- [x] `loop()` não bloqueia mais que ~100 ms por ciclo
- [x] Boot conecta o 4G em background sem segurar o AP nem a página de configuração
- [x] Queda de sinal → sistema se recupera sozinho ou loga claramente que não se recuperou
- [x] Mudança de APN reconecta a quente; mudança de SSID/senha avisa que exige reboot

## Validação em hardware

### Queda real de RF

`esp-netif_lwip-ppp: Connection timeout` → `NETIF_PPP_STATUS=ERRORPEERDEAD (9)` →
`Uplink: enlace caiu — reconectando` → teardown → `Uplink: online`. **~16 s** do evento à
sessão nova. É o LCP echo fazendo o trabalho: sem ele o PPP ficaria "up" para sempre e o
roteador viraria buraco negro.

### Troca de APN a quente

Reconectou com o APN novo sem reset, teardown limpo via `ERRORUSER (5)`. A resposta HTTP
diferencia corretamente o que vale a quente do que exige reboot.

**Ressalva:** o caminho de *falha* não foi exercitado por aqui. A Vivo aceitou o APN
deliberadamente inválido `1zap.vivo.com.br` e entregou IP mesmo assim, então `connectOnce()`
nunca devolveu `false`. Quem exercitou o backoff foi o teste do SIM.

### Troca de SSID

Persistiu na NVS e passou a valer no boot seguinte, como a resposta HTTP avisa.

### Backoff e reboot (SIM removido com a placa ligada)

Único jeito de produzir falha de verdade, já que a operadora aceita APN inválido. Sequência
medida: **5 s, 10 s, 20 s, 40 s, 60 s, 60 s, 60 s, 60 s, 60 s**, e `esp_restart()` na
décima. `rst:0xc (SW_CPU_RESET)` no boot seguinte confirma que foi o restart, não brownout
nem watchdog. Cada tentativa custa ~36 s (pulso de PWRKEY 1 s + sync AT ~6 s +
`kSimReadyMaxAttempts` 20 × 1 s), o que dá ~12 min da queda ao reboot.

Dois pontos que o log provou de graça:

- **O AP ficou no ar a queda inteira.** Um cliente associou durante a oitava falha e
  recebeu DHCP `192.168.4.2` com o 4G morto havia 9 minutos. É a decisão 1 se pagando: no
  `loop()` esse cliente não teria pego nem o endereço.
- **`Bateria:` imprimiu sem buraco do início ao fim**, inclusive durante as esperas de 60 s
  — o `loop()` nunca travou.

Foi esse mesmo log que motivou o teto de 2 reinícios da decisão 2.

### Orçamento de reinícios (SIM fora por 3 minutos corridos)

Teste dedicado ao teto da decisão 2, com `kMaxFailuresBeforeReboot` baixado a 2 para caber
em minutos em vez de meia hora. Os três pontos que faltavam provar apareceram no mesmo log:

1. **O contador atravessou o `esp_restart()`.** Primeiro reboot logou `reinicio 1/2`, o
   segundo logou `reinicio 2/2`. Se a RTC RAM não tivesse preservado o valor, o segundo
   boot teria voltado a imprimir `1/2` e a placa reiniciaria para sempre — era exatamente o
   bug que o teste existia para pegar.
2. **O terceiro reboot foi suprimido.** Após o orçamento gasto:
   `Uplink: 2 reinicios nao resolveram — causa externa (SIM, cobertura ou credito). Segue
   tentando sem reiniciar; o AP continua no ar.` Nenhum `rst:0xc` depois disso; as falhas
   seguiram até a décima segunda com o AP de pé e `Bateria:` sem buraco.
3. **A mensagem saiu uma vez só.** A guarda `consecutiveFailures_ == kMaxFailuresBeforeReboot`
   segurou a repetição a cada 60 s.

Com o SIM de volta: `Modem: SIM pronto na tentativa 2` → `Uplink: online` em ~11 s.

**Defeito cosmético corrigido a partir deste log:** o serial imprimia `Uplink: falha 12/2`,
porque o denominador era `kMaxFailuresBeforeReboot` mesmo depois do orçamento de reinícios
acabar. Passa a omitir o denominador quando não há mais reinício possível — o limite
deixou de existir, e mostrá-lo estourado sugeria um estado que o código não tem.

## Retrospectiva da fase

### O que acertou

**Botar a reconexão numa task, e não no `loop()`.** A decisão 1 parecia over-engineering na
hora de escrever — uma task, um mutex e uma cópia de `RouterSettings` para não bloquear uma
página HTML. O log do SIM removido pagou a conta sozinho: um cliente associou e recebeu
DHCP com o 4G morto havia 9 minutos, e `Bateria:` imprimiu sem buraco durante esperas de
60 s. Dentro do `loop()` esse cliente não teria pegado nem o endereço.

**Ligar o LCP echo antes de existir código de reconexão.** Sem `CONFIG_LWIP_ENABLE_LCP_ECHO`
uma queda silenciosa de RF nunca vira evento: o PPP fica "up" para sempre e o roteador vira
buraco negro. Toda a lógica de reconexão teria sido escrita e nunca disparado no caso mais
comum de falha em campo.

**Teardown completo a cada tentativa, sem tentar voltar ao modo de comando.** A sequência de
escape leva segundos e falha justamente quando o módulo travou — que é exatamente quando
`stop()` é chamado. Destruir DCE e netif e deixar o `powerOnSequence()` seguinte dar reset
por hardware é mais curto e não tem caminho de falha.

**Validar `esp_reset_reason()` em vez de gravar um magic word junto do contador.** Mesmo
trabalho de código, e o magic word ainda erraria 1 em 2^32 depois de um power-on.

### O que errou

**O reboot não tinha teto.** Erro de projeto, não de código: dez falhas disparavam
`esp_restart()` e o contador zerava no boot, então falha permanente reiniciaria a placa a
cada ~12 min para sempre, derrubando quem estivesse no AP a cada ciclo. Passou por revisão
sem ninguém ver. Quem pegou foi o hardware, e só porque o teste foi longo o bastante para o
segundo reboot acontecer. **Lição:** reinício como recuperação precisa de orçamento, e o
orçamento precisa sobreviver ao próprio reinício.

**O teste de troca de APN não testou o que parecia testar.** A Vivo aceitou o APN
deliberadamente inválido `1zap.vivo.com.br` e entregou IP mesmo assim, então `connectOnce()`
nunca devolveu `false` e o backoff jamais rodou — mas o teste foi dado como validação do
caminho de falha. Quem exercitou o backoff de verdade foi remover o SIM. **Lição:** teste
de caminho de erro só vale se o erro de fato aconteceu no log; "passou" não é evidência de
que a condição foi produzida.

**Meia hora perdida investigando `MEMP_NUM_TCPIP_MSG_INPKT` e `PBUF_POOL_SIZE`** como causa
das rajadas de `pppos_input_tcpip failed with -1`. As duas constantes são inertes neste
build: `MEMP_MEM_MALLOC 1` faz `memp_malloc()` virar `mem_malloc()`, que o
`MEM_LIBC_MALLOC 1` manda para o heap do ESP-IDF. Mexer nelas não faz nada. **Lição:** ler
a flag de alocador antes dos tamanhos de pool — ela decide se os pools existem.

**Denominador do log sem sentido depois do orçamento gasto** (`Uplink: falha 12/2`). Bug de
observabilidade, não de comportamento, mas o serial é a única janela para o que o supervisor
está fazendo em campo. Só apareceu porque o teste rodou além do ponto em que a mensagem
ainda fazia sentido.

**`server_.onNotFound(...)` nunca foi registrado** — rota desconhecida vira log de erro do
WebServer em vez de 404 (débito 14). Escapou porque nenhum teste pediu uma rota inexistente.

### Ficou aberto

- Débito 13 — rajadas de `pppos_input_tcpip failed with -1` sob tráfego. Afeta throughput,
  não conectividade, e é anterior à Fase 6. Sem ação deliberada, com plano de medição
  registrado
- Débito 14 — `onNotFound` ausente
- Débito 9 — cliente que associa antes do PPP subir recebe DNS `192.168.4.1`. A Fase 6 não
  mudou isso; a janela só encurtou porque o AP sobe antes do uplink de propósito
- `+CEREG: 0,11` continua aparecendo sem explicação desde a Fase 4. Inofensivo: o registro
  fecha como `CEREG=1` ~2 s depois
