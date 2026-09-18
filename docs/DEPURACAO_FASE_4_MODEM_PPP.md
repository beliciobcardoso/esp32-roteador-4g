# Depuração da Fase 4 — Modem PPP (A7670E)

Registro das falhas encontradas até o PPPoS subir, incluindo os diagnósticos que se
provaram errados. O objetivo não é narrar a sessão: é evitar que o próximo (humano ou
agente) repita a mesma investigação, e deixar explícito **como cada causa foi provada**,
já que várias hipóteses plausíveis estavam incorretas.

Resultado final: registro LTE em ~3 s, PAP aceito, IP e DNS da operadora atribuídos.

---

## 1. `AT+CPIN?` falhando logo após o boot — não era SIM ausente

**Sintoma:** `Modem: read_pin -> [ESP_FAIL]` → `SIM nao detectado (AT+CPIN? falhou)`.

**Diagnóstico errado:** cartão mal encaixado ou defeituoso. O usuário chegou a validar o
SIM no celular (funcionava em 4G normalmente) — o hardware nunca foi o problema.

**Causa real:** logo após o power-on o módulo já responde `AT`, mas ainda está lendo o
cartão. Nessa janela o `AT+CPIN?` devolve `+CME ERROR: 14 (SIM busy)`, e o `esp_modem`
colapsa qualquer erro em `ESP_FAIL` — indistinguível de cartão ausente. Havia uma única
sondagem, sem retry.

**Correção:** `waitForSimReady()` em `infra/modem_ppp.cpp`, com orçamento de 20 tentativas
a 1 s. Distingue os dois casos: `ESP_FAIL` é transitório e merece retry; `ESP_OK` com
`pinOk == false` significa que o cartão respondeu e está pedindo PIN — nenhum retry
resolve, falha imediato.

**Prova:** `Modem: SIM pronto na tentativa 3`.

---

## 2. Pulso de PWRKEY curto demais

**Sintoma:** handshake AT só fechava por volta da 18ª tentativa, às vezes exigindo um novo
pulso de PWRKEY.

**Causa:** `kPwrKeyPulseMs` era 100 ms. O `Ton(pwrkey)` do A7670E é ≈ 1000 ms — o
`LilyGo-Modem-Series` usa `delay(1000)`.

**Correção:** `kPwrKeyPulseMs = 1000`.

**Prova:** sync AT caiu da tentativa 18 para a 6, sem re-pulso.

---

## 3. Logs do `esp_modem` não apareciam — duas causas somadas

Chamar `esp_log_level_set(..., ESP_LOG_VERBOSE)` não produzia saída nenhuma.

1. `CONFIG_ESP_MODEM_ADD_DEBUG_LOGS` não estava setado. O Kconfig do componente compila os
   `ESP_LOGD`/`ESP_LOGV` **fora do binário** — nenhum ajuste em runtime os traz de volta.
2. O TAG usado era `"esp-modem"`, que não identifica nada. Os TAGs reais do componente são
   `command_lib`, `modem_api` e `uart_terminal`.

Só com as duas coisas corrigidas o diálogo AT cru apareceu — e foi ele que permitiu
diagnosticar os itens 5 e 6.

**Estado atual:** ambas as flags foram removidas após a estabilização. A receita para
religá-las está comentada em `ModemPpp::start()`.

---

## 4. Tabela de partições ignorada silenciosamente

**Sintoma:** firmware ocupava 87,3% de 1 MB num chip de 4 MB. `partitions.csv` foi criado e
`CONFIG_PARTITION_TABLE_CUSTOM=y` aparecia corretamente no `sdkconfig` gerado — mas o boot
continuava mostrando `factory, app, factory, 0x10000, 1M`.

**Causa:** o PlatformIO sobrepõe a tabela do ESP-IDF com o default do board. A build
"passava" sem nenhum aviso.

**Correção:** `board_build.partitions = partitions.csv` no `platformio.ini`, mais
`rm -rf .pio/build/esp-wrover-kit`.

