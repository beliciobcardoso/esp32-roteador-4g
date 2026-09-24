# AGENTS.md

Firmware roteador 4G — LilyGO T-A7670E R2 (ESP32-WROVER-E + modem A7670E).

## Stack

- PlatformIO, framework híbrido `espidf, arduino` (não é Arduino puro)
- Board declarado: `esp-wrover-kit` (mais próximo no catálogo — placa real é LilyGO, diferenças de flash são esperadas)
- C++ (ESP-IDF/Arduino), sem sistema de build CMake de terceiros além do gerado pelo PlatformIO

## Comandos

```bash
pio run                    # build
pio run --target upload    # flash (fecha o monitor serial antes)
pio device monitor         # monitor serial
pio test -e native         # testes das regras puras, no host (sem placa)
```

`pio test -e native` compila só `src/domain/` (ver `build_src_filter` no `platformio.ini`).
Se um arquivo de `domain/` passar a incluir `Arduino.h` ou `esp_*.h` direto, esse comando
quebra — é de propósito, é o que impede a regra de dependência de virar só comentário.

O build lê o `sdkconfig.esp-wrover-kit` gerado, não o `sdkconfig.defaults`, e só regenera
o gerado quando ele não existe. Opção nova no defaults não chega ao firmware e o build sai
verde do mesmo jeito — em 24/09/2026 isso gravou placa sem `CORE_LOCKING` e **sem rollback de
OTA** (débito 26). Depois de mexer no defaults, antes de gravar unidade de campo, ou na
dúvida:
```bash
rm -f sdkconfig.esp-wrover-kit && rm -rf .pio && pio run
```

Ver [docs/SETUP.md](docs/SETUP.md) pra setup de ambiente do zero.

## Arquitetura alvo

Clean Architecture — ver [docs/PLANO_ROTEADOR.md](docs/PLANO_ROTEADOR.md) pra estrutura completa de pastas e fases de implementação. Resumo da regra de dependência:

- `domain/` — regras puras, não importa `infra` nem `adapters`
- `usecases/` — depende só de interfaces (`adapters/*_repository.h`), nunca de implementação concreta
- `adapters/` — implementam as interfaces, fazem parsing HTTP/HTML
- `infra/` — wrappers finos sobre APIs ESP-IDF/Arduino (WiFi AP, PPP, NAT)
- `main.cpp` — só orquestração/injeção, zero lógica de negócio

**Estado atual:** Fases 1-6 implementadas e validadas em hardware — storage NVS, SoftAP, config HTTP, modem PPP, NAT/roteamento e supervisão do uplink com reconexão automática. Um celular conectado no AP navega pelo 4G, e o enlace se recupera sozinho de queda de RF (~16 s) e de perda do SIM (backoff até reboot). As Fases 7 (relógio por SNTP e fuso configurável) e 8 (atualização de firmware pela própria página, com rollback do bootloader) foram **validadas em placa em 20/09/2026**: relógio certo 17 s depois do reset, fuso trocando a quente, OTA trocando de slot e rollback revertendo de verdade um reset dentro da janela. Segue aberto um único critério de bancada — upload interrompido no meio. Da Fase 9 (telemetria MQTT, [PRD 14](docs/prd/14-telemetria-mqtt.md)) **só o domínio existe** — `domain/telemetry`, `domain/telemetry_buffer` e `domain/mqtt_backoff`, concluídos e testados no host em 23/09/2026; nada ainda fala com o mundo, e nenhuma linha rodou em placa. As Fases 10 (acesso remoto à página, [PRD 13](docs/prd/13-acesso-remoto.md)) e 11 (posição por GNSS, [PRD 15](docs/prd/15-gps-posicao.md)) seguem só propostas. PRDs em [docs/prd/](docs/prd/), ressalvas por fase em [docs/PLANO_ROTEADOR.md](docs/PLANO_ROTEADOR.md).

A configuração persistida (chaves da NVS, defaults de fábrica, como consultar e apagar) está documentada em [docs/CONFIGURACAO_NVS.md](docs/CONFIGURACAO_NVS.md).

## Decisões já fechadas (não reabrir sem motivo)

