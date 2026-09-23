# Plano — Firmware Roteador 4G (ESP32 + A7670E)

Decisões já fechadas, que este plano assume como dadas:

- Framework: PlatformIO, `espidf, arduino` híbrido (já migrado e validado)
- Conectividade: PPP via modem A7670E + NAT (NAPT) entre WiFi AP e a interface PPP — roteamento transparente, não proxy
- Autenticação da página de config: HTTP Basic Auth simples
- Acesso à página de config: IP fixo (sem portal cativo)
- Persistência de credenciais: NVS sem criptografia
- Até 15 clientes WiFi simultâneos (teto do driver `ESP_WIFI_MAX_CONN_NUM`)
- WiFi AP: WPA2-PSK (`WIFI_AUTH_WPA2_PSK`) — WPA3 em softAP exige IDF 5.x,
  indisponível no ESP32 clássico com IDF 4.4 (ver AGENTS.md)

## Por que esp_modem em vez de TinyGSM

TinyGSM opera em modo "AT command relay" — abre sockets TCP/HTTP para o próprio ESP32, não cria uma interface de rede IP compartilhável. Para o ESP32 rotear tráfego de terceiros (qualquer dispositivo no WiFi AP navegando através do modem), é necessário que o modem apareça como uma interface de rede real — isso é o que o PPP faz.

`esp_modem` é o componente oficial do ESP-IDF para isso: estabelece uma sessão **PPPoS** com o modem, expõe uma interface `esp_netif` PPP, e essa interface entra no mesmo pool de roteamento/NAT que a interface WiFi AP.

Consequência prática: o código da Etapa 1-3 anterior (inicialização do modem via TinyGSM, comandos AT manuais) **não é reaproveitado** — a comunicação com o modem recomeça do zero em modo PPP.

## Estrutura de arquivos proposta

Seguindo Clean Architecture (regra de negócio isolada de infra/framework):

```
esp32-roteador-4g/
├── platformio.ini
├── sdkconfig.defaults
├── components/
│   └── esp_modem/              # componente ESP-IDF oficial, não é lib Arduino
├── include/
│   └── config.h                # constantes de pinagem, defaults de fábrica
├── src/
│   ├── main.cpp                 # orquestração: chama setup de cada módulo, nada de lógica aqui
│   │
│   ├── domain/                  # ENTIDADES — regras puras, sem dependência de hardware/framework
│   │   ├── router_settings.h    # struct: ssid, senha wifi, apn, usuario/senha admin
│   │   └── router_settings.cpp  # validação de formato (ex: ssid não vazio, senha >= 8 chars)
│   │
│   ├── usecases/                # CASOS DE USO — orquestram entidades, chamam interfaces de infra
│   │   ├── load_settings.h/.cpp     # lê config salva ou aplica defaults de fábrica
│   │   ├── save_settings.h/.cpp     # valida e persiste config nova
│   │   └── start_routing.h/.cpp     # sobe AP + PPP + NAT, nessa ordem
│   │
│   ├── adapters/                 # ADAPTADORES — implementam interfaces que usecases dependem
│   │   ├── settings_repository.h     # interface abstrata (o usecase depende disso, não do NVS)
│   │   ├── nvs_settings_repository.h/.cpp  # implementação concreta usando NVS
│   │   ├── http_config_handler.h/.cpp      # rotas HTTP, parsing de formulário, Basic Auth
│   │   └── html_page.h                     # página HTML embutida (const char*)
│   │
│   └── infra/                    # INFRA — wrappers finos sobre APIs do ESP-IDF/Arduino
│       ├── wifi_ap.h/.cpp            # sobe SoftAP, aplica IP fixo
│       ├── modem_ppp.h/.cpp          # inicializa esp_modem, sobe PPPoS
│       └── nat_bridge.h/.cpp         # habilita NAPT entre as duas interfaces esp_netif
│
└── docs/
    ├── SETUP.md                  # já existe
    └── PLANO_ROTEADOR.md          # este arquivo
```

