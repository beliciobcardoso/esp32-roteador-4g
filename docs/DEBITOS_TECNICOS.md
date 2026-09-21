# Débitos Técnicos

## 1. `VOLTAGE_DIVIDER_RATIO` hardcoded e calibrado por placa — RESOLVIDO em 19/09/2026

**Onde:** [include/config.h](../include/config.h) — `BATTERY_VOLTAGE_DIVIDER_RATIO`
(era `src/main.cpp`, movido em 19/09/2026)

Constante `2.19` calibrada com multímetro numa placa específica (16/09). Resistores do divisor variam por tolerância/placa — ratio não é universal.

**Ação:** mover para `config.h` (já previsto no plano, Fase 1) como default de fábrica, sobrescrevível via NVS/config, em vez de `#define` fixo no firmware.

**Feito:** o `#define` saiu do `main.cpp` e virou `BATTERY_VOLTAGE_DIVIDER_RATIO` no
[config.h](../include/config.h), junto dos outros defaults de fábrica e com a calibração
documentada ali. Recalibrar é trocar um valor num arquivo que já existe para isso.

**O que estava em aberto:** sobrescrever por configuração. O bloqueio nunca foi o
trabalho, era o efeito — campo novo obriga subir o schema, e `load()` tratava schema menor
que o atual como registro ausente. Pior depois do débito 10: `load()` falso faz o
`ProvisionSettingsUseCase` **sortear senha nova**, então o bump não custava só a
configuração, custava o acesso à unidade. A migração ([PRD 10](prd/10-migracao-de-schema.md))
acabou com isso.

**Resolvido:** `battery_divider_ratio` é campo de `RouterSettings`, gravado em `bat_ratio`
com `putFloat`/`getFloat`, editável pela página de config e aplicado a quente
(`BatteryAdc::applyDividerRatio()`, sem reboot e sem reconectar nada). Entrou no degrau
2 → 3 junto com o fuso da Fase 7: fazer separado custaria dois degraus, dois testes e dois
bumps, e a unidade em campo migraria 1 → 2 → 3 de qualquer jeito. O `config.h` continua
tendo o valor, agora só como default de fábrica.

**A faixa `[1.4, 10.0]` é física, não gosto.** `ratio = Vbateria / Vpino`: uma LiPo 1S cheia
chega a ~4.4 V e a referência do ADC é 3.3 V, então abaixo de 4.4/3.3 = 1.33 a leitura
satura e a placa reporta tensão **menor** justamente quando está carregada — falha
silenciosa, porque o ADC não avisa que grampeou. 1.4 arredonda isso pra cima; 10.0 pega o
`21.9` digitado com o ponto no lugar errado. Os dois extremos, o zero e o negativo têm teste
nativo.

**Vírgula é recusada com 400 antes do `toFloat()`**, que para `"2,19"` devolve `2.00` sem
sinal nenhum de erro: valor dentro da faixa válida, e a bateria passaria a ser lida com ~9%
a menos para sempre. O `<input type="number">` normaliza, mas ele só existe no navegador.

Falta validar em placa que a troca pela página muda a leitura seguinte — Fase 7 ainda não
rodou em hardware.

## 2. Perda de precisão silenciosa em `voltageToPercent` — RESOLVIDO em 19/09/2026

**Onde:** [src/domain/battery.cpp](../src/domain/battery.cpp) — `voltageToPercent()`
(era `src/main.cpp`, extraído em 19/09/2026)

`return p2 + frac * (p1 - p2);` retorna `float` em função `int` — trunca sem aviso. Funciona pra exibição de %, mas não está documentado como intencional (podia arredondar com `round()` ou já declarar o intuito no comentário).

**Ação:** decidir explicitamente — arredondar (`round()`) se quiser %, ou mudar assinatura pra `float` se precisão importar em algum consumidor futuro (ex: log/telemetria).

**Resolvido:** arredonda, via `std::lround`. Assinatura fica `int` porque o único consumidor
exibe percentual inteiro, e o cabeçalho registra a condição de reabrir: se aparecer
consumidor que precise de resolução abaixo de 1%, o que muda é a assinatura — arredondar
aqui e deixar o chamador lidar com a perda seria esconder a decisão de novo.

O truncamento errava sempre para baixo, até 1 ponto. Um teste nativo fixa o caso
(4.1324 V → 93%, truncando daria 92), escolhido longe de .5 para não depender do erro do
`float`.

## 3. Sem suavização entre ciclos de leitura de bateria

**Onde:** [src/infra/battery_adc.cpp](../src/infra/battery_adc.cpp) — `readVoltage()`
(era `src/main.cpp`, extraído em 19/09/2026)

Cada `loop()` reamostra do zero (`NUM_SAMPLES` leituras), sem média móvel ou filtro entre ciclos anteriores. Ruído do ADC pode causar variação de % perceptível entre prints consecutivos.

**Ação:** avaliar filtro exponencial (EMA) entre leituras se oscilação incomodar na UI final; por ora é decisão aceita pro protótipo, não bug.

**Nota, 19/09/2026:** segue sendo decisão aceita, não foi mexido. Mudou só o custo de
mexer: com a leitura em `infra/battery_adc` e a conversão em `domain/battery`, um EMA cabe
num dos dois (estado da amostragem em infra, ou suavização pura como função de domínio
testável) sem tocar no `main.cpp`. Antes era editar o mesmo bloco de novo.

## 4. `loop()` bloqueia `httpConfigHandler.handleClient()` por ~3s por ciclo — RESOLVIDO na Fase 6

**Onde:** [src/main.cpp](../src/main.cpp) — `loop()`

`delay(500)` x2 (blink LED) + `delay(2000)` (intervalo de leitura de bateria) somavam ~3s de bloqueio síncrono antes do próximo `handleClient()`.

**Resolvido:** blink e leitura de bateria agora são agendados por `millis()` (`blinkLed()` /
`reportBattery()`), e o `loop()` só encadeia checagens de prazo. Restou um bloqueio de
~100ms a cada 3s dentro de `readBatteryVoltage()` (`NUM_SAMPLES` x `delay(5)`) — mantido
de propósito, é a média que tira o ruído do ADC e não atrapalha nem o HTTP nem a
supervisão do modem.

## 5. Limite de clientes do AP prometia 20, teto do driver é 15

**Onde:** [src/infra/wifi_ap.cpp](../src/infra/wifi_ap.cpp) — `kMaxClients`

`kMaxClients` era 20 e o driver cortava em 10, avisando no boot:

```
W (1209) wifi:Affected by the ESP-NOW encrypt num, set the max connection num to 10
```

Duas causas distintas, e a segunda eu tinha diagnosticado errado antes:

1. As chaves reservadas ao ESP-NOW consumiam metade dos slots. Como não usamos ESP-NOW em
   lugar nenhum, `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=0` devolve esses slots.
2. **20 nunca foi alcançável.** `ESP_WIFI_MAX_CONN_NUM` vale **15** no ESP32 clássico
   (`esp_wifi_types.h:320`) — é teto de driver, não de configuração. A ação registrada
   aqui antes ("zerar o ESP-NOW libera os 10 slots restantes") estava errada.

**Resolvido parcialmente na Fase 6:** `kMaxClients = 15` e `ESPNOW_MAX_ENCRYPT_NUM=0`.
Docs que prometiam 20 (`AGENTS.md`, `PLANO_ROTEADOR.md`, PRD 02, PRD 06) corrigidos.

**Em aberto:** só o log de boot foi verificado (ausência do warning de corte). Quantos
clientes a placa realmente aguenta *navegando ao mesmo tempo* é outra pergunta, e depende
de RAM e de vazão do 4G, não do teto do driver.

## 6. Rollback de OTA ainda desabilitado — RESOLVIDO em 19/09/2026