- PPP via `esp_modem` (componente oficial ESP-IDF), não TinyGSM — TinyGSM não expõe interface IP roteável
- Sequência de subida do roteador fica em `main.cpp`, não num caso de uso — decidido na
  Fase 5. Orquestrar `WifiAp`/`ModemPpp`/`NatBridge` em `usecases/` exigiria ou acoplar a
  camada a implementação concreta, ou criar três interfaces que nunca terão segunda
  implementação neste hardware. É side-effect em hardware, não regra de domínio.
  Justificativa completa em [docs/prd/05-nat-roteamento.md](docs/prd/05-nat-roteamento.md)
- A reconexão do uplink 4G roda numa task FreeRTOS própria (`infra/link_supervisor`), não
  no `loop()` — decidido na Fase 6. A sequência de conexão bloqueia até ~160 s no pior
  caso; no `loop()` isso congelaria a página de configuração justamente quando ela é mais
  necessária. Reboot após 10 falhas seguidas, **no máximo 2 vezes** — o contador vive em
  `RTC_NOINIT_ATTR` para atravessar o próprio restart, e é validado por
  `esp_reset_reason() != ESP_RST_SW`. Sem o teto, falha permanente (SIM fora, sem
  cobertura) reiniciaria a placa a cada ~12 min para sempre, derrubando quem está no AP.
  Justificativa completa em [docs/prd/06-integracao-testes-carga.md](docs/prd/06-integracao-testes-carga.md)
- Config web: HTTP Basic Auth, IP fixo (sem portal cativo)
- O `GET /` mostra o estado do uplink 4G, e o texto mora em `domain/uplink_status` — não no
  adaptador. A frase decide o que a pessoa faz (esperar resolve ou não), então é regra e
  tem teste nativo. A página não se atualiza sozinha: recarregar é o refresh (débito 22)
- Persistência: NVS sem criptografia — aceitável em bancada, bloqueante para campo (débito 10)
- Não existe senha de fábrica. Senha de AP e senha de admin são **sorteadas por unidade no
  primeiro boot** com NVS vazia, impressas uma única vez no serial, e a de admin nasce com
  troca obrigatória (`admin_password_pending`). Derivar a senha do MAC foi **rejeitado**: o
  MAC do SoftAP é o BSSID, que vai em todo beacon — trocaria senha pública no GitHub por
  senha pública no ar. O sorteio roda no topo do `setup()`, antes do ADC e do rádio, porque
  usa `bootloader_random_enable()` e o contrato da IDF manda fechar essa janela antes de
  inicializar RF/ADC/I2S. Justificativa completa em
  [docs/prd/08-segredos-por-unidade.md](docs/prd/08-segredos-por-unidade.md)
- O DNS dos clientes do AP é resolvido localmente. `DnsForwarder` (`infra/dns_forwarder`)
  escuta em `192.168.10.1:53` e repassa para o DNS da operadora lido de `dns_getserver(0)`.
  O `NatBridge` **não mexe mais no DHCP do AP**: a opção 6 já sai com o IP do próprio AP
  por padrão (`dhcpserver.c`), e com um resolvedor nesse endereço o valor do lease vale
  para sempre. O bind é explicitamente em `192.168.10.1`, nunca `INADDR_ANY` — com
  `INADDR_ANY` o socket atenderia a interface PPP e o roteador viraria resolvedor aberto
  para a rede da operadora. Encurtar o lease foi rejeitado: agravaria a janela de DHCP.
  Justificativa completa em [docs/prd/09-dns-local.md](docs/prd/09-dns-local.md)
- **A faixa do AP é `192.168.10.0/24`, com a placa em `192.168.10.1`** — trocada em
  23/09/2026, antes era `192.168.4.0/24`. O motivo é colisão: a `192.168.4.0/24` é o default
  do core Arduino e de metade dos exemplos de ESP32, e numa bancada `http://192.168.4.1/`
  respondeu com um nginx da rede da empresa em vez da placa, **sem erro nenhum** — a rota
  saiu pela interface cabeada e a página simplesmente era de outro aparelho. Falha que se
  parece com "a placa está no ar", e não com "você está falando com a coisa errada", custa
  caro para diagnosticar.
  O valor é fixo em `infra/wifi_ap` (`kApIp`/`kApGateway`/`kApSubnet`), não vai para a NVS:
  existe uma faixa em uso, não duas, e editar isso pela página daria a quem configura a
  chance de se trancar para fora da unidade. Trocar as três constantes basta — o lease do
  DHCP sai do próprio IP do AP dentro do `set_esp_interface_ip()` do core (início em
  `ap_ip + 1`, fim em `início + 10`), e `DnsForwarder` e `NatBridge` leem de
  `WiFi.softAPIP()`. A máscara precisa ficar entre `/24` e `/28`, limite daquela função.
  **PRD 05, PRD 06, PRD 09 e os débitos 9 e 12 continuam dizendo `192.168.4.x`**: são
  registro do que foi medido na data, não instrução — quem for operar a unidade usa a faixa
  desta seção