**Regra de dependência:** `domain` não importa nada de `infra` ou `adapters`. `usecases` depende só de interfaces (`adapters/settings_repository.h`), nunca da implementação concreta (`nvs_settings_repository.cpp`) diretamente — a injeção acontece no `main.cpp`.

## Fluxo de dados da configuração

```
Usuário abre 192.168.10.1
   → http_config_handler (adapters) exige Basic Auth
   → GET: renderiza html_page com valores atuais (via load_settings usecase)
   → POST: valida payload → save_settings usecase → valida via domain/router_settings
                                                    → grava via nvs_settings_repository
   → aplica: reinicia wifi_ap e/ou modem_ppp com os novos valores
```

## Ordem de implementação (fases)

### Fase 1 — Storage (NVS) — ✅ concluída
- `domain/router_settings` (struct + validação)
- `adapters/settings_repository` (interface)
- `adapters/nvs_settings_repository` (implementação)
- `usecases/load_settings`, `usecases/save_settings`
- Teste isolado: gravar e ler de volta via serial, sem rede nem HTTP ainda
- **Validado em campo:** grava defaults, relê após reboot, persistência confirmada
- Evoluiu na Fase 4: schema NVS versionado (v2) para acomodar `apn_user`/`apn_password`
  sem deixar placas já gravadas subirem com campos vazios

### Fase 2 — WiFi AP — ✅ concluída (com ressalvas)
- `infra/wifi_ap`: sobe SoftAP com SSID/senha vindos do `load_settings`, IP fixo (ex: `192.168.10.1`)
- Teste: conectar um celular no AP e confirmar que recebe IP por DHCP
- **Validado em campo:** celular conecta e recebe IP por DHCP
- ⚠️ Dois critérios originais não se sustentaram, descobertos na Fase 4:
  - WPA2/WPA3 misto é inalcançável no ESP32 clássico com IDF 4.4 — revisado para WPA2-PSK
  - "até 20 clientes" não é cumprido: o driver corta em 10 (chaves do ESP-NOW).
    Ver débito 5 em [DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md); validar na Fase 6

### Fase 3 — Servidor de configuração HTTP — ✅ concluída
- `adapters/html_page`: formulário simples (SSID, senha WiFi, APN, usuário/senha admin)
- `adapters/http_config_handler`: rotas GET/POST, Basic Auth, chama `save_settings`
- Teste: acessar `192.168.10.1` do celular conectado no AP, editar e salvar configs, confirmar persistência após reboot
- **Validado em campo:** GET renderiza o formulário, POST persiste, configs sobrevivem ao reboot
- Evoluiu na Fase 4: formulário ganhou `apn_user`/`apn_password` (senha em branco mantém a atual)
- ⚠️ `loop()` bloqueia `handleClient()` por ~3 s por ciclo — débito 4 em [DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md)

### Fase 4 — Modem PPP — ✅ concluída
- `infra/modem_ppp`: integra `esp_modem`, sequência de power-on do A7670E (PWRKEY), sobe PPPoS com o APN salvo
- Teste: confirmar que a interface PPP recebe IP da operadora (log via serial)
- **Validado em campo:** registro LTE em ~3 s na Vivo, PAP aceito, IP e DNS da operadora atribuídos
- Percalços e diagnósticos descartados: [DEPURACAO_FASE_4_MODEM_PPP.md](DEPURACAO_FASE_4_MODEM_PPP.md)

### Fase 5 — NAT / roteamento — ✅ concluída
- `infra/nat_bridge`: habilita NAPT na interface **AP** (não na PPP — ver PRD 05)
  - Entregava também o DNS da operadora pela opção 6 do DHCP; desde 19/09/2026 não mexe
    mais no DHCP — quem resolve é o `infra/dns_forwarder` em `192.168.10.1:53` (PRD 09)
- Orquestração (settings → AP → modem → espera IP → NAT) ficou em `main.cpp`, não em
  `usecases/start_routing` — decisão registrada no PRD 05