**Onde:** [sdkconfig.defaults](../sdkconfig.defaults), [partitions.csv](../partitions.csv)

A tabela de partições já tem os dois slots (`app0`/`app1`) e `otadata`, mas
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` estava intencionalmente desligado: sem um cliente OTA
que chame `esp_ota_mark_app_valid_cancel_rollback()`, ligar o rollback faria o bootloader
reverter toda imagem nova como se tivesse falhado.

**Resolvido junto com a Fase 8** (ver [PRD 11](prd/11-atualizacao-ota.md)), porque os dois
são indissociáveis: a chave está ligada e quem confirma é o `loop()`, via
`needsHealthConfirmation()` e `verificationWindowElapsed()` de `domain/firmware_update`.

O critério de saúde não é "chegou ao `setup()`". Confirmar ali tornaria o rollback quase
inútil — pegaria só o firmware que morre antes de o AP subir — e o estado `PendingVerify`
nunca apareceria na página. A imagem é confirmada depois de **300 s de pé**, e esse prazo
tem teto: o `LinkSupervisor` reinicia a placa após 10 falhas de uplink, o que com backoff de
5/10/20/40/60 s passa de 7 min. Esse reboot é por falta de sinal, não defeito do firmware —
uma janela que encostasse nele faria uma área sem cobertura reverter uma atualização boa. O
teste `test_the_window_fits_before_the_supervisor_can_reboot` amarra as duas pontas.

## 7. Partição `spiffs` reservada mas não montada

**Onde:** [partitions.csv](../partitions.csv)

256 KB em `0x3C0000` reservados e nunca montados. Custo zero hoje (é só endereço não usado),
mas é espaço parado se nada for escrito ali.

**Ação:** decidir na Fase 6 — montar para logs/telemetria persistente, ou devolver o espaço
aos slots de app.

**A Fase 8 (OTA) não consumiu essa partição**: o upload vai do socket direto para o slot de
app, sem arquivo intermediário. A decisão continua aberta pelos mesmos motivos.

## 8. `infra/modem_ppp` escreve diagnóstico direto no `Serial`

**Onde:** [src/infra/modem_ppp.cpp](../src/infra/modem_ppp.cpp)

Toda saída de diagnóstico usa `Serial.printf` direto. Funciona e foi essencial na
depuração, mas acopla a camada de infra ao transporte de log — não dá para redirecionar
para a página web, um buffer em RAM ou telemetria sem reescrever as chamadas.

**Ação:** avaliar uma abstração de log quando houver um segundo consumidor real. Hoje há
apenas um; criar a abstração agora seria abstração especulativa.

## 9. Cliente que associa antes do PPP subir recebe DNS inútil — RESOLVIDO em 19/09/2026

**Onde:** [src/infra/nat_bridge.cpp](../src/infra/nat_bridge.cpp), [src/main.cpp](../src/main.cpp)

O DNS da operadora só é conhecido depois do IPCP, então o `NatBridge` reconfigura o
servidor DHCP do AP **depois** que o AP já está no ar. Um cliente que associe nessa janela
recebe um lease com DNS `192.168.4.1` — e não há resolvedor escutando ali. Ele roteia por
NAT, mas não resolve nome nenhum até renovar o lease.

No boot normal isso não aparece: o AP ainda não tem cliente quando o PPP fecha (~17 s).
O caso real é a reconexão de PPP em campo, com clientes já associados — que é justamente o
que a Fase 6 vai implementar.

**Ação:** decidir na Fase 6 entre (a) encurtar o lease do DHCP para que a renovação corrija
sozinha em minutos, (b) subir um forwarder DNS local em 192.168.4.1, o que torna o endereço
entregue no lease permanentemente válido e elimina a dependência de renovação, ou (c) só
documentar e exigir reconexão manual do cliente. A (b) é a única que resolve de verdade.

**Resolvido:** caminho (b) — [src/infra/dns_forwarder.cpp](../src/infra/dns_forwarder.cpp),
descrito em [PRD 09](prd/09-dns-local.md). A (a) foi medida e descartada: o padrão da IDF é
`DHCPS_LEASE_TIME_DEF` = 120 minutos (`dhcpserver.h`), T1 ≈ 60 min, então o cliente errado
fica errado por até uma hora; e encurtar o lease multiplica as renovações, o que agrava o
débito 12. O forwarder escuta em `192.168.4.1:53` — bind explícito nesse IP, nunca
`INADDR_ANY`, senão o mesmo socket atenderia a interface PPP e o roteador viraria
resolvedor aberto para a rede da operadora. O upstream sai de `dns_getserver(0)` lido a
cada pergunta, então reconexão que troque o servidor da operadora já vale na pergunta
seguinte. Sem uplink a resposta é SERVFAIL imediato, não silêncio.

**Validado em hardware (19/09/2026):** celular associado ao AP **antes** do IPCP — o AP sobe
em ~1,2 s e o uplink fechou em 15,3 s, então a janela do débito era de 14 s — recebeu
`192.168.4.1` no lease e resolveu nome assim que o `Uplink: online` apareceu, sem renovar o
lease e sem reconectar. Era exatamente o que não acontecia antes. O forwarder subiu em
silêncio, como esperado: ele só loga quando falha, e nenhuma linha `DNS:` saiu no boot.

**Não validado em hardware ainda:** o SERVFAIL sem uplink e a porta 53 fechada do lado PPP.

**Não coberto:** DNS over TCP. Resposta truncada faz o cliente reperguntar por TCP, e não
há ninguém escutando em `192.168.4.1:53/tcp`. Não há cache: cada pergunta vira uma pergunta
ao upstream.

## 10. Credenciais de fábrica em claro, NVS sem criptografia, admin sobre HTTP puro — PARCIAL em 19/09/2026

**Onde:** [include/config.h](../include/config.h), [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp), [sdkconfig.defaults](../sdkconfig.defaults)

Três problemas que se somam, todos aceitáveis em bancada e nenhum aceitável em campo:

- Senha do AP (`roteador4g`) e credenciais de admin (`admin` / `admin1234`) estão em claro
  num header versionado. São **iguais em toda unidade** que ainda não foi reconfigurada, e
  públicas para quem tiver o repositório.
- `CONFIG_NVS_ENCRYPTION` não está habilitado: as senhas ficam legíveis na flash. Quem tiver
  acesso físico à placa faz um dump e extrai tudo.
- A página de configuração usa **HTTP Basic Auth sobre HTTP puro**. As credenciais trafegam
  em base64, protegidas apenas pelo WPA2 do AP — e o WPA2 é PSK compartilhado, então
  qualquer cliente associado consegue capturá-las.

**Ação:** antes de qualquer unidade sair de bancada — senha de AP derivada por dispositivo
(ex.: sufixo do MAC), obrigar troca da senha de admin no primeiro acesso, e habilitar
`CONFIG_NVS_ENCRYPTION`. Detalhes do que está guardado em
[CONFIGURACAO_NVS.md](CONFIGURACAO_NVS.md).

**Feito** (ver [PRD 08](prd/08-segredos-por-unidade.md)): não existe mais senha de fábrica.
`DEFAULT_AP_PASSWORD` e `DEFAULT_ADMIN_PASSWORD` saíram do [config.h](../include/config.h).
Numa NVS vazia, `ProvisionSettingsUseCase` sorteia as duas senhas no primeiro boot, grava e
imprime uma única vez no serial. A senha de admin nasce marcada como pendente
(`admin_password_pending`, chave `admin_pend`), e o `POST /` recusa qualquer gravação que a
mantenha — trocar a senha de admin é a primeira coisa que a página aceita fazer. O caminho
de fallback do `LoadSettingsUseCase` (NVS ilegível) passou a devolver senhas vazias, que o
`validate()` reprova e que impedem o AP de subir: falha visível em vez de placa no ar com a
senha que está no repositório.

**A sugestão de derivar do MAC foi rejeitada**, e não por gosto. O MAC do SoftAP é o MAC
base com o último octeto incrementado (`esp_hw_support/mac_addr.c`,
`case ESP_MAC_WIFI_SOFTAP: mac[5] += 1`), e esse MAC é o BSSID, transmitido em todo beacon.
Qualquer pessoa no alcance lê o BSSID num scan passivo e calcula a senha: seria trocar uma
senha pública no GitHub por uma senha pública no ar. Segredo por unidade tem que ser
sorteado, e o sorteio usa `bootloader_random_enable()` porque acontece antes do rádio subir,
justo na janela em que `esp_random()` não é confiável.

**Agravado em 19/09/2026 pela Fase 8 (OTA).** A senha de admin deixou de valer
"reconfigurar o roteador" e passou a valer **executar código arbitrário na placa**: quem
autentica no `POST /update` troca o firmware. O canal não mudou — Basic Auth em base64 sobre
HTTP puro, num AP cuja PSK é compartilhada entre todos os clientes —, então qualquer cliente
associado que capture o handshake de outro consegue as credenciais e, com elas, a unidade
inteira. A guarda que existe é a ordem: a credencial é conferida no `UPLOAD_FILE_START`,
antes de qualquer escrita na flash, então quem não se identifica não grava nada. Isso protege
contra o anônimo, não contra o vizinho de AP. **Em bancada é aceitável; para campo, o TLS
abaixo deixou de ser melhoria e virou pré-requisito.**

**Em aberto — os dois que dependem de decisão, não de código:**

- **TLS na página de configuração.** Basic Auth continua sobre HTTP puro, e a PSK do WPA2 é
  compartilhada: qualquer cliente já associado captura o handshake dos outros e lê o
  tráfego deles. Fechar isso troca o `WebServer` do Arduino por `esp_https_server`, custa
  RAM e traz certificado autoassinado. É PRD próprio.
- **`CONFIG_NVS_ENCRYPTION`.** Não é flag independente: `depends on SECURE_FLASH_ENC_ENABLED`
  (`components/nvs_flash/Kconfig`, IDF 4.4.7). Ligar exige flash encryption, que **queima
  eFuse — permanente e por placa**, e em Release mode acaba com a regravação em texto claro.
  O terreno já está preparado: a partição `nvs_keys` cabe no vão de 56 KB em `0x12000` sem
  mover `nvs` nem `phy_init`, e o `Preferences` não precisa de mudança nenhuma. O que falta
  é a decisão de queimar, que não se desfaz.

**Unidade de bancada já gravada continua com `admin1234`** — provisionamento novo só roda em
NVS vazia, de propósito. Para reprovisionar, apagar a partição: ver
[CONFIGURACAO_NVS.md](CONFIGURACAO_NVS.md). Desde 19/09/2026 existe migração de schema
([PRD 10](prd/10-migracao-de-schema.md)), mas ela não muda isto: migrar preserva o registro
justamente para não re-sortear, então quem já tem `admin1234` gravado continua com ele até
apagar a NVS ou trocar a senha pela página.

## 11. Tabela NAPT não é limpa entre sessões PPP

**Onde:** [src/infra/nat_bridge.cpp](../src/infra/nat_bridge.cpp) — `enableNaptOnAp()`

`ip_napt_enable()` chama `ip_napt_init()`, que é idempotente de propósito
(`if (ip_portmap_table == NULL && ip_napt_table == NULL)`). Rearmar o NAT numa reconexão
não vaza memória, mas também não descarta as entradas da sessão morta: elas seguem
ocupando slots das 512 até o `ip_napt_tmr` expirar.

Não quebra correção — o rewrite de saída usa o `netif->ip_addr` corrente, então pacotes
novos saem com o IP novo, e as conexões TCP antigas já estão mortas do outro lado.

**Ação:** nenhuma por ora. Vira problema se as reconexões forem frequentes o bastante pra
esgotar a tabela antes do timer limpar. Medir antes de mexer.

## 12. Janela de DHCP fechada a cada reconexão — RESOLVIDO em 19/09/2026

**Onde:** [src/infra/nat_bridge.cpp](../src/infra/nat_bridge.cpp) — `offerDnsToApClients()`

O DNS da operadora só entra na opção 6 do DHCP com o servidor parado
(`esp_netif_dhcps_option` devolve `ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED` se estiver
rodando), então cada reconexão faz `dhcps_stop` → `set_dns_info` → `dhcps_option` →
`dhcps_start`. Clientes já associados mantêm o lease, mas um cliente que peça DHCP
exatamente nessa janela falha e precisa tentar de novo.

**Ação:** aceito. A janela é de milissegundos e o cliente DHCP retenta sozinho. Some junto
com o débito 9, se o forwarder de DNS local for implementado — aí o DNS entregue passa a
ser sempre `192.168.4.1` e não precisa mais mudar entre sessões.

**Resolvido:** foi o que aconteceu. `offerDnsToApClients()` não existe mais, e com ela
sumiram o `dhcps_stop` e o `dhcps_start` do caminho de reconexão — o débito morreu por
remoção de código, não por contorno. O `NatBridge` só liga o NAPT e loga o DNS da
operadora; o valor entregue na opção 6 passa a ser o que o `dhcpserver` já preenchia
sozinho (o IP do AP), e agora existe resolvedor nesse endereço. Uplink sem DNS também
deixou de ser fatal: o NAT arma do mesmo jeito e o forwarder responde SERVFAIL.


## 13. Pacotes PPP de entrada descartados sob tráfego (`pppos_input_tcpip failed with -1`)

**Onde:** fila da task tcpip do lwIP — `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE` em
[sdkconfig.defaults](../sdkconfig.defaults) (hoje no default, 32)

Durante tráfego de cliente o log solta rajadas de dezenas de linhas seguidas:

```
E (305667) esp-netif_lwip-ppp: pppos_input_tcpip failed with -1
```

`-1` é `ERR_MEM` (`err.h:57`), vindo de `pppos_input_tcpip()` (`pppos.c:429-445`).

**O que foi descartado:** os pools do lwIP não são o teto. `MEMP_MEM_MALLOC 1`
(`lwipopts.h:105`) faz `memp_malloc()` virar `mem_malloc()` (`memp.c:380`), e
`MEM_LIBC_MALLOC 1` manda isso pro heap do ESP-IDF. Logo `MEMP_NUM_TCPIP_MSG_INPKT` (8) e
`PBUF_POOL_SIZE` (16) do `opt.h` são inertes neste build — mexer neles não muda nada.

**Causa provável:** `sys_mbox_trypost(&tcpip_mbox, msg)` falhando (`tcpip.c:269-272`), ou
seja, fila da task tcpip cheia. `TCPIP_MBOX_SIZE` = `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE` = 32
(`lwipopts.h:626`). Cada quadro PPP entrante ocupa um slot, e com NAPT a mesma task ainda
faz o forward e entrega ao driver WiFi antes de drenar o próximo. Rajada de download entra
mais rápido do que ela escoa.

A outra possibilidade é heap interno esgotado no instante do burst, mas o formato do log
não bate: rajadas concentradas durante tráfego e silêncio completo fora disso é assinatura
de fila cheia, não de heap no talo — e nem o driver WiFi nem o `esp_modem` reclamaram junto.

**Impacto:** o TCP retransmite. Perde vazão, não perde conexão. Não é regressão da Fase 6 —
existe desde a Fase 5, só não aparecia sem tráfego sustentado.

**Ação:** nada por ora, de propósito. O lever óbvio é `CONFIG_LWIP_TCPIP_RECVMBOX_SIZE`
32→64 (custo: 128 bytes de ponteiros mais o heap dos pbufs que ficam na fila), mas é tuning
de vazão sem número medido — só empurra o ponto de saturação e pode trocar descarte cedo
por latência e pico de heap maior. Antes de mexer: medir a vazão real pelo celular e logar
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` periodicamente, pra separar em definitivo
fila cheia de heap esgotado.