- O relógio vem de SNTP, não do `AT+CCLK?`/NITZ do modem — NITZ depende de a operadora
  entregar, NTP não depende de operadora nenhuma. A sincronização é disparada pelo
  `LinkSupervisor` ao entrar em `Online`, e "já sincronizou alguma vez" é um latch ligado
  pelo callback do lwIP: `sntp_get_sync_status()` **não serve** para isso, porque volta a
  `SNTP_SYNC_STATUS_RESET` depois que a atualização completa. A tabela de fusos mora em
  `domain/timezone.h` e não no adaptador: `setenv("TZ", ...)` + `tzset()` não reclamam de
  string sem sentido, e o `<select>` da página não protege de um POST direto.
  Justificativa completa em [docs/prd/07-relogio-ntp.md](docs/prd/07-relogio-ntp.md)
- A atualização de firmware é **push**: a pessoa sobe o `firmware.bin` pela página, a placa
  não busca imagem em servidor nenhum. Pull exigiria URL, TLS e política de versão para um
  parque que hoje é uma placa. Não existe campo de checksum no formulário porque não
  adianta: o `esp_ota_set_boot_partition()` roda `image_validate()` antes de tocar no
  otadata e confere o SHA-256 que a própria imagem carrega — um MD5 digitado à mão só
  acrescentaria um jeito novo de errar. A gravação usa o `Update` do Arduino, que segura os
  16 primeiros bytes até o fim, então imagem parcial nunca fica bootável. A autenticação é
  conferida no `UPLOAD_FILE_START`, **não** no handler do POST: o `WebServer` chama o
  callback de upload dentro do parser da requisição, antes do handler, e checar depois
  gravaria a flash de quem não tem credencial. **A imagem nova é confirmada pelo operador, não pelo relógio.** Em
  20/09/2026 a confirmação automática por tempo saiu: ficar de pé não prova que alguém
  consegue chegar na placa, e um firmware que sobe, roda e não atende passava batido —
  confirmava sozinho, cancelava o rollback e deixava a placa viva e inalcançável, sem
  ninguém no local para apertar nada. Quem confirma agora é o botão em
  `POST /firmware/confirmar`, e o clique é prova empírica: se o POST chegou, o AP subiu, o
  DHCP entregou IP e o servidor respondeu. A regra está em `decideFirmwareConfirmation()`,
  no domínio, e junta três entradas — clique, saúde do boot e prazo:
  - clique → confirma, e ganha de uma leitura de saúde ruim (o POST veio pelo caminho que
    ela diz estar fora)
  - AP não subiu ou servidor fora → reverte na hora, sem esperar o prazo: esperar por um
    clique que não tem como chegar não acrescenta informação
  - `kConfirmationDeadlineMs` (**600 s**) sem clique → reverte
  O revert é ativo (`esp_ota_mark_app_invalid_rollback_and_reboot()`), não passivo: o
  rollback do bootloader depende de um reset, e firmware que roda normal nunca reseta
  sozinho.
  Justificativa completa em [docs/prd/11-atualizacao-ota.md](docs/prd/11-atualizacao-ota.md)