- Validado em hardware: celular conectado no AP navegou pelo 4G
- ~~⚠️ Cliente que associa antes do PPP subir recebe DNS `192.168.4.1` e só resolve nome
  depois de renovar o lease (débito 9)~~ — resolvido em 19/09/2026 pelo DNS local
  ([prd/09-dns-local.md](prd/09-dns-local.md)) e validado em hardware: agora existe
  resolvedor nesse endereço, então o valor do lease nasce certo

### Fase 6 — Integração e reconexão automática — ✅ concluída
- `loop()` non-blocking: blink e leitura de bateria passam a ser agendados por `millis()`
  (fecha o débito 4). Sem isso, qualquer supervisão vivendo no `loop()` reagiria com até
  3 s de atraso e travaria o `handleClient()` no meio
- `infra/link_supervisor`: task FreeRTOS própria que conecta o 4G, detecta queda pelos
  eventos do PPP e reconecta com backoff (5→10→20→40→60 s), rearmando o NAT a cada sessão
  nova. Reboot após 10 falhas seguidas, no máximo 2 vezes — contador em `RTC_NOINIT_ATTR`,
  porque falha permanente (SIM fora, sem cobertura) reiniciaria a placa a cada ~12 min para
  sempre e derrubaria quem estivesse no AP
- `CONFIG_LWIP_ENABLE_LCP_ECHO=y` (3 s × 3 falhas): sem isso uma perda silenciosa de RF
  nunca gera evento e o roteador vira buraco negro com o PPP eternamente "up"
- `kMaxClients` corrigido de 20 para 15 — `ESP_WIFI_MAX_CONN_NUM` é teto do driver no
  ESP32 clássico, 20 nunca foi alcançável (débito 5 reescrito)
- Config a quente: mudar APN reconecta sozinho; mudar SSID/senha exige reboot e a página
  de configuração diz isso na resposta do POST
- **Carga com dispositivos reais está fora do escopo desta fase**
- **Validado em campo:** queda de RF recupera em ~16 s (LCP echo → `ERRORPEERDEAD`); troca
  de APN reconecta a quente; SIM removido percorre o backoff inteiro e reinicia na décima
  falha (`rst:0xc SW_CPU_RESET`). Durante a queda inteira o AP seguiu no ar — um cliente
  associou na oitava falha e recebeu DHCP com o 4G morto havia 9 min
- **Orçamento de reinícios validado em bancada:** o contador atravessou o `esp_restart()`
  (`reinicio 1/2` → `reinicio 2/2`) e o terceiro reboot foi suprimido — sem ele a placa
  reiniciaria para sempre. Depois disso o supervisor segue tentando com backoff de 60 s e o
  AP não cai mais sozinho
- ⚠️ Rajadas de `pppos_input_tcpip failed with -1` sob tráfego — débito 13 em
  [DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md)