Se 64 não bastar, as opções são `CONFIG_LWIP_TCPIP_CORE_LOCKING` +
`CORE_LOCKING_INPUT` (entrega direta, elimina a classe inteira do problema, mas muda o
modelo de concorrência do lwIP) ou `CONFIG_LWIP_IRAM_OPTIMIZATION` (acelera o forward,
custa IRAM). Ambas caras demais pra especular sem medição.

**Medido em 20/09/2026, e é pior do que "perde vazão".** Numa captura contínua de serial
durante os testes manuais da Fase 8, `7179` das `8067` linhas eram essa mensagem — **89% do
serial**. Numa segunda captura, `3150` de `5187` (60%), mais `64` linhas de
`W (…) uart_terminal: HW FIFO Overflow`. O efeito colateral é o que importa: com o FIFO
estourando, linhas de outros módulos saem cortadas ao meio, e a leitura periódica de
bateria aparece como `ateria:`, `eria:`, `ria:`. O log deixa de ser confiável exatamente
quando há tráfego — que é quando se quer olhar para ele.

Toda leitura de serial feita neste projeto passou a depender de um filtro
(`grep -avE "pppos_input_tcpip|ateria:|^ria:|^eria:|^teria:"`), o que é remendo de
ferramenta, não correção. Enquanto isso valer, qualquer diagnóstico por serial sob carga
começa com um passo manual que alguém vai esquecer. Baixar o nível de log desse componente
(`esp_log_level_set("esp-netif_lwip-ppp", ESP_LOG_NONE)`) esconderia o sintoma e continuaria
descartando pacote — pior negócio do que o remendo.


