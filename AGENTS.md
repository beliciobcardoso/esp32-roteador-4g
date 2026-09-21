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

Se `sdkconfig.defaults` mudar e não refletir:
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

**Estado atual:** Fases 1-6 implementadas e validadas em hardware — storage NVS, SoftAP, config HTTP, modem PPP, NAT/roteamento e supervisão do uplink com reconexão automática. Um celular conectado no AP navega pelo 4G, e o enlace se recupera sozinho de queda de RF (~16 s) e de perda do SIM (backoff até reboot). As Fases 7 (relógio por SNTP e fuso configurável) e 8 (atualização de firmware pela própria página, com rollback do bootloader) foram **validadas em placa em 20/09/2026**: relógio certo 17 s depois do reset, fuso trocando a quente, OTA trocando de slot e rollback revertendo de verdade um reset dentro da janela. Segue aberto um único critério de bancada — upload interrompido no meio. PRDs em [docs/prd/](docs/prd/), ressalvas por fase em [docs/PLANO_ROTEADOR.md](docs/PLANO_ROTEADOR.md).

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
  escuta em `192.168.4.1:53` e repassa para o DNS da operadora lido de `dns_getserver(0)`.
  O `NatBridge` **não mexe mais no DHCP do AP**: a opção 6 já sai com o IP do próprio AP
  por padrão (`dhcpserver.c`), e com um resolvedor nesse endereço o valor do lease vale
  para sempre. O bind é explicitamente em `192.168.4.1`, nunca `INADDR_ANY` — com
  `INADDR_ANY` o socket atenderia a interface PPP e o roteador viraria resolvedor aberto
  para a rede da operadora. Encurtar o lease foi rejeitado: agravaria a janela de DHCP.
  Justificativa completa em [docs/prd/09-dns-local.md](docs/prd/09-dns-local.md)
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

## Hardware — cuidados obrigatórios

- `BOARD_POWERON_PIN` (GPIO12) tem que ir `HIGH` no `setup()` — sem isso a placa desliga sozinha rodando só na bateria
- `BATTERY_VOLTAGE_DIVIDER_RATIO` em [include/config.h](include/config.h) é calibrado por multímetro numa placa específica — não é universal. Desde a Fase 7 é só o **default de fábrica**: o valor em uso vem da NVS e é editável pela página de config, com faixa `[1.4, 10.0]` validada no domínio. O piso é física, não gosto — abaixo de 4.4/3.3 a leitura satura e a placa reporta tensão menor justamente quando está carregada
- Só um processo por vez na porta serial — upload falha com `Device or resource busy` se o monitor estiver aberto
- A porta serial reenumera após o reset do upload (`ttyACM0` → `ttyACM1`) — sempre usar o caminho estável `/dev/serial/by-id/...`, nunca o numerado
- Pulso de PWRKEY do A7670E precisa de 1000 ms (`Ton(pwrkey)`) — 100 ms faz o handshake AT demorar ou falhar
- `sdkconfig.<env>` é gerado e ignorado pelo git; o PlatformIO **não** reaplica `sdkconfig.defaults` enquanto ele existir — apagar o arquivo, limpar `.pio/build` não basta
- **Ler serial sob tráfego exige filtro.** Medido em 20/09/2026: `E (…) esp-netif_lwip-ppp: pppos_input_tcpip failed with -1` chega a 89% das linhas, e o `HW FIFO Overflow` que vem junto corta linhas de outros módulos ao meio (`Bateria:` vira `ateria:`, `eria:`). Usar `grep -avE "pppos_input_tcpip|ateria:|^ria:|^eria:|^teria:"`. Causa e opções no débito 13

## Testes manuais

- Atualização de firmware pela página: [docs/TESTE_OTA.md](docs/TESTE_OTA.md) — como gerar os
  arquivos que exercitam cada regra de recusa e o caminho do sucesso, com o que cada um tem
  que mostrar na tela.

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
- Referência de documentação para código **deste** repositório cita arquivo + símbolo
  (`http_config_handler.cpp` → `begin()`), nunca número de linha — linha envelhece em
  silêncio a cada refatoração, e este erro já apareceu em duas revisões seguidas (débito 21).
  Citação de fonte externa (core Arduino, lwIP, ESP-IDF) pode ter linha: aponta para versão
  instalada de dependência, que não se move sozinha