- **O core do Arduino cancela o rollback antes do `setup()` se ninguém o impedir.** O
  `initArduino()` (`esp32-hal-misc.c`) chama `esp_ota_mark_app_valid_cancel_rollback()`
  quando `CONFIG_APP_ROLLBACK_ENABLE=y` — chave **diferente** da
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` que o `sdkconfig.defaults` liga, e que vem ligada
  na configuração gerada. Ele só não faz isso se o símbolo weak `verifyRollbackLater()`
  devolver `true`, e o `main.cpp` sobrescreve esse símbolo exatamente por isso. Sem essa
  linha a imagem chega ao `loop()` já em `Valid`: a página nasce dizendo "confirmado",
  nenhum log sai e reset nenhum reverte — o rollback inteiro vira enfeite, sem erro em
  lugar nenhum. Conferir com
  `xtensa-esp32-elf-nm .pio/build/esp-wrover-kit/firmware.elf | grep verifyRollbackLater`:
  `T` é a aplicação valendo, `W` é o core vencendo
- **O `LinkSupervisor` segura o reboot dele enquanto a confirmação está pendente**
  (`supervisorMayRebootForUplink()`). Ele reinicia a placa depois de 10 falhas de uplink, a
  partir de ~7 min — dentro do prazo de 600 s isso faria o bootloader reverter uma imagem
  possivelmente boa por falta de sinal, que não é defeito dela. Foi o que destravou o prazo
  passar dos 7 min. Custo: até 10 min sem tentativas de reconexão quando o firmware novo é
  justamente o que quebrou o modem — e esse caso termina em revert no fim do prazo de todo
  jeito
- **`CONFIG_ESP_TASK_WDT_PANIC=y`.** Sem ela o Task WDT só imprime aviso no serial a cada
  5 s e não reinicia nada: firmware travado — `while` infinito, deadlock, espera de I/O sem
  timeout — fica de pé, mudo e inalcançável para sempre, e o rollback nunca roda porque
  depende de um reset que não acontece. É a única das três camadas que cobre travamento;
  prazo e autocheck só funcionam com o `loop()` girando
- Até 15 clientes WiFi simultâneos, WPA2-PSK (`WIFI_AUTH_WPA2_PSK`) — 15 é o teto do
  driver no ESP32 clássico (`ESP_WIFI_MAX_CONN_NUM`), não uma escolha de projeto
  - Revisado na Fase 4: WPA2/WPA3 misto era a decisão original, mas o ESP32 clássico
    não suporta SAE em softAP no IDF 4.4 (`ESP32_WIFI_ENABLE_WPA3_SAE` cobre só o lado
    station). O driver rejeitava com `Invalid authmode 7`. Reabrir se migrarmos para IDF 5.x.
- **A página de configuração mora em `http/index.html`, não num raw string em C++.** O
  arquivo entra no binário como está, pelo `EMBED_TXTFILES` de `src/CMakeLists.txt` mais o
  `board_build.embed_txtfiles` do `platformio.ini` — as duas chaves são necessárias, o CMake
  só lista o `.S` gerado e quem o gera é o SCons do PlatformIO. `html_page.h` expõe o
  conteúdo como `kConfigPageTemplate` via `asm("_binary_index_html_start")`, e
  `html_page.cpp` ficou só com o `escapeForHtmlAttribute`. Antes havia duas cópias do mesmo
  HTML — a que ia pro firmware e a que se editava no navegador — e elas já estavam
  divergindo na indentação. Consequência a aceitar: abrir `http/index.html` direto no
  navegador mostra os `{{PLACEHOLDER}}` crus, porque agora é o template de verdade, não uma
  maquete. Renomear ou mover o arquivo quebra o link com `undefined reference to
  _binary_index_html_start`, não em silêncio
- **A telemetria sai por MQTT, só de subida, e vem antes do túnel.** Fase 9 antes da 10: as
  duas contornam o CGNAT pelo mesmo princípio — a conexão nasce na placa —, mas o WireGuard
  põe uma terceira interface no mesmo lwIP que já faz NAT dos clientes do AP, e MQTT é um
  socket TCP de saída. Quatro decisões dessa fase valem **antes** de existir código, porque
  reabri-las depois é caro:
  - **Só publish, nunca subscribe.** Broker comprometido vê dado, não manda comando
  - **Duas famílias de nome de métrica**, fixadas antes da primeira série ir para o banco:
    `router_*` para a placa, `sensor_*` para grandeza de ambiente. Renomear métrica depois
    quebra painel e histórico ao mesmo tempo
  - **Payload JSON plano.** Sensores externos seriam o "segundo caso" que justificaria
    aninhamento em `domain/json`, e não são: objeto plano carrega N sensores e é o formato
    que o parser genérico do Telegraf repassa sem conhecer os nomes
  - **O anel de backfill em `RTC_NOINIT` tem cabeçalho versionado** (`magic` + `layout` +
    `sample_size`). Sem ele, depois de um OTA o firmware novo lê o layout antigo no mesmo
    endereço e publica lixo como amostra válida — pior que perder o histórico, porque dado
    falso vira decisão
  Justificativa completa em [docs/prd/14-telemetria-mqtt.md](docs/prd/14-telemetria-mqtt.md)

## Hardware — cuidados obrigatórios

- `BOARD_POWERON_PIN` (GPIO12) tem que ir `HIGH` no `setup()` — sem isso a placa desliga sozinha rodando só na bateria
- `BATTERY_VOLTAGE_DIVIDER_RATIO` em [include/config.h](include/config.h) é calibrado por multímetro numa placa específica — não é universal. Desde a Fase 7 é só o **default de fábrica**: o valor em uso vem da NVS e é editável pela página de config, com faixa `[1.4, 10.0]` validada no domínio. O piso é física, não gosto — abaixo de 4.4/3.3 a leitura satura e a placa reporta tensão menor justamente quando está carregada
- Só um processo por vez na porta serial — upload falha com `Device or resource busy` se o monitor estiver aberto
- A porta serial reenumera após o reset do upload (`ttyACM0` → `ttyACM1`) — sempre usar o caminho estável `/dev/serial/by-id/...`, nunca o numerado
- Pulso de PWRKEY do A7670E precisa de 1000 ms (`Ton(pwrkey)`) — 100 ms faz o handshake AT demorar ou falhar
- **Nem todo A7670E tem GNSS, e o nome do produto não diz qual é.** Só o `A7670E-FASE` (e o `A7670SA-FASE`) trazem GNSS interno; `-LASE`, `-LNXY-UBL` e toda a linha `A7670G` não trazem — o A7670G não tem GNSS **nem quando vendido "com GPS"**, caso em que a placa vem com módulo externo soldado na lateral. O conector IPEX de GNSS só está na PCB conforme a versão do módulo, então a ausência dele é sinal mas a presença não é prova. Quem responde é `AT+SIMCOMATI`. Fonte: `docs/en/esp32/a7670-esp32/README.MD` do [LilyGo-Modem-Series](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series)
- **Nesta placa o GNSS não tem pino de habilitação.** O `utilities.h` da LilyGO define `MODEM_GPS_ENABLE_GPIO (-1)` para `LILYGO_T_A7670` — ligar o GNSS é AT, não GPIO, e não custa pino nenhum
- **Pinos já ocupados pela placa**, além do modem (4, 5, 12, 25, 26, 27): cartão SD em 2, 13, 14 e 15; bateria em 35; RING do modem em 33; e ADC solar em 36 **na V1.4** (nas outras o 36 não está ligado). Conferir contra o `utilities.h` da LilyGO antes de prometer qualquer barramento
- **ADC2 não funciona com o Wi-Fi ligado no ESP32.** O driver do rádio toma o periférico e a leitura passa a falhar ou devolver lixo. Como o AP nunca desliga nesta placa, entrada analógica só em **ADC1** — GPIO 32–39, dos quais 34/36/39 são só entrada. GPIO35 já é a bateria e GPIO32 é o LED de teste. Orçamento de pinos: o modem ocupa 4, 5, 12, 25, 26 e 27, e o GPIO12 (`BOARD_POWERON`) é strapping que ainda alimenta o cartão SD
- **A PSRAM da placa não está compilada.** São 8 MB no hardware e `# CONFIG_ESP32_SPIRAM_SUPPORT is not set` no `sdkconfig` gerado, então todo o heap é DRAM interna. Qualquer raciocínio de memória — TLS, buffer, biblioteca nova — parte de ~320 KB compartilhados com WiFi, lwIP, PPP, NAT, DNS e WebServer, não dos 8 MB. `MALLOC_CAP_INTERNAL` e o heap total são o mesmo número hoje
- `sdkconfig.<env>` é gerado e ignorado pelo git; o PlatformIO **não** reaplica `sdkconfig.defaults` enquanto ele existir — apagar o arquivo, limpar `.pio/build` não basta
- ~~**Ler serial sob tráfego exige filtro.**~~ **Não exige mais** — o filtro de `grep` saiu de circulação e não deve voltar por hábito. Medido em 20/09/2026, a linha `E (…) esp-netif_lwip-ppp: pppos_input_tcpip failed with -1` chegava a 89% do serial, e o `HW FIFO Overflow` que vinha junto cortava linhas de outros módulos ao meio (`Bateria:` virava `ateria:`, `eria:`). Duas correções do mesmo dia encerraram isso: `infra/ppp_drop_counter` intercepta o `esp_log_set_vprintf`, conta o evento e **suprime a linha antes do serial**, publicando o total uma vez por janela de 30 s; e `CONFIG_LWIP_TCPIP_CORE_LOCKING` + `CORE_LOCKING_INPUT` tiraram a fila do caminho de entrada, que era a origem do descarte. Filtrar por `pppos_input_tcpip` hoje não casa com nada, e filtrar por `ateria:` esconde leitura de bateria legítima. Histórico no débito 13, fechado em 24/09/2026 com download completo e zero descartes