**Medido e corrigido em 20/09/2026 — validação sob carga sustentada pendente.**

A poluição do serial saiu primeiro, e sem calar o componente: `infra/ppp_drop_counter`
intercepta o `esp_log_set_vprintf`, conta o evento e suprime a linha; o `loop()` publica o
total uma vez por janela de 30 s, junto do heap interno livre. O número deixou de afogar o
serial e virou a medição que esta seção pedia. `HW FIFO Overflow` e as linhas de bateria
cortadas (`ateria:`, `eria:`) desapareceram junto.

Com o número na mão, as duas hipóteses se separaram:

| Config | Carga | Janelas | Total | Pico |
|---|---|---|---|---|
| `RECVMBOX=32` | vídeo | 14 | 3166 | 1239 |
| `RECVMBOX=64` | vídeo | 23 | 3043 | 1187 |
| `RECVMBOX=64` | zip 19 MB | 12 | 1727 | 540 |
| `CORE_LOCKING` | 5,6 MB (parcial) | 0 | 0 | 0 |
| `CORE_LOCKING` | zip 19 MB (parcial) | 0 | 0 | 0 |

**Não era heap.** O heap interno livre nunca desceu de 113 KB em nenhuma rodada, contra um
piso de 32 KB — a hipótese alternativa desta seção está descartada com número.

**Dobrar a fila não resolveu**, como esta seção previa antes da medição: 32 → 64 mudou o pico
de 1239 para 1187 na mesma carga. O gargalo é drenagem, não tamanho. A fila voltou a 32.

A correção é `CONFIG_LWIP_TCPIP_CORE_LOCKING` + `CORE_LOCKING_INPUT`: entrega direta sob
mutex, sem fila no caminho de entrada, então não há fila para encher. O código do projeto já
estava do lado certo — `dns_forwarder` usa BSD sockets, que adquirem o lock por dentro, e
`nat_bridge` chama `ip_napt_enable` por `esp_netif_tcpip_exec()`.

**O que falta:** nenhuma das duas rodadas com `CORE_LOCKING` teve download completo — o PPP
caiu no meio das duas. O 4G estava instável naquele dia (44 quedas somadas nas quatro
sessões, e mais frequentes *sem* `CORE_LOCKING`: 6 e 22 contra 14 e 2). Zero descartes em
~8 min de tráfego é sinal — no regime anterior a primeira janela do zip já marcava 540 — mas
não é prova sob carga sustentada. Fechar exige um download inteiro com enlace estável.
## 14. Requisição a rota não registrada vira log de erro — RESOLVIDO em 18/09/2026

**Onde:** [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) — `begin()`

Só `/` está registrado (GET e POST) e não há `onNotFound`. Qualquer `/favicon.ico` ou sonda
de portal cativo do Android cai no caminho default do `WebServer`, que loga como erro:

```
[E][WebServer.cpp:638] _handleRequest(): request handler not found
```

Benigno — o cliente recebe 404, nada quebra. O incômodo é o log: numa depuração de campo
essas linhas se misturam com erro de verdade.

**Ação:** `server_.onNotFound(...)` devolvendo 404 limpo, sem vazar detalhe interno. Duas
linhas. Não entrou na Fase 6 por ser fora do escopo dela.

**Resolvido:** `handleNotFound()` em
[http_config_handler.cpp](../src/adapters/http_config_handler.cpp) responde `404` com corpo
fixo — sem eco da URI e sem lista de rotas. Sem Basic Auth de propósito: exigir credencial
aí faria o navegador abrir o popup de senha por causa de um favicon.

---

Daqui pra frente os débitos vêm de revisão de código, não de fase de implementação, e
nenhum deles **foi priorizado** — a ordem é a de descoberta, não de importância. Cada um
registra explicitamente o que foi confirmado por leitura de fonte e o que segue em aberto,
porque a severidade de alguns depende de teste que ainda não foi feito.

- **15 a 21** — revisão completa do código em 18/09/2026.
- **22** — análise do projeto em 18/09/2026, posterior e independente da anterior. Mesma
  data, revisão diferente: não faz parte do lote acima.

## 15. `validate()` não impõe os limites de comprimento do 802.11 — RESOLVIDO em 18/09/2026

**Onde:** [src/domain/router_settings.cpp](../src/domain/router_settings.cpp) — `validate()`

A validação checa vazio e mínimo de 8 caracteres, mas não o **máximo**: SSID pode ter 40
caracteres e senha de AP pode ter 70, e os dois são persistidos na NVS.

**Confirmado por leitura** (core Arduino 2.0.17, `libraries/WiFi/src/WiFiAP.cpp`):

- `WiFiAPClass::softAP()` (linhas 136-150) **não valida comprimento máximo**. Só rejeita
  SSID vazio e senha entre 1 e 7 caracteres — e essa segunda checagem o nosso `validate()`
  já cobre. Uma revisão anterior afirmou que o core rejeita SSID acima de 32; não rejeita.
- `wifi_softap_config()` (linhas 102-120) monta a config assim:

  ```c
  _wifi_strncpy((char*)wifi_config->ap.ssid, ssid, 32);  // copia 32 bytes SEM terminador
  wifi_config->ap.ssid_len = strlen(ssid);               // recebe 40, não truncado
  ```

  `_wifi_strncpy` (linhas 53-65) faz `if (src_len >= dst_len) src_len = dst_len;`. Ou seja:
  com SSID de 40 caracteres o driver recebe um `wifi_ap_config_t` internamente
  inconsistente — buffer de 32 bytes sem NUL, e `ssid_len` valendo 40. Mesma coisa na
  senha, com `password[64]`.

**Em aberto:** o que o driver faz com essa config. `libnet80211.a` é blob, e a doc de
`esp_wifi_set_config` lista `ESP_ERR_WIFI_PASSWORD` mas não `ESP_ERR_WIFI_SSID`, fechando
com "others: refer to the error code in esp_err.h". Dois desfechos possíveis, com
severidades muito diferentes:

