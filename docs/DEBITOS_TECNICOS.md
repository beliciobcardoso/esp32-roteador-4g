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
  `startRouting()` para no SoftAP ([src/main.cpp:120](../src/main.cpp:120)). Como
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

## 16. `NvsSettingsRepository::save()` sempre reporta sucesso

**Onde:** [src/adapters/nvs_settings_repository.cpp:45-61](../src/adapters/nvs_settings_repository.cpp:45)

A função termina em `return true` fixo e não checa nenhum dos oito retornos que a
`Preferences` oferece. A interface promete o contrário —
"true se a gravação foi bem-sucedida"
([settings_repository.h:14](../src/adapters/settings_repository.h:14)) — e o HTTP responde
"Configuração salva." mesmo sem nada ter sido gravado.

**Confirmado por leitura** (`libraries/Preferences/src/Preferences.cpp`):

- `begin()` (linhas 33-56) devolve `false` se `nvs_open` falhar, e também se a instância já
  estiver aberta.
- `putString()` (linhas 267-283) devolve `0` se `nvs_set_str` **ou** `nvs_commit` falhar —
  partição cheia (`ESP_ERR_NVS_NOT_ENOUGH_SPACE`) cai exatamente aí.

**Cuidado na correção:** `putString()` devolve `strlen(value)`, então uma gravação
bem-sucedida de string vazia também devolve `0`. Como `apn_user` e `apn_password` são
opcionais e podem ser vazios de propósito
([router_settings.h:10-14](../src/domain/router_settings.h:10)), testar `> 0` em todos os
campos criaria falso negativo justamente no caso legítimo. O critério tem que distinguir
"gravou vazio" de "não gravou".

**Ação:** propagar o retorno do `begin()` e checar os `putString`/`putInt`/`putBool` com um
critério que tolere campo opcional vazio. Vira mais relevante na Fase 7, que sobe o schema
para 3 e reescreve todos os campos de uma vez.

## 17. Valores da configuração vão para o HTML sem escape

**Onde:** [src/adapters/http_config_handler.cpp:35-39](../src/adapters/http_config_handler.cpp:35)

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

## 18. Lógica de bateria mora no `main.cpp`, contra a regra do próprio AGENTS.md

**Onde:** [src/main.cpp:30-88](../src/main.cpp:30)

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

## 19. Nenhum teste automatizado, e nenhum ambiente onde rodar um — RESOLVIDO em 18/09/2026

**Onde:** [platformio.ini](../platformio.ini) — não há env `native`; não há diretório `test/`

O projeto adotou Clean Architecture explicitamente para isolar regra de negócio de
hardware, e hoje existem duas funções puras que essa escolha tornou testáveis sem placa:
`validate()` ([router_settings.cpp](../src/domain/router_settings.cpp)) e
`voltageToPercent()` ([main.cpp:73](../src/main.cpp:73), assim que sair do `main` — débito
18). Nenhuma das duas tem teste, e não há ambiente configurado para executar um.

O retorno prático disso é concreto: o débito 15 é um caso de limite em `validate()`, e um
teste de limite o pegaria em segundos, sem hardware e sem depender do comportamento do
driver.

**Ação:** env `native` no `platformio.ini` com os testes de `validate()` — incluindo os
limites do débito 15, uma vez decididos. Escopo deliberadamente pequeno: só o que é puro.
Testar `infra/` exigiria mock de ESP-IDF e não se paga aqui.

**Resolvido:** `[env:native]` no [platformio.ini](../platformio.ini) e 11 testes Unity em
[test/test_router_settings/test_router_settings.cpp](../test/test_router_settings/test_router_settings.cpp),
rodando por `pio test -e native`. Cobrem `validate()` inteira — cada código de erro, os dois
limites de 8 caracteres pelos dois lados, a precedência entre campos inválidos, e o caso de
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

**Em aberto:** `voltageToPercent()` continua sem teste porque continua dentro do `main.cpp`
(débito 18). Extrair para `domain/` e testar é o mesmo trabalho.

## 20. Porta serial de um adaptador específico versionada no `platformio.ini`

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

## 21. Referências de linha deste arquivo saem de sincronia sem aviso

**Onde:** este arquivo — débitos 1 e 3

Os links com número de linha envelhecem silenciosamente conforme o código anda:

- Débito 1 aponta [src/main.cpp:11](../src/main.cpp:11); `VOLTAGE_DIVIDER_RATIO` está hoje
  na linha 22.
- Débito 3 aponta `src/main.cpp:51-60` como a leitura de bateria; essa faixa hoje é o meio
  da tabela da curva de descarga, e `readBatteryVoltage()` está em 62-71.

Os dois ainda são encontráveis pelo nome do símbolo, então o custo hoje é pequeno. Mas este
arquivo é o mecanismo de memória do projeto entre fases, e referência errada gasta confiança
justamente de quem chega sem contexto.

**Ação:** revisar as referências de linha ao fechar cada fase, junto com a atualização do
`PLANO_ROTEADOR.md`. Alternativa mais durável: citar símbolo em vez de linha
(`main.cpp` → `readBatteryVoltage()`), que não envelhece — mas perde o link clicável.

## 22. Estado do uplink é exposto pelo supervisor e ninguém consome

**Onde:** [src/infra/link_supervisor.h:38-41](../src/infra/link_supervisor.h:38) —
`state()` e `consecutiveFailures()`

Os dois acessores públicos não têm chamador fora da própria classe. Confirmado por busca em
`src/` e `include/`: as únicas referências a `linkSupervisor` no `main.cpp` são `begin()` e
`applySettings()`.

O comentário em [link_supervisor.h:56-58](../src/infra/link_supervisor.h:56) afirma o
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