## Procedimentos manuais

- **Testar o OTA em bancada:** [docs/TESTE_OTA.md](docs/TESTE_OTA.md) — como gerar os
  arquivos que exercitam cada regra de recusa e o caminho do sucesso, com o que cada um tem
  que mostrar na tela.
- **Atualizar uma unidade em produção:** [docs/ATUALIZACAO_EM_PRODUCAO.md](docs/ATUALIZACAO_EM_PRODUCAO.md)
  — build de release, o que conferir antes de sair, a visita e a janela de confirmação.
  **O operador precisa estar dentro do Wi-Fi da unidade**: a página não responde pela
  internet (CGNAT), então o OTA tirou o cabo do caminho, não a viagem. Débito 24.

## Débitos técnicos conhecidos

Ver [docs/DEBITOS_TECNICOS.md](docs/DEBITOS_TECNICOS.md).

## Histórico de depuração

- Fase 4 (modem PPP): [docs/DEPURACAO_FASE_4_MODEM_PPP.md](docs/DEPURACAO_FASE_4_MODEM_PPP.md) —
  armadilhas do `esp_modem`, ordem do `AT+CGDCONT`, PAP no lwIP, `sdkconfig.defaults` que não
  recarrega. Consultar antes de mexer no modem ou em flags de Kconfig.