- **Driver rejeita** → `esp_wifi_set_config` falha → `softAP()` devolve false →
  `startRouting()` para no SoftAP ([src/main.cpp](../src/main.cpp)). Como
  SSID/senha só valem após reboot ([http_config_handler.cpp](../src/adapters/http_config_handler.cpp)
  — `handlePostRoot()`),
  o usuário salva, vê "Configuração salva", reinicia e a placa fica sem AP — e sem AP não
  há página de configuração. Recuperação só por serial ou `erase_flash`.
- **Driver aceita** → AP sobe com SSID truncado em 32. O usuário se conecta normalmente,
  só não vê o nome que digitou. Incômodo, não perda de acesso.

**Ação:** duas etapas, e a segunda depende da primeira.

1. Determinar o desfecho com um sketch de bancada que chame `WiFi.softAP()` com SSID de 40
   caracteres e logue o retorno, **sem persistir nada na NVS** — assim a dúvida se resolve
   sem arriscar deixar a placa sem AP.
2. Impor os limites em `validate()` (SSID ≤ 32 bytes, senha de AP entre 8 e 63) com os
   `SettingsValidationError` correspondentes. Vale nos dois desfechos; o que muda é a
   urgência.

**Resolvido:** etapa 2 feita — `kMaxSsidLength = 32` e `kMaxWifiPasswordLength = 63` em
[router_settings.cpp](../src/domain/router_settings.cpp), com `SsidTooLong` e
`WifiPasswordTooLong`. Quatro testes nativos fixam as duas fronteiras pelos dois lados
(32 aceito / 33 recusado, 63 aceito / 64 recusado).

A etapa 1 **não foi feita e deixou de ser pré-requisito**: com o limite imposto antes da
gravação, o driver nunca recebe a config inconsistente, e a correção era a mesma nos dois
desfechos. O que o teste de bancada responderia hoje é só curiosidade sobre o blob.

**Em aberto:** `load()` não revalida o que já está na NVS. Um valor acima do limite gravado
antes desta mudança continua sendo carregado e entregue ao `softAP()` como sempre foi — a
proteção é só na entrada. Custo de fechar: chamar `validate()` no `load()` e decidir o que
fazer com um registro reprovado, que não é obviamente "cair nos defaults" (isso apagaria
uma config que o usuário reconhece). Nenhuma placa conhecida está nesse estado.

## 16. `NvsSettingsRepository::save()` sempre reporta sucesso — RESOLVIDO em 18/09/2026

**Onde:** [src/adapters/nvs_settings_repository.cpp](../src/adapters/nvs_settings_repository.cpp) — `save()`

A função termina em `return true` fixo e não checa nenhum dos oito retornos que a
`Preferences` oferece. A interface promete o contrário —
"true se a gravação foi bem-sucedida"
(`save()` em [settings_repository.h](../src/adapters/settings_repository.h)) — e o HTTP responde
"Configuração salva." mesmo sem nada ter sido gravado.

**Confirmado por leitura** (`libraries/Preferences/src/Preferences.cpp`):

- `begin()` (linhas 33-56) devolve `false` se `nvs_open` falhar, e também se a instância já
  estiver aberta.
- `putString()` (linhas 267-283) devolve `0` se `nvs_set_str` **ou** `nvs_commit` falhar —
  partição cheia (`ESP_ERR_NVS_NOT_ENOUGH_SPACE`) cai exatamente aí.

**Cuidado na correção:** `putString()` devolve `strlen(value)`, então uma gravação
bem-sucedida de string vazia também devolve `0`. Como `apn_user` e `apn_password` são
opcionais e podem ser vazios de propósito
([router_settings.h](../src/domain/router_settings.h) — comentário sobre `apn_user`), testar `> 0` em todos os
campos criaria falso negativo justamente no caso legítimo. O critério tem que distinguir
"gravou vazio" de "não gravou".

**Ação:** propagar o retorno do `begin()` e checar os `putString`/`putInt`/`putBool` com um
critério que tolere campo opcional vazio. Vira mais relevante na Fase 7, que sobe o schema
para 3 e reescreve todos os campos de uma vez.

**Resolvido:** `begin()` propagado nos dois métodos e cada escrita checada em
[nvs_settings_repository.cpp](../src/adapters/nvs_settings_repository.cpp). O critério dos
`putString` é `retorno == valor.length()`, não `> 0` — é o que aceita campo opcional vazio
sem aceitar falha. Para `putInt` e `putBool` o retorno de sucesso é fixo (4 e 1, conferido
na fonte da `Preferences`), então ali `!= 0` basta. `configured` é a última escrita: se
algo antes falhou, a flag não é reescrita.

O `load()` também passou a checar `begin()`. Ali o comportamento não muda — sem namespace
gravado, o `getBool` já caía no default `false` e a função já devolvia `false`. Mudou de
acidente que depende do default para decisão explícita.

**Em aberto, dois pontos que a correção não fecha:**

- **A gravação não é atômica.** A `Preferences` faz `nvs_commit` por chave e não expõe
  transação, então falha no meio deixa campos novos e antigos misturados. O `save()` agora
  reporta a falha em vez de esconder; desfazer é outro problema. Fechar exigiria gravar um
  blob único ou manter dois registros e alternar o ponteiro.
- **Falha ao gravar campo vazio é indistinguível de sucesso.** `putString` devolve 0 nos
  dois casos e o critério aprova. Só afeta `apn_user`/`apn_password` vazios, e as causas de
  falha (partição cheia, handle inválido) derrubam também os cinco campos não vazios — a
  falha aparece, só não por esse campo. Separar de verdade exigiria reler a chave.

## 17. Valores da configuração vão para o HTML sem escape — RESOLVIDO em 18/09/2026

**Onde:** [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) — `handleGetRoot()`

`page.replace("{{SSID}}", current.wifi_ssid)` injeta o valor direto dentro de
`value="..."`. Um `"`, `<` ou `&` em SSID, APN ou usuário admin quebra o atributo e pode
deixar o formulário inutilizável — inclusive impedindo a correção do próprio valor pela
página que o quebrou.

Nenhuma dessas strings é filtrada: `validate()` não restringe caracteres, e SSID em 802.11
é sequência de bytes arbitrária. Mas o vetor exige estar autenticado como admin e digitar
um caractere incomum num campo de nome de rede, então é **baixa probabilidade com
consequência local** — não é XSS explorável por terceiro, é tiro no próprio pé.

**Ação:** escapar os quatro valores interpolados (`&`, `<`, `>`, `"`) antes do `replace`.
Uma função de escape no `html_page` resolve; é a mesma correção para os quatro campos.

**Resolvido:** `escapeForHtmlAttribute()` em
[html_page.cpp](../src/adapters/html_page.cpp), aplicada nos quatro `replace`.

Escapa um caractere além dos quatro previstos: `{` vira `&#123;`. Não é escape de HTML — é
o que impede um valor gravado de forjar um placeholder. Um SSID literal `{{APN}}`
atravessava o `replace` do SSID intacto e o `replace` seguinte o trocava pelo APN, jogando
o valor no campo errado. Consequência era cosmética, mas o custo de fechar era uma linha e
renderiza igual.

**Em aberto:** a função não tem teste. Vive em `adapters/` e depende da `String` do
Arduino, então está fora do `build_src_filter` do env nativo (débito 19). Testar exigiria
ou mover o escape para `domain/` — onde ele não pertence, é apresentação — ou abrir o
filtro para `adapters/`, que arrasta `WebServer.h`.

## 18. Lógica de bateria mora no `main.cpp`, contra a regra do próprio AGENTS.md — RESOLVIDO em 19/09/2026

**Onde:** [src/main.cpp](../src/main.cpp) — linhas 30-88 à época; o código saiu do arquivo
em 19/09/2026

O `AGENTS.md` declara para o `main.cpp`: "só orquestração/injeção, zero lógica de negócio".
São 70 das 198 linhas do arquivo em curva de descarga Li-ion, média de ADC e interpolação
tensão → percentual — lógica pura, que pertenceria a `domain/battery` (conversão) +
`infra/battery_adc` (leitura do pino).

Não é bug: o código funciona e está validado. É desalinhamento entre a regra declarada e o
arquivo, e explica por que os **débitos 1, 2 e 3 são todos sintomas do mesmo trecho** —
ratio hardcoded, truncamento silencioso e ausência de suavização são três consequências de
a conversão não ter camada própria.

**Ação:** extrair junto com o débito 1 (que já prevê mover o ratio para configuração).
Fazer as duas coisas separadamente significa mexer no mesmo código duas vezes. Extraída
para `domain/`, a conversão passa a ser testável sem hardware — ver débito 19.

**Resolvido:** a conversão foi para [domain/battery.cpp](../src/domain/battery.cpp)
(`voltageToPercent()`, com a curva em `constexpr` de namespace anônimo) e a amostragem do
pino para [infra/battery_adc.cpp](../src/infra/battery_adc.cpp) (`BatteryAdc`). As
constantes de ADC e o ratio foram para o [config.h](../include/config.h). O `main.cpp` caiu
de 198 para 123 linhas e ficou com o agendamento por `millis()` e o `Serial.print` — que é
orquestração, não regra.

Sete testes nativos cobrem a conversão: saturação nas duas pontas, ponto exato da tabela,
arredondamento (débito 2) e monotonicidade varrendo 2.80–4.40 V de 1 mV em 1 mV — essa
última pega ponto fora de ordem e buraco entre faixas, que devolveria 0 no meio da curva.

O `esp_log_level_set("gpio", ESP_LOG_WARN)` foi junto, para `BatteryAdc::begin()`. É
mudança global de nível de log, mas existe só porque `analogRead` loga 20 linhas por
leitura de bateria — ficando ao lado da causa, sai junto se a leitura sair.

**Em aberto:** nada da extração. A metade do débito 1 que dependia do schema 3 fechou na
Fase 7 (ver lá). O débito 3 não foi mexido, de propósito.

## 19. Nenhum teste automatizado, e nenhum ambiente onde rodar um — RESOLVIDO em 18/09/2026

**Onde:** [platformio.ini](../platformio.ini) — não há env `native`; não há diretório `test/`

O projeto adotou Clean Architecture explicitamente para isolar regra de negócio de
hardware, e hoje existem duas funções puras que essa escolha tornou testáveis sem placa:
`validate()` ([router_settings.cpp](../src/domain/router_settings.cpp)) e
`voltageToPercent()` (ainda dentro do `main.cpp` à época — débito 18). Nenhuma das duas tem
teste, e não há ambiente configurado para executar um.

O retorno prático disso é concreto: o débito 15 é um caso de limite em `validate()`, e um
teste de limite o pegaria em segundos, sem hardware e sem depender do comportamento do
driver.

**Ação:** env `native` no `platformio.ini` com os testes de `validate()` — incluindo os
limites do débito 15, uma vez decididos. Escopo deliberadamente pequeno: só o que é puro.
Testar `infra/` exigiria mock de ESP-IDF e não se paga aqui.

**Resolvido:** `[env:native]` no [platformio.ini](../platformio.ini) e 15 testes Unity em
[test/test_router_settings/test_router_settings.cpp](../test/test_router_settings/test_router_settings.cpp),
rodando por `pio test -e native`. Cobrem `validate()` inteira — cada código de erro, os
limites de comprimento pelos dois lados (8 caracteres de senha, e os máximos de SSID e
passphrase que vieram com o débito 15), a precedência entre campos inválidos, e o caso de
`apn_user`/`apn_password` vazios serem aceitos de propósito — mais `to_string()`, que hoje
tem mensagem própria para todo código do enum.

Duas coisas que o débito não previa, e que quem mexer aqui precisa saber:

- **O domínio não era puro.** [router_settings.h](../src/domain/router_settings.h) incluía
  `<Arduino.h>` sem condição, então `src/domain/` não compilava no host de jeito nenhum. A
  regra de dependência do AGENTS.md estava escrita, não verificada. O arquivo ganhou um
  `#ifdef ARDUINO` que troca `String` por `std::string` fora da placa.
- **O teste exercita `std::string`, não a `String` do Arduino.** Para `validate()` dá no
  mesmo: a função só chama `length()`, e nos dois tipos isso conta bytes do buffer. Deixa de
  dar no mesmo se o domínio crescer e passar a depender de conversão implícita ou de
  semântica de cópia — aí verde no host para de significar verde na placa.

O `build_src_filter = +<domain/>` é o que sustenta a fronteira: se um arquivo de `domain/`
voltar a incluir hardware, `pio test -e native` quebra antes de o conceito quebrar calado.

**Fechado em 19/09/2026:** `voltageToPercent()` saiu do `main.cpp` para
[domain/battery.cpp](../src/domain/battery.cpp) junto com o débito 18 e ganhou sete testes
nativos. As duas funções puras que o projeto tinha estão cobertas; o env nativo deixou de
ter ponta solta.

## 20. Porta serial de um adaptador específico versionada no `platformio.ini` — RESOLVIDO em 19/09/2026

**Onde:** [platformio.ini](../platformio.ini) — `upload_port` e `monitor_port`

Os dois apontam para `/dev/serial/by-id/usb-1a86_USB_Single_Serial_58EF052375-if00`. O
caminho `by-id` resolve um problema real e está bem justificado no comentário do arquivo
(o número do `ttyACM` muda entre replugues), mas o serial `58EF052375` é de **um** adaptador
físico: qualquer segunda placa ou segunda máquina precisa editar um arquivo rastreado pelo
git para conseguir gravar, e essa edição depois aparece como sujeira em todo `git status`.

**Ação:** aceitar `${sysenv.ESP_PORT}` com o valor atual como fallback, ou mover os dois
para um `platformio_override.ini` ignorado pelo git. Enquanto houver uma placa e uma
máquina, é atrito zero — o débito existe para não custar uma hora de confusão quando
aparecer a segunda.

**Resolvido:** das duas opções, a de `sysenv` não serve — só resolve metade. `upload_port`
tem `sysenvvar="PLATFORMIO_UPLOAD_PORT"` na tabela de opções do PlatformIO 6.2
(`platformio/project/options.py`), e `monitor_port` não tem nenhum. Conferido na prática:
com as duas variáveis exportadas, `pio project config` mostra o `upload_port` da variável e
o `monitor_port` do arquivo. Quem usasse a variável gravaria na placa certa e abriria o
monitor na errada — pior que o problema original, porque falha em silêncio.

O escolhido foi o `platformio_override.ini`, ignorado pelo git e carregado por
`extra_configs` no `[platformio]`. O `extra_configs` resolve o valor com `glob.glob()`
(`platformio/project/config.py`), e glob de caminho literal inexistente devolve lista vazia
sem erro — por isso o arquivo é opcional e o clone limpo continua funcionando com o padrão
versionado. Segunda máquina copia as duas linhas para lá e nunca mais suja o `git status`.

Não fecha: o valor versionado continua sendo o serial de um adaptador específico, agora como
default explícito em vez de única opção. Quem clonar sem criar o override e tiver outro
adaptador ainda vê o upload falhar — a diferença é que o comentário do `platformio.ini`
agora diz o que fazer, em vez de o arquivo parecer universal.

## 21. Referências de linha deste arquivo saem de sincronia sem aviso — RESOLVIDO em 19/09/2026

**Onde:** este arquivo — débitos 1 e 3

Os links com número de linha envelhecem silenciosamente conforme o código anda:

- Débito 1 apontava `src/main.cpp:11` para `VOLTAGE_DIVIDER_RATIO`, que na data em que isto
  foi escrito já estava na linha 22.
- Débito 3 apontava `src/main.cpp:51-60` como a leitura de bateria; a faixa já era o meio da
  tabela da curva de descarga, e `readBatteryVoltage()` estava em 62-71.

(Ambos os trechos saíram do `main.cpp` em 19/09/2026 — ver o registro no fim desta entrada.)

Os dois ainda são encontráveis pelo nome do símbolo, então o custo hoje é pequeno. Mas este
arquivo é o mecanismo de memória do projeto entre fases, e referência errada gasta confiança
justamente de quem chega sem contexto.

**Ação:** revisar as referências de linha ao fechar cada fase, junto com a atualização do
`PLANO_ROTEADOR.md`. Alternativa mais durável: citar símbolo em vez de linha
(`main.cpp` → `readBatteryVoltage()`), que não envelhece — mas perde o link clicável.

**Parcial, 18/09/2026:** fechar os débitos 15, 16 e 17 deslocou as linhas dos três arquivos
que eles citavam, exatamente o efeito descrito aqui. As referências dessas entradas passaram
a citar símbolo (arquivo + nome da função), sem número.

**Parcial, 19/09/2026:** as duas referências que o texto acima mede — débitos 1 e 3 —
deixaram de existir: o código que elas apontavam saiu do `main.cpp` na extração da bateria,
e as entradas passaram a citar arquivo e símbolo. Pelo critério declarado, este débito
estaria fechado.

**Não está, e é o próprio ponto:** a mesma extração deslocou outras três referências
(débitos 18, 19 e o `main.cpp:120` do débito 15), corrigidas na mesma passada. Duas
revisões seguidas produziram o mesmo defeito — o débito não é sobre as linhas erradas de
hoje, é sobre o formato que as produz. Sobrou número de linha apontando para código do
projeto em três entradas — 5, 14 e 16 —, nenhuma verificada nesta passada; a 22 saiu ao ser
fechada em 19/09/2026, de novo pelo mesmo efeito. As citações
de fonte externa (core Arduino, lwIP, `Preferences`) nos débitos 5, 13, 15 e 16 ficam como
estão: apontam para versão instalada de dependência, não para código que este repositório
move.

**Ação revisada:** ou converter as restantes para símbolo de uma vez e proibir número novo,
ou aceitar o formato e checar tudo ao fechar cada fase. Continuar corrigindo por encontro
é o que já se mostrou não funcionar.

**Resolvido em 19/09/2026 pelo primeiro caminho.** As três restantes viraram símbolo:
débito 5 → `kMaxClients`, débito 14 → `begin()`, débito 16 → `save()`. Detalhe que vale
registrar: as três *estavam certas* no momento da conversão — `wifi_ap.cpp:14` de fato era
`kMaxClients`. Não foram convertidas por estarem erradas, foram convertidas porque acertar
hoje não diz nada sobre amanhã, e a única evidência que este débito acumulou é que o formato
erra sozinho.

A proibição de número novo está em [AGENTS.md](../AGENTS.md), em "Regras de código" — sem
isso a conversão dura até a próxima entrada escrita no automático. Varredura final:
`grep -rE '\(\.\./[^)]*:[0-9]+\)' docs AGENTS.md` não devolve nada que aponte para código
deste repositório. Sobra uma linha em `docs/DEBITOS_TECNICOS.md` com `WebServer.cpp:638`,
que é log colado do core Arduino, não referência.

Fica fora do fechamento o que este débito nunca cobriu: referências de linha para fonte
externa (débitos 5, 13, 15 e 16 citam core Arduino, lwIP e `Preferences`) continuam com
número, porque apontam para versão instalada de dependência — não é código que este
repositório move.

## 22. Estado do uplink é exposto pelo supervisor e ninguém consome — RESOLVIDO em 19/09/2026

**Onde:** [src/infra/link_supervisor.h](../src/infra/link_supervisor.h) — `state()` e
`consecutiveFailures()` (substituídos por `status()` em 19/09/2026)

Os dois acessores públicos não têm chamador fora da própria classe. Confirmado por busca em
`src/` e `include/`: as únicas referências a `linkSupervisor` no `main.cpp` são `begin()` e
`applySettings()`.

O comentário ao lado de `state_`/`consecutiveFailures_` afirma o
contrário — "Lida pela task do loop() via state()/consecutiveFailures()" — e é essa leitura
cruzada que justifica o `volatile` nos dois membros. A justificativa está correta como
raciocínio e descreve um consumidor que não existe. Quem chegar aqui vai procurar a leitura
no `loop()` e não encontrar.

**O sintoma real não é o código morto.** É que a página de configuração não mostra estado
nenhum do uplink. Quem está associado ao AP sem internet não consegue distinguir:

- `Connecting` — tentativa em andamento, esperar resolve
- `Backoff` — já falhou, vai tentar de novo em até 60 s
- orçamento de reinício esgotado — causa externa (SIM, cobertura, crédito), esperar não
  resolve nada

Os dois primeiros saem de `state()` e `consecutiveFailures()`. **O terceiro não sai de
lugar nenhum:** a condição é `gRebootsWithoutUplink >= kMaxRebootsWithoutUplink`, e
`gRebootsWithoutUplink` é variável de namespace anônimo em
[link_supervisor.cpp](../src/infra/link_supervisor.cpp) sem acessor — só aparece no
`Serial.printf` da transição. É justamente o estado mais útil de mostrar, porque é o único
em que esperar não adianta.

Tudo isso só sai pelo serial hoje. Em campo não há serial — e a página de configuração, que
é o único canal que sobra, é onde a pessoa vai olhar primeiro quando a internet cai.

**Ação:** expor o estado na página de configuração (`GET /`), o que torna o comentário do
header verdadeiro e o `volatile` necessário de fato. Note que os dois acessores existentes
não bastam — o orçamento de reinício precisa de um terceiro, e ele é `RTC_NOINIT_ATTR` lido
e escrito só pela task do supervisor, então vale a mesma análise de concorrência que
justificou o `volatile` nos outros dois.

A alternativa honesta, se a decisão for não mostrar status, é remover os acessores e o
trecho do comentário que fala da leitura pelo `loop()` — mas aí o `volatile` perde a
justificativa registrada e precisa de outra.

Relacionado: o PRD 07 cita "histórico de quedas do uplink" como consumidor do relógio. Um
histórico pressupõe que o estado corrente já apareça em algum lugar; esta é a peça anterior.

**Resolvido:** o `GET /` passou a mostrar um bloco "Uplink 4G" acima do formulário, e o
comentário do header virou verdade — o `volatile` tem consumidor real agora.

O texto é regra, não formatação, então mora no domínio:
[domain/uplink_status.h](../src/domain/uplink_status.h) define `UplinkState` e
`UplinkStatus`, e `describeUplinkStatus()` traduz o estado na frase que responde a única
pergunta de quem está sem internet — **esperar resolve?**. Nove testes nativos amarram essa
distinção, não a redação.

O terceiro estado, que o débito apontava como o mais útil e o único sem acessor nenhum,
agora sai: `status()` calcula `reboot_budget_exhausted` lendo `gRebootsWithoutUplink`, que
virou `volatile` pela mesma análise que já justificava o `volatile` nos outros dois campos.
A condição não é só "acabaram os reinícios" — é isso **e** falhas suficientes para
justificar mais um. Sem a segunda metade, uma queda nova depois de dois reinícios gastos
cairia na mensagem de causa externa logo na primeira falha, antes das dez tentativas que
costumam resolver.