**Lição:** conferir o **binário da partição** com `gen_esp32part.py`, não o log da build.
Um `sdkconfig` correto não garante que a tabela aplicada seja aquela.

Resultado: app passou de 1 MB para 1,856 MB com dois slots OTA (48,2% de ocupação).

---

## 5. O APN nunca chegava ao módulo — ordem, não valor

**Sintoma:** `CEREG` estacionava em 3 (*registration denied*), com RSSI excelente
(26–29, ou seja −61 a −55 dBm) e SIM lendo normalmente.

**Diagnóstico errado (meu):** afirmei que o APN incorreto (`internet` em vez de
`zap.vivo.com.br`) causava o `CEREG=3`. O valor estava de fato errado, mas essa **não era a
causa**.

**Causa real:** `esp_modem_dce_module.hpp:56` mostra que `set_pdp_context()` só é chamado
dentro de `setup_data_mode()`, que roda em `set_mode(DATA)` — ou seja, **depois** da espera
por registro. O `AT+CGDCONT` nunca havia sido enviado quando o módulo tentava o EPS Attach.

Isso importa porque no LTE o Attach carrega um *PDN Connectivity Request*: um APN
desconhecido não degrada só os dados, **rejeita o attach inteiro** (EMM cause #27). O
módulo estava anexando com o contexto PDP de fábrica.

**Correção:** `applyPdpContext()` envia `AT+CGDCONT=1,"IP","<apn>"` **antes** do laço de
espera de `CEREG`.

**Prova:** `CEREG` passa 0 → 11 → 1 em 3 s, contra o timeout completo anterior.

---

## 6. Armadilha de ABI C/C++ no `esp_modem`

Ao tentar usar a API tipada para o PDP context:

```
invalid initialization of non-const reference of type 'PdpContext&'
from an rvalue of type 'esp_modem_PdpContext_t*'
```

**Causa:** funções geradas a partir de `esp_modem_command_declare.inc` declaram parâmetros
C++ (referências, `std::string`) no header, mas são implementadas contra tipos C
(`char*`, `esp_modem_PdpContext_t*`). Os layouts são incompatíveis — `esp_modem::PdpContext`
tem membros `std::string`, o struct C tem `const char*`.

O mesmo vale para comandos com saída string: o header declara `std::string&`, a
implementação faz `strlcpy` num `char*`. **Usá-los arrasa a stack e derruba a placa no
interrupt watchdog.** Comandos com saída `int`/`bool` não têm o problema — ali a ABI de
referência e ponteiro coincide de verdade.

`esp_modem_set_apn()` também não serve: só chama `configure_pdp_context()`, sem enviar AT.

**Correção:** usar `esp_modem_command()` — declarado à mão em `esp_modem_c_api_types.h:150`,
consistente com sua implementação — com a string AT crua.

---

## 7. `esp_netif_ppp_set_auth()` devolvendo `ESP_ERR_ESP_NETIF_IF_NOT_READY`

**Sintoma:** `PPP: auth PAP usuario="vivo" -> [ESP_ERR_ESP_NETIF_IF_NOT_READY]`. A conexão
subia mesmo assim (a Vivo aceita o APN sem autenticar), então o erro passaria despercebido —
o PAP simplesmente nunca era aplicado.

**Diagnóstico errado (meu):** supus que a chamada acontecia antes do driver PPP ser anexado
ao `esp_netif`, e movi o bloco para depois do `esp_modem_new_dev()`. **O erro persistiu** —
a hipótese estava errada.

**Causa real:** `esp_netif_lwip_ppp.c:252-260`. Sem `PPP_AUTH_SUPPORT`, o corpo da função é
compilado fora e o ramo `#else` retorna exatamente esse código de erro. Faltava habilitar
PAP no lwIP.

**Correção:** `CONFIG_LWIP_PPP_PAP_SUPPORT=y` em `sdkconfig.defaults`.

**Atenção ao nome:** o símbolo é `LWIP_PPP_PAP_SUPPORT`. Tentei antes
`LWIP_PPP_PAP_AUTH_SUPPORT`, que não existe — o Kconfig aceitou em silêncio e o
`sdkconfig` gerado seguiu com `# ... is not set`.

**Prova:** `PPP: auth PAP usuario="vivo" -> [ESP_OK]`.

---

## 8. `sdkconfig.defaults` não é reaplicado sozinho

**Sintoma:** flag nova adicionada ao `sdkconfig.defaults`, build limpa, e o `sdkconfig`
gerado continuava com `# CONFIG_... is not set`.

**Causa:** o PlatformIO gera `sdkconfig.<env>` uma vez e não o regenera enquanto o arquivo
existir. Como ele está no `.gitignore`, é fácil esquecer que existe.

**Correção:** apagar `sdkconfig.<env>` — limpar `.pio/build` **não basta**.

```bash
rm -f sdkconfig.esp-wrover-kit && rm -rf .pio/build/esp-wrover-kit && pio run -e esp-wrover-kit
```

Sempre confirme com `grep` no `sdkconfig` gerado que a flag entrou.

---

## 9. WPA3 em softAP é inalcançável neste hardware

**Sintoma:** `W (1261) wifi:Invalid authmode 7 configured for softAP` em todo boot.

**Causa:** a decisão original pedia `WIFI_AUTH_WPA2_WPA3_PSK`. No ESP32 clássico com
IDF 4.4 isso não existe: o Kconfig `ESP32_WIFI_ENABLE_WPA3_SAE` cobre apenas o lado
station — *"allow the device to establish a WPA3-Personal connection with eligible AP's"*.
SoftAP SAE só chegou no IDF 5.x (`ESP_WIFI_SOFTAP_SAE_SUPPORT`). O driver rejeitava o
authmode e caía num modo não determinado.

**Correção:** `WIFI_AUTH_WPA2_PSK` fixado explicitamente, com o retorno de
`esp_wifi_set_config()` verificado — antes ele era descartado, então o AP poderia subir com
authmode indeterminado sem ninguém perceber. Decisão revisada em `AGENTS.md` e
`docs/PLANO_ROTEADOR.md`; reabrir se migrarmos para IDF 5.x.

---

## 10. Ferramental — erros repetidos que custaram tempo

**Porta serial ocupada.** `Could not exclusively lock port` aparece sempre que um
`platformio device monitor` está aberto. Aconteceu três vezes. Um processo por vez na
porta: `fuser -v /dev/ttyACM*` mostra quem segura.

**A porta reenumera após o reset do upload.** `/dev/ttyACM0` vira `/dev/ttyACM1`. Nunca
usar o caminho numerado em script — resolver pelo estável:

```bash
DEV=$(readlink -f /dev/serial/by-id/usb-1a86_USB_Single_Serial_58EF052375-if00)
```

**`pio device monitor` não aceita stdin redirecionado** (`UserSideException`). Para captura
automatizada, falar com a porta via `pyserial`, alternando DTR/RTS para forçar o reset.

**Invocar o esptool.** `python -m esptool` → `No module named esptool`; forçar `sys.path` →
`ModuleNotFoundError: intelhex`. A forma que funciona:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port /dev/ttyACM1 flash_id
```

---

## Em aberto

**`CEREG` reportando estado 11.** Não é valor válido no 3GPP TS 27.007. Aparece de forma
transitória durante o attach e converge para 1 sozinho, então não bloqueia — mas o
significado não foi confirmado na documentação da SIMCom. O diagnóstico cru
(`AT+CEREG?`/`AT+COPS?`/`AT+CGDCONT?`) segue no código justamente para isso.

**`dumpNetworkDiagnostics()` e os traços `AT>`/`AT<` foram mantidos** de propósito. Só
disparam quando o registro falha, e foram exatamente o que revelou o item 5. Removê-los
nos deixaria cegos na próxima.
