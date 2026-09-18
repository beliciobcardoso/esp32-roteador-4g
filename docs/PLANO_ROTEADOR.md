# Plano — Firmware Roteador 4G (ESP32 + A7670E)

Decisões já fechadas, que este plano assume como dadas:

- Framework: PlatformIO, `espidf, arduino` híbrido (já migrado e validado)
- Conectividade: PPP via modem A7670E + NAT (NAPT) entre WiFi AP e a interface PPP — roteamento transparente, não proxy
- Autenticação da página de config: HTTP Basic Auth simples
- Acesso à página de config: IP fixo (sem portal cativo)
- Persistência de credenciais: NVS sem criptografia
- Até 20 clientes WiFi simultâneos
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

### Fase 1 — Storage (NVS)
- `domain/router_settings` (struct + validação)
- `adapters/settings_repository` (interface)
- `adapters/nvs_settings_repository` (implementação)
- `usecases/load_settings`, `usecases/save_settings`
- Teste isolado: gravar e ler de volta via serial, sem rede nem HTTP ainda

### Fase 2 — WiFi AP
- `infra/wifi_ap`: sobe SoftAP com SSID/senha vindos do `load_settings`, IP fixo (ex: `192.168.4.1`)
- Teste: conectar um celular no AP e confirmar que recebe IP por DHCP

### Fase 3 — Servidor de configuração HTTP
- `adapters/html_page`: formulário simples (SSID, senha WiFi, APN, usuário/senha admin)
- `adapters/http_config_handler`: rotas GET/POST, Basic Auth, chama `save_settings`
- Teste: acessar `192.168.4.1` do celular conectado no AP, editar e salvar configs, confirmar persistência após reboot

### Fase 4 — Modem PPP — ✅ concluída
- `infra/modem_ppp`: integra `esp_modem`, sequência de power-on do A7670E (PWRKEY), sobe PPPoS com o APN salvo
- Teste: confirmar que a interface PPP recebe IP da operadora (log via serial)
- **Validado em campo:** registro LTE em ~3 s na Vivo, PAP aceito, IP e DNS da operadora atribuídos
- Percalços e diagnósticos descartados: [DEPURACAO_FASE_4_MODEM_PPP.md](DEPURACAO_FASE_4_MODEM_PPP.md)

### Fase 5 — NAT / roteamento
- `infra/nat_bridge`: habilita NAPT entre a interface AP e a interface PPP
- `usecases/start_routing`: orquestra a ordem (settings → AP → modem → NAT)
- Teste final: celular conectado no AP navega na internet através do modem 4G

### Fase 6 — Integração e testes de carga
- Testar com múltiplos dispositivos simultâneos (até 20)
- Validar reconexão automática se o modem cair
- Validar que salvar config nova reconecta corretamente sem exigir reboot manual (ou define que reboot é necessário e avisa o usuário na página)

## Em aberto para decidir durante a implementação (não bloqueia o início)

- Se a mudança de config exige reboot do ESP32 ou se o firmware reconecta a quente
- Payload exato do formulário HTML (campos extras que você mencionou como "etcs futuras")
- Reconexão automática do modem em caso de queda de sinal (retry, backoff)