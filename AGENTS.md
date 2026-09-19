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

**Estado atual:** Fases 1-6 implementadas e validadas em hardware — storage NVS, SoftAP, config HTTP, modem PPP, NAT/roteamento e supervisão do uplink com reconexão automática. Um celular conectado no AP navega pelo 4G, e o enlace se recupera sozinho de queda de RF (~16 s) e de perda do SIM (backoff até reboot). PRDs em [docs/prd/](docs/prd/), ressalvas por fase em [docs/PLANO_ROTEADOR.md](docs/PLANO_ROTEADOR.md).

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
- Até 15 clientes WiFi simultâneos, WPA2-PSK (`WIFI_AUTH_WPA2_PSK`) — 15 é o teto do
  driver no ESP32 clássico (`ESP_WIFI_MAX_CONN_NUM`), não uma escolha de projeto
  - Revisado na Fase 4: WPA2/WPA3 misto era a decisão original, mas o ESP32 clássico
    não suporta SAE em softAP no IDF 4.4 (`ESP32_WIFI_ENABLE_WPA3_SAE` cobre só o lado
    station). O driver rejeitava com `Invalid authmode 7`. Reabrir se migrarmos para IDF 5.x.

## Hardware — cuidados obrigatórios

- `BOARD_POWERON_PIN` (GPIO12) tem que ir `HIGH` no `setup()` — sem isso a placa desliga sozinha rodando só na bateria
- `BATTERY_VOLTAGE_DIVIDER_RATIO` em [include/config.h](include/config.h) é calibrado por multímetro numa placa específica — não é universal, e ainda não é sobrescrevível por configuração (débito 1 em [docs/DEBITOS_TECNICOS.md](docs/DEBITOS_TECNICOS.md))
- Só um processo por vez na porta serial — upload falha com `Device or resource busy` se o monitor estiver aberto
- A porta serial reenumera após o reset do upload (`ttyACM0` → `ttyACM1`) — sempre usar o caminho estável `/dev/serial/by-id/...`, nunca o numerado
- Pulso de PWRKEY do A7670E precisa de 1000 ms (`Ton(pwrkey)`) — 100 ms faz o handshake AT demorar ou falhar
- `sdkconfig.<env>` é gerado e ignorado pelo git; o PlatformIO **não** reaplica `sdkconfig.defaults` enquanto ele existir — apagar o arquivo, limpar `.pio/build` não basta

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

- Migration destrutiva/irreversível: sinalizar antes. Na prática isto quer dizer **não subir
  `kCurrentSchema` por causa de campo novo**: `load()` trata schema menor que o atual como
  registro ausente, então o bump apaga SSID, senha e APN de toda unidade já configurada.
  Chave nova entra lida com default (foi assim com `admin_pend`, débito 10)
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
