# Débitos Técnicos

## 1. `VOLTAGE_DIVIDER_RATIO` hardcoded e calibrado por placa

**Onde:** [src/main.cpp:11](../src/main.cpp:11)

Constante `2.19` calibrada com multímetro numa placa específica (16/09). Resistores do divisor variam por tolerância/placa — ratio não é universal.

**Ação:** mover para `config.h` (já previsto no plano, Fase 1) como default de fábrica, sobrescrevível via NVS/config, em vez de `#define` fixo no firmware.

## 2. Perda de precisão silenciosa em `voltageToPercent`

**Onde:** [src/main.cpp:73](../src/main.cpp:73)

`return p2 + frac * (p1 - p2);` retorna `float` em função `int` — trunca sem aviso. Funciona pra exibição de %, mas não está documentado como intencional (podia arredondar com `round()` ou já declarar o intuito no comentário).

**Ação:** decidir explicitamente — arredondar (`round()`) se quiser %, ou mudar assinatura pra `float` se precisão importar em algum consumidor futuro (ex: log/telemetria).

## 3. Sem suavização entre ciclos de leitura de bateria

**Onde:** [src/main.cpp:51-60](../src/main.cpp:51)

Cada `loop()` reamostra do zero (`NUM_SAMPLES` leituras), sem média móvel ou filtro entre ciclos anteriores. Ruído do ADC pode causar variação de % perceptível entre prints consecutivos.

**Ação:** avaliar filtro exponencial (EMA) entre leituras se oscilação incomodar na UI final; por ora é decisão aceita pro protótipo, não bug.

## 4. `loop()` bloqueia `httpConfigHandler.handleClient()` por ~3s por ciclo — RESOLVIDO na Fase 6

**Onde:** [src/main.cpp](../src/main.cpp) — `loop()`

`delay(500)` x2 (blink LED) + `delay(2000)` (intervalo de leitura de bateria) somavam ~3s de bloqueio síncrono antes do próximo `handleClient()`.

**Resolvido:** blink e leitura de bateria agora são agendados por `millis()` (`blinkLed()` /
`reportBattery()`), e o `loop()` só encadeia checagens de prazo. Restou um bloqueio de
~100ms a cada 3s dentro de `readBatteryVoltage()` (`NUM_SAMPLES` x `delay(5)`) — mantido
de propósito, é a média que tira o ruído do ADC e não atrapalha nem o HTTP nem a
supervisão do modem.

## 5. Limite de clientes do AP prometia 20, teto do driver é 15

**Onde:** [src/infra/wifi_ap.cpp:14](../src/infra/wifi_ap.cpp:14)

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

## 6. Rollback de OTA ainda desabilitado

**Onde:** [sdkconfig.defaults](../sdkconfig.defaults), [partitions.csv](../partitions.csv)

A tabela de partições já tem os dois slots (`app0`/`app1`) e `otadata`, mas
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` está intencionalmente desligado: sem um cliente OTA
que chame `esp_ota_mark_app_valid_cancel_rollback()`, ligar o rollback faria o bootloader
reverter toda imagem nova como se tivesse falhado.

**Ação:** habilitar junto com a implementação do cliente OTA — os dois são indissociáveis.

## 7. Partição `spiffs` reservada mas não montada

**Onde:** [partitions.csv](../partitions.csv)

256 KB em `0x3C0000` reservados e nunca montados. Custo zero hoje (é só endereço não usado),
mas é espaço parado se nada for escrito ali.

**Ação:** decidir na Fase 6 — montar para logs/telemetria persistente, ou devolver o espaço
aos slots de app.

## 8. `infra/modem_ppp` escreve diagnóstico direto no `Serial`

**Onde:** [src/infra/modem_ppp.cpp](../src/infra/modem_ppp.cpp)

Toda saída de diagnóstico usa `Serial.printf` direto. Funciona e foi essencial na
depuração, mas acopla a camada de infra ao transporte de log — não dá para redirecionar
para a página web, um buffer em RAM ou telemetria sem reescrever as chamadas.

**Ação:** avaliar uma abstração de log quando houver um segundo consumidor real. Hoje há
apenas um; criar a abstração agora seria abstração especulativa.

## 9. Cliente que associa antes do PPP subir recebe DNS inútil

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

## 10. Credenciais de fábrica em claro, NVS sem criptografia, admin sobre HTTP puro

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

## 12. Janela de DHCP fechada a cada reconexão

**Onde:** [src/infra/nat_bridge.cpp](../src/infra/nat_bridge.cpp) — `offerDnsToApClients()`

O DNS da operadora só entra na opção 6 do DHCP com o servidor parado
(`esp_netif_dhcps_option` devolve `ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED` se estiver
rodando), então cada reconexão faz `dhcps_stop` → `set_dns_info` → `dhcps_option` →
`dhcps_start`. Clientes já associados mantêm o lease, mas um cliente que peça DHCP
exatamente nessa janela falha e precisa tentar de novo.

**Ação:** aceito. A janela é de milissegundos e o cliente DHCP retenta sozinho. Some junto
com o débito 9, se o forwarder de DNS local for implementado — aí o DNS entregue passa a
ser sempre `192.168.4.1` e não precisa mais mudar entre sessões.


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

## 14. Requisição a rota não registrada vira log de erro

**Onde:** [src/adapters/http_config_handler.cpp:14-15](../src/adapters/http_config_handler.cpp:14)

Só `/` está registrado (GET e POST) e não há `onNotFound`. Qualquer `/favicon.ico` ou sonda
de portal cativo do Android cai no caminho default do `WebServer`, que loga como erro:

```
[E][WebServer.cpp:638] _handleRequest(): request handler not found
```

Benigno — o cliente recebe 404, nada quebra. O incômodo é o log: numa depuração de campo
essas linhas se misturam com erro de verdade.

**Ação:** `server_.onNotFound(...)` devolvendo 404 limpo, sem vazar detalhe interno. Duas
linhas. Não entrou na Fase 6 por ser fora do escopo dela.