Um quarto caso apareceu ao montar isso, e não estava no débito: depois de um reinício
disparado pelo próprio supervisor, `consecutiveFailures_` volta a zero — é membro da
classe e não atravessa o `esp_restart()`, enquanto o contador de reinícios atravessa. A
página diria "conectando pela primeira vez" logo depois de a placa ter reiniciado por falta
de uplink. Mesmo defeito que o débito descreve no caso do orçamento esgotado: sugerir
progresso que não existe. `UplinkStatus` ganhou `rebooted_for_uplink` e um teste.

Os dois acessores mortos foram removidos em vez de ganharem um chamador cada. Quem lê a
página quer a situação inteira; devolver as partes soltas foi o que produziu dois acessores
sem consumidor. Ficou um: `status()`.

O `#ifdef ARDUINO` que troca `String` por `std::string` saiu do `router_settings.h` para
[domain/string_type.h](../src/domain/string_type.h) — a segunda entidade a precisar dele
era a ocorrência que justificava extrair. Foi junto um `numberToString()`, porque
`String += int` existe no Arduino e não em `std::string`: escrever direto compilaria na
placa e quebraria no host, que é o tipo exato de divergência que o seam existe para pegar.

**Em aberto, registrado de propósito:**

- `status()` **não é instantâneo atômico.** Os três campos são lidos um a um e a task do
  supervisor pode avançar no meio. Aceito: alimenta um texto que o navegador já lê com
  atraso, e o pior caso é uma contagem de falhas um ciclo velha. Deixa de ser aceitável se
  alguém decidir algo com base nesse retorno.
- **A página não se atualiza sozinha.** Sem `<meta refresh>` nem polling: quem quiser o
  estado novo recarrega. Um refresh automático a cada N segundos custa uma linha, e custa
  também um `GET /` autenticado por cliente por N segundos num servidor single-threaded
  que divide o `loop()` com a leitura de bateria.
- **`escapeForHtmlAttribute` é aplicado a um texto que hoje não precisa** — são literais
  nossos e um número. Fica no caminho para o dia em que a mensagem incluir um valor
  gravado.

## 23. O caminho de OTA só fala no serial quando o gravador falha

**Onde:** [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) —
`handleUpdateUpload()` e `handleUpdateDone()`

As três linhas `Serial.printf("OTA: …")` cobrem só falha do `Update`: começo recusado,
escrita curta e ativação do slot. Não existe linha nenhuma para **sucesso**, nem para as
recusas do domínio (`inspectImageHead`/`inspectImageSize`) — arquivo que não é imagem do
ESP32, arquivo curto demais, arquivo maior que o slot e formulário vazio são respondidos na
página e somem.

**Por que incomoda:** quem está com o cabo na mão e não com o celular na página não
consegue distinguir "a placa recusou o arquivo" de "a requisição nunca chegou". Isso
apareceu na validação de 20/09/2026: o teste do arquivo de lixo teve que ser refeito porque
o navegador abortou o POST antes de enviá-lo (`ERR_UPLOAD_FILE_CHANGED`), e o serial não
tinha como mostrar a diferença — em ambos os casos, silêncio.

**Correção:** uma linha no `handleUpdateDone()` com o veredito e o tamanho recebido, nos
dois desfechos. Barato; ficou de fora porque a fase fechou antes. Cuidado ao fazer: o débito
13 já afoga o serial sob tráfego, então a linha tem que ser uma por requisição, não por
bloco recebido.

## 24. A página só é alcançável de dentro do AP

**Onde:** [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) —
`begin()`; [docs/ATUALIZACAO_EM_PRODUCAO.md](ATUALIZACAO_EM_PRODUCAO.md)

O OTA pela página tirou o cabo serial do caminho, e é fácil ler isso como "dá para atualizar
a unidade de qualquer lugar". Não dá. O `WebServer` escuta em `0.0.0.0` e em tese atenderia
pelo PPP, mas dados móveis saem por CGNAT: não há porta alcançável da internet. O que o OTA
eliminou foi o cabo e o notebook, não a viagem.

**Por que incomoda:** o custo de qualquer intervenção continua sendo uma visita por unidade,
e é esse número que decide se vale corrigir um defeito pequeno. Planejar em cima de
"atualização remota" que na verdade é presencial erra o custo de todo o roadmap.

**Direção escolhida (21/09/2026): acesso remoto à própria página.** Não um canal só de
firmware. A página já faz status, configuração e atualização; alcançá-la de fora resolve as
três de uma vez, e o procedimento de campo continua sendo o mesmo que já foi testado — muda
só de onde o operador abre o navegador.

A alternativa considerada era a placa consultar um servidor por uma imagem assinada e gravar
sozinha. É mais barata e mais segura, mas cobre só firmware: continuaria exigindo visita para
ler status ou corrigir um APN errado, que são justamente os casos mais comuns. Fica
registrada aqui como comparação, não como recomendação concorrente.

**O que a fase vai ter que resolver, e nenhum é pequeno:**

- **Entrada não existe.** Sob CGNAT a conexão tem que partir da placa e ficar de pé: túnel
  reverso persistente contra um servidor nosso com IP público. Isso é um componente novo na
  placa e uma VPS a manter
- **Hoje é HTTP puro.** O Basic Auth manda usuário e senha em base64, que é reversível.
  Dentro do AP isso já é discutível; exposto à internet é senha em claro. Ou o túnel cifra
  (WireGuard resolve), ou entra TLS na placa — e o handshake do mbedTLS custa dezenas de KB
  de heap num ESP32 que já roda PPP, NAT, DNS e o servidor
- **Dado móvel é pago e a subida é lenta.** Um túnel permanente gasta keepalive o tempo
  todo, e cada atualização sobe ~1 MB pelo pior sentido do enlace
- **Identidade por unidade.** O túnel precisa saber qual placa é qual, o que esbarra no
  provisionamento — hoje cada unidade sorteia o próprio segredo no primeiro boot e ninguém
  guarda isso em lugar nenhum
- **Inventário.** Com as unidades se conectando a um servidor, saber o que roda em cada uma
  deixa de exigir visita. É consequência da fase, não trabalho extra
- **Assinatura da imagem.** Sem secure boot, quem chegar na página grava qualquer firmware.
  Hoje isso está atrás do AP; numa via remota o Basic Auth passa a ser a única barreira

**Enquanto não existir**, o procedimento de campo declara o alcance real logo no início, em
vez de deixar a limitação implícita.

## 25. Nada impede um binário de árvore suja de ir para campo

**Onde:** [platformio.ini](../platformio.ini); [docs/ATUALIZACAO_EM_PRODUCAO.md](ATUALIZACAO_EM_PRODUCAO.md)

A versão que o firmware reporta na página sai do `git describe`, e com a árvore suja ela vira
`<hash>-dirty`. Esse sufixo não identifica código nenhum: não diz o que estava modificado,
então a imagem não é reproduzível a partir do repositório.

**Por que incomoda:** o sintoma aparece tarde e longe. Uma unidade em campo rodando `-dirty`
só vira problema quando alguém precisa reproduzir um defeito dela, e aí não há de onde
partir. Aconteceu nesta bancada: a placa passou boa parte do dia 20/09/2026 reportando
`88d4901-dirty` sem que isso chamasse atenção de ninguém.

**Correção:** o build do env de release recusa árvore suja, ou ao menos grita — um
`extra_scripts` de pre-build conferindo `git status --porcelain` resolve. Não fazer isso no
env de desenvolvimento, onde build sujo é o caso normal e travar seria atrito puro.

Por enquanto a disciplina é manual e está escrita no procedimento de campo, que começa por
`git status --porcelain` ter que sair vazio.