## Workflow de PRD/feature (obrigatório — consultar antes de iniciar e antes de concluir)

- **Ao iniciar** uma feature de um PRD: criar branch a partir de `developer` (nunca direto em `main`)
- **Ao finalizar** uma feature: aguardar validação do usuário antes de qualquer `git commit` ou merge para `developer`
- **Proibido criar PRs sem autorização explícita do usuário** — em nenhuma hipótese

## Regras de código

- Migration destrutiva/irreversível: sinalizar antes. Para a NVS isto deixou de ser o caso
  padrão: subir `kCurrentSchema` exige **um degrau novo em `domain/settings_migration`**,
  com teste nativo, e aí a unidade em campo preserva o que já tinha. Campo novo com default
  seguro continua entrando lido com default, sem subir o schema (foi assim com `admin_pend`,
  débito 10) — é menos trabalho e não precisa de degrau. Subir o schema **sem** escrever o
  degrau volta a ser destrutivo, e pior que antes: `load()` falso faz o
  `ProvisionSettingsUseCase` sortear senha nova, derrubando todos os clientes da unidade
- Nenhuma senha em código versionado — nem como default de fábrica. Segredo por unidade é
  sorteado no dispositivo (`domain/secret.h` + `infra/entropy.h`)
- Toda rota HTTP sensível (config): Basic Auth obrigatório, erro sem vazar detalhe interno
- Seguir padrão do arquivo existente ao editar, não impor estilo novo
- Não criar abstração antes de duas ocorrências reais a justificarem
- **Instante de tempo no domínio é `uint32_t`, não `unsigned long`.** `unsigned long` tem
  4 bytes na placa e 8 no host, então a subtracão que atravessa a virada de `millis()` nunca
  vira no teste nativo — o teste passa por não exercitar nada. `millis()` cabe exato em
  `uint32_t`, então nada se perde na placa. `domain/link_diagnostics` era a exceção
  anterior à regra (teste de virada verde pelo motivo errado) e foi convertido em 24/09/2026
- Referência de documentação para código **deste** repositório cita arquivo + símbolo
  (`http_config_handler.cpp` → `begin()`), nunca número de linha — linha envelhece em
  silêncio a cada refatoração, e este erro já apareceu em duas revisões seguidas (débito 21).
  Citação de fonte externa (core Arduino, lwIP, ESP-IDF) pode ter linha: aponta para versão
  instalada de dependência, que não se move sozinha