- Acertos, erros e lições da fase: [prd/06-integracao-testes-carga.md](prd/06-integracao-testes-carga.md#retrospectiva-da-fase)

### Fase 7 — Relógio (NTP + fuso) — concluída, validada em hardware

- `infra/clock`: SNTP com `a.st1.ntp.br` e `pool.ntp.org`, sincronizado quando o
  `LinkSupervisor` entra em `Online` — primeira conexão e cada reconexão. `esp_sntp_init()`
  não é chamado duas vezes (guarda com `esp_sntp_enabled()`, e reconexão usa
  `sntp_restart()`)
- `CONFIG_LWIP_SNTP_MAX_SERVERS=2` no `sdkconfig.defaults`: o default é `1`, e com ele o
  segundo servidor é ignorado sem erro nenhum
- Campo `timezone` na NVS (`kCurrentSchema` 2→3, com degrau em
  `domain/settings_migration`) e `<select>` montado da tabela de `domain/timezone.h`,
  aplicado a quente. A tabela é domínio porque o `setenv("TZ", ...)`/`tsset()` do IDF não
  reclama de string sem sentido — o resultado seria hora errada em silêncio
- **Carona no mesmo degrau:** `battery_divider_ratio` virou configurável, fechando o
  débito 1. Faixa `[1.4, 10.0]` validada no domínio, aplicada a quente
- Fonte é NTP, não `AT+CCLK?`/NITZ — NITZ depende da operadora entregar
- **Consumidores da hora ficam fora do escopo**: histórico de quedas, agendamento de
  reboot, expiração de sessão e carimbo de OTA dependem disso, mas vêm depois. A página de
  config mostra o relógio só para dar como verificar que ele funciona
- **Validada em placa em 20/09/2026**, com um defeito encontrado e corrigido no caminho: o
  `systemClock.begin()` rodava antes do `esp_netif_init()` (que quem chama é o `wifiAp.start()`),
  e as funções do `esp_sntp` entram pela task tcpip do lwIP — a placa ficava em boot loop com
  `assert failed: tcpip_callback ... (Invalid mbox)`, sem nunca subir o AP. A chamada passou
  para dentro do `startRouting()`, depois do AP e antes do `linkSupervisor.begin()`
- 17 s do reset até a hora certa; troca de fuso valendo a quente (UTC−2 → UTC−3 sem reset
  entre as duas linhas de sincronização) e ressincronização após cada `ERRORPEERDEAD`, sete
  vezes nas capturas do dia. UDP 123 sai pelo NAT — o SNTP é o primeiro tráfego originado
  pela própria placa, e saiu de primeira
- Detalhes em [prd/07-relogio-ntp.md](prd/07-relogio-ntp.md)

### Fase 8 — Atualização de firmware pela página (OTA) — concluída, validada em hardware

- `POST /update` no mesmo `WebServer`: upload de `firmware.bin` pelo AP, gravado direto no
  slot livre pelo `Update` do Arduino. Sem arquivo intermediário e sem a partição `spiffs`
- **Basic Auth conferido no primeiro bloco do upload**, não no handler que responde: o
  `WebServer` chama o upload de dentro do `_parseForm()`, antes do handler de POST, e
  autenticar no fim gravaria ~900 KB no slot de quem não se identificou
- `domain/firmware_update`: exame raso da imagem em duas partes — magic `0xE9` no primeiro
  bloco, tamanho contra o slot no fim (o `WebServer` não expõe `Content-Length`). Quem
  valida de verdade é o `esp_image_verify()` dentro do `Update.end()`, que confere o
  SHA-256 da própria imagem — por isso não há campo de checksum no formulário
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, fechando o débito 6. A imagem nova é confirmada
  pelo `loop()` depois de **300 s de pé**, não no `setup()`: ali o rollback só pegaria o
  firmware que morre antes de o AP subir. O prazo tem teto — o `LinkSupervisor` pode
  reiniciar a placa após ~7 min por falta de sinal, e isso não pode disparar rollback
- **O core do Arduino cancelava o rollback antes do `setup()`.** O `initArduino()` chama
  `esp_ota_mark_app_valid_cancel_rollback()` quando `CONFIG_APP_ROLLBACK_ENABLE=y` — chave
  diferente da do bootloader, e ligada na configuração gerada. Sem sobrescrever o símbolo
  weak `verifyRollbackLater()`, a imagem chegava ao `loop()` já em `Valid` e o rollback
  inteiro era enfeite. Corrigido em `main.cpp`; conferir com
  `xtensa-esp32-elf-nm firmware.elf | grep verifyRollbackLater` (`T`, não `W`)
- Bloco de firmware no `GET /`: slot em execução, versão, data de compilação e estado da
  imagem, com o aviso de não reiniciar durante a janela de verificação
- O reboot sai do `loop()` e não do handler — `esp_restart()` lá dentro cortaria a resposta
  antes de ela sair do socket
- ⚠️ **A senha de admin passou a valer execução de código**, e a página continua em HTTP
  puro sobre PSK compartilhada. Aceitável em bancada, bloqueante para campo — registrado no
  débito 10 ao lado do TLS
- **Validada em placa em 20/09/2026:** upload real trocando de slot (`0x20000` ↔ `0x1f0000`),
  arquivo truncado recusado pelo `esp_image_verify()` sem reiniciar nada, formulário vazio
  recusado com a frase do domínio, e rollback de verdade — reset dentro da janela voltou de
  `0x1f0000` para `0x20000`, e a confirmação saiu em 300,9 s de uptime
- **Confirmação manual validada em placa em 20/09/2026.** A confirmação automática por
  tempo saiu: ficar de pé não prova que alguém consegue chegar na placa. Dois cenários
  exercidos com builds de teste propositalmente quebrados:
  - **`loop()` travado** (spin infinito): `task_wdt` disparou ~5 s depois, `Aborting.`,
    reboot, e o bootloader voltou de `0x20000` para `0x1f0000`. Sem
    `CONFIG_ESP_TASK_WDT_PANIC=y` o watchdog só imprimia o aviso e a placa ficava de pé,
    muda e inalcançável — este era o caminho que perdia o dispositivo
  - **SoftAP forçado a falhar**: `loop()` girou, o autocheck viu `ap_up = false` e o revert
    ativo saiu em **589 ms**, sem esperar o prazo — `esp_ota_ops: Rollback to previously
    worked partition. Restart.` Na primeira tentativa a placa tinha morrido antes disso,
    num `assert failed: tcpip_send_msg_wait_sem (Invalid mbox)`, porque o
    `httpConfigHandler.begin()` abria socket sem AP no ar; corrigido, e o mesmo caminho é
    alcançável em produção pela NVS ilegível
  - **Botão de confirmar**: upload subiu para `0x20000`, o botão apareceu na página com a
    imagem em `PendingVerify`, o clique saiu em `Firmware: confirmado pelo operador,
    rollback cancelado`, e um **power-on reset** depois a placa continuou em `0x20000` —
    reset por energia é mais forte que o reset por software que o critério pedia
  - **Prazo sem clique**: exercitado com um build de teste de 60 s. O revert saiu aos
    **61,25 s**, numa unica tentativa, e o bootloader voltou de `0x20000` para `0x1f0000`.
    A primeira rodada deste cenario falhou de dois jeitos e os dois viraram correcao: o
    `Revert` nao marcava a decisao como resolvida e repetiu 3679 vezes em dois minutos, e
    nao havia tratamento para `Rollback is not possible, do not have any suitable apps in
    slots` — a IDF recusa quando o outro slot nao tem imagem valida, que e onde uma
    sequencia de reverts deixa a placa
- ⚠️ Upload interrompido no meio (cabo/Wi-Fi) segue por validar — unico criterio aberto
- Detalhes em [prd/11-atualizacao-ota.md](prd/11-atualizacao-ota.md)

### Fase 9 — Telemetria MQTT e painéis no Grafana — em andamento (domínio concluído)

- A unidade publica o próprio estado num broker MQTT sobre TLS, **só de subida**, e esse
  estado vira série temporal de longo prazo na stack Prometheus + Grafana que já roda em
  produção. Ingestão por Telegraf (`mqtt_consumer`) e `remote_write`
- **Vem antes da Fase 10 de propósito.** As duas contornam o CGNAT pelo mesmo princípio — a
  conexão nasce na placa —, mas o túnel WireGuard põe uma terceira interface no mesmo lwIP
  que hoje faz NAT dos clientes do AP, e MQTT é um socket TCP de saída. Telemetria não sobe
  `firmware.bin` nem corrige um APN: reduz o que sobra para o túnel, e entrega antes
- `domain/telemetry`, `domain/telemetry_buffer` e `domain/mqtt_backoff` — payload, política
  do anel, correção de timestamp e aritmética do backoff, todos com teste nativo.
  `infra/mqtt_client` é wrapper fino sobre o `esp_mqtt_client` que já vem no IDF 4.4.7
- **A métrica que importa é a que não pode ser enviada.** Uplink caído é o evento que se quer
  ver, e é exatamente quando não há publicação possível. Três mecanismos, os três
  necessários: LWT retido, anel em `RTC_NOINIT` drenado na reconexão, e alerta por ausência
- O anel tem **cabeçalho versionado** (`magic` + `layout` + `sample_size`). Sem ele, o
  firmware novo leria depois de um OTA o layout antigo no mesmo endereço e publicaria lixo
  como amostra válida — pior que perder o histórico, porque dado falso vira decisão
- `infra/ppp_drop_counter` ganha o acumulado desde o boot, ao lado da janela de 30 s que
  continua servindo o serial. É o que fecha a medição do débito 13 para a frota inteira
- **Famílias de nome fixadas antes da primeira série**: `router_` para a placa, `sensor_`
  para o ambiente. Renomear métrica depois quebra painel e histórico ao mesmo tempo
- ⚠️ **Risco número um: heap.** A PSRAM existe na placa e **não está compilada**
  (`# CONFIG_ESP32_SPIRAM_SUPPORT is not set`), então o handshake do mbedTLS disputa só DRAM
  interna. Encolher os buffers TLS vem primeiro; medir vem depois, com o cliente carregado
- Sensores externos ficam **fora de escopo**, mas a fase fixa os nomes, o formato e o
  armazenamento que eles vão usar
- **Domínio concluído em 23/09/2026**, com 50 testes nativos, sem tocar em hardware:
  `domain/mqtt_backoff` (progressão, jitter, gate por uplink, patamar de estabilidade),
  `domain/telemetry` (amostra de 20 B, payload, decisão de publicar) e
  `domain/telemetry_buffer` (anel, as três guardas do cabeçalho, correção de timestamp).
  Fecha os critérios de aceite 11 e 15 do PRD
- **Falta tudo o que toca o mundo:** `infra/mqtt_client`, o acumulado do
  `infra/ppp_drop_counter`, os campos de broker em `RouterSettings` e na página, os buffers
  do mbedTLS no `sdkconfig.defaults`, o broker com TLS e ACL, o Telegraf, os painéis e a
  medição de heap com TLS carregado
- Detalhes em [prd/14-telemetria-mqtt.md](prd/14-telemetria-mqtt.md)

### Fase 10 — Acesso remoto à página de configuração — proposta

- Túnel WireGuard partindo da placa contra servidor próprio, para abrir a página e subir
  firmware sem estar no Wi-Fi da unidade. Depende de um servidor que ainda não existe
- Detalhes em [prd/13-acesso-remoto.md](prd/13-acesso-remoto.md)

### Fase 11 — Posição por GNSS — proposta

- Responde três perguntas: onde a unidade foi instalada, se ela saiu do lugar (furto) e onde
  a frota está num geomap. As três se resolvem com fix esparso — nenhuma pede rastreamento
  contínuo. Depende da Fase 9, que é o canal por onde a posição sai da placa
- **Bloqueada por uma verificação de hardware.** Nem todo A7670E tem GNSS: só o
  `A7670E-FASE`. `AT+SIMCOMATI` responde, e roda em modo comando no boot, sem CMUX. Se o
  módulo não for `-FASE`, o caminho passa a ser módulo GPS externo, que é outro PRD
- **A decisão técnica é migrar o modem para CMUX.** GNSS se lê por AT, e AT não passa por uma
  UART ocupada com PPP. Sair do modo dados a cada fix derrubaria a internet dos clientes;
  fix só no boot não detecta movimento e ainda atrasaria o enlace pelo cold start
- O CMUX paga por duas features: encerra também a exclusão de RSSI/CSQ registrada na Fase 9
- ⚠️ **O risco mora debaixo do que já funciona:** o PPP validado nas Fases 4–6 passa a rodar
  sobre uma camada nova, na mesma UART de 115200 que o débito 13 mostrou ser gargalo.
  Revalidar reconexão, queda de RF e perda de SIM é obrigatório, e o `ppp_drops` sob carga
  não pode piorar
- Detecção de movimento é domínio puro e testável: fix reprovado não entra, haversine contra
  uma posição de referência **gravada pelo operador** (não pelo primeiro fix, que se
  desarmaria no cativeiro), e N confirmações antes de alarmar
- Detalhes em [prd/15-gps-posicao.md](prd/15-gps-posicao.md)

## Em aberto para decidir durante a implementação (não bloqueia o início)

- Se a mudança de config exige reboot do ESP32 ou se o firmware reconecta a quente
- Payload exato do formulário HTML (campos extras que você mencionou como "etcs futuras")
- Reconexão automática do modem em caso de queda de sinal (retry, backoff)