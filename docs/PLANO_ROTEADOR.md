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
Usuário abre 192.168.4.1
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
- `infra/wifi_ap`: sobe SoftAP com SSID/senha vindos do `load_settings`, IP fixo (ex: `192.168.4.1`)
- Teste: conectar um celular no AP e confirmar que recebe IP por DHCP
- **Validado em campo:** celular conecta e recebe IP por DHCP
- ⚠️ Dois critérios originais não se sustentaram, descobertos na Fase 4:
  - WPA2/WPA3 misto é inalcançável no ESP32 clássico com IDF 4.4 — revisado para WPA2-PSK
  - "até 20 clientes" não é cumprido: o driver corta em 10 (chaves do ESP-NOW).
    Ver débito 5 em [DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md); validar na Fase 6

### Fase 3 — Servidor de configuração HTTP — ✅ concluída
- `adapters/html_page`: formulário simples (SSID, senha WiFi, APN, usuário/senha admin)
- `adapters/http_config_handler`: rotas GET/POST, Basic Auth, chama `save_settings`
- Teste: acessar `192.168.4.1` do celular conectado no AP, editar e salvar configs, confirmar persistência após reboot
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
    mais no DHCP — quem resolve é o `infra/dns_forwarder` em `192.168.4.1:53` (PRD 09)
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

### Fase 7 — Relógio (NTP + fuso) — planejada

- `infra/clock`: SNTP sincronizado ao subir o uplink e a cada reconexão
- Campo `timezone` na NVS (`kCurrentSchema` 2→3, descarta a config atual) e `<select>` de
  fuso na página de configuração, valendo a quente
- Fonte é NTP, não `AT+CCLK?`/NITZ — NITZ depende da operadora entregar
- **Consumidores da hora ficam fora do escopo**: histórico de quedas, agendamento de
  reboot, expiração de sessão e carimbo de OTA dependem disso, mas vêm depois
- Detalhes em [prd/07-relogio-ntp.md](prd/07-relogio-ntp.md)

## Em aberto para decidir durante a implementação (não bloqueia o início)

- Se a mudança de config exige reboot do ESP32 ou se o firmware reconecta a quente
- Payload exato do formulário HTML (campos extras que você mencionou como "etcs futuras")
- Reconexão automática do modem em caso de queda de sinal (retry, backoff)