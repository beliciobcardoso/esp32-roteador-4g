# Configuração persistida na NVS

Referência de consulta: o que o roteador guarda na flash, com que chave, de onde vem o
valor de fábrica e quem consome cada campo.

## Onde fica

| Item | Valor |
| --- | --- |
| Partição | `nvs`, offset `0x9000`, tamanho `0x6000` (24 KB) — ver `partitions.csv` |
| Namespace | `router_cfg` (`SETTINGS_NVS_NAMESPACE`, [include/config.h](../include/config.h)) |
| API | `Preferences` do core Arduino, sobre a NVS do ESP-IDF |
| Código | [src/adapters/nvs_settings_repository.cpp](../src/adapters/nvs_settings_repository.cpp) |

A partição `nvs` é separada das partições de app (`app0`/`app1`), então **atualização OTA
não apaga a configuração**. Só um `erase_flash` ou um `nvs_flash_erase` explícito apaga.

## Chaves

Todas as chaves de conteúdo são string; `schema` é int e `configured` é bool. O limite de
nome de chave da NVS é 15 caracteres — daí os nomes abreviados (`wifi_pass` e não
`wifi_password`).

| Chave NVS | Campo em `RouterSettings` | Tipo | Default de fábrica | Validação |
| --- | --- | --- | --- | --- |
| `ssid` | `wifi_ssid` | String | `esp32-roteador-4g` | não pode ser vazio |
| `wifi_pass` | `wifi_password` | String | sorteada no 1º boot | mínimo 8 caracteres |
| `apn` | `apn` | String | `zap.vivo.com.br` | não pode ser vazio |
| `apn_user` | `apn_user` | String | `vivo` | opcional (pode ser vazio) |
| `apn_pass` | `apn_password` | String | `vivo` | opcional (pode ser vazio) |
| `admin_user` | `admin_user` | String | `admin` | não pode ser vazio |
| `admin_pass` | `admin_password` | String | sorteada no 1º boot | mínimo 8 caracteres |
| `admin_pend` | `admin_password_pending` | bool | `true` no provisionamento | interno, ver abaixo |
| `schema` | — | int | `2` | interno, ver abaixo |
| `configured` | — | bool | `true` após o primeiro save | interno, ver abaixo |

⚠️ **Não existe mais senha de fábrica.** `DEFAULT_AP_PASSWORD` e `DEFAULT_ADMIN_PASSWORD`
saíram do [include/config.h](../include/config.h): as duas senhas são sorteadas por unidade
no primeiro boot com NVS vazia, impressas uma única vez no serial e gravadas em seguida.
Quem gravar a placa sem o monitor serial aberto perde esse bloco, e a saída é apagar a NVS
e reprovisionar (ver abaixo). Os defaults que sobraram — SSID, APN, usuário do APN e
`admin_user` — não são segredo. Detalhes e modelo de ameaça em
[PRD 08](prd/08-segredos-por-unidade.md).

Regras de validação em [src/domain/router_settings.cpp](../src/domain/router_settings.cpp).

## Quem consome cada campo

| Campo | Consumidor | O que faz com ele |
| --- | --- | --- |
| `wifi_ssid`, `wifi_password` | [src/infra/wifi_ap.cpp](../src/infra/wifi_ap.cpp) | `WiFi.softAP()`, authmode fixo em `WIFI_AUTH_WPA2_PSK` |
| `apn` | [src/infra/modem_ppp.cpp](../src/infra/modem_ppp.cpp) | `AT+CGDCONT=1,"IP",<apn>` antes do registro, e `ESP_MODEM_DCE_DEFAULT_CONFIG` |
| `apn_user`, `apn_password` | [src/infra/modem_ppp.cpp](../src/infra/modem_ppp.cpp) | `esp_netif_ppp_set_auth(PAP, ...)` — **só é chamado se `apn_user` não for vazio** |
| `admin_user`, `admin_password` | [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) | HTTP Basic Auth, realm `roteador-4g` |
| `admin_password_pending` | [src/adapters/http_config_handler.cpp](../src/adapters/http_config_handler.cpp) | enquanto `true`, o `GET /` mostra o aviso e o `POST /` recusa gravação que mantenha a senha sorteada |

Valores fixos em código, **não** configuráveis pela NVS: IP do AP (192.168.4.1/24),
canal Wi-Fi (1), limite de clientes, porta HTTP (80), pinagem do modem, baud da UART.

## `configured` e `schema` — por que existem duas chaves de controle

`load()` devolve `false` (= "usa os defaults de fábrica") em dois casos:

1. `configured` ausente ou `false` — primeiro boot, nada foi salvo ainda.
2. `schema` gravado **menor** que `kCurrentSchema` (hoje `2`).

O schema 1 não tinha `apn_user`/`apn_pass` e guardava um APN default que não existe na
rede da Vivo. Sem o corte por versão, um dispositivo já configurado continuaria insistindo
no APN velho para sempre, porque `configured` estava `true`.

**Consequência operacional: subir o `kCurrentSchema` descarta silenciosamente toda a
configuração do usuário e volta para os defaults de fábrica — inclusive SSID e senhas.**
Não há migração campo a campo. Fazer isso só quando a perda for intencional.

`save()` sempre grava os dois: primeiro o `schema`, depois `configured = true`.

### Por que `admin_pend` não subiu o schema

`admin_pend` é chave nova e o schema continua em `2`, de propósito. Subir para `3` faria
`load()` tratar todo registro existente como ausente — **toda unidade já configurada
perderia SSID, senha e APN** por causa de um booleano. A chave é lida com default `false`,
que é exatamente o comportamento certo para registro antigo: unidade que já foi configurada
por uma pessoa não tem senha pendente.

## Comportamento de escrita parcial

`save()` grava os 7 campos de uma vez, sempre. Não existe update de campo isolado.

O formulário HTTP tem um atalho: **campo de senha em branco mantém a senha atual** (as três
— Wi-Fi, APN e admin). Serve para trocar só o SSID sem retypar tudo. Isso acontece no
`handlePostRoot()`, não no repositório: o que chega no `save()` já vem completo.

Não há transação. Se a placa reiniciar no meio de um `save()`, dá para ficar com parte dos
campos novos e parte dos velhos. `configured` e `schema` são gravados por último, então
pelo menos o registro nunca fica marcado como válido antes dos dados estarem lá.

## Como consultar e apagar

Ler a configuração atual sem serial — pela página de config, com o AP no ar:

```bash
curl -u admin:<a senha sorteada no primeiro boot> http://192.168.4.1/
```

O HTML devolve SSID, APN, usuário do APN e usuário admin preenchidos. **Senhas nunca são
devolvidas** — os campos vêm em branco de propósito.

Apagar só a NVS (mantém o firmware gravado) — é também o caminho para **reprovisionar**
uma unidade cuja senha sorteada se perdeu, e a única forma de tirar de bancadas antigas o
`admin1234` que ficou gravado antes desta versão. Com a NVS vazia, o boot seguinte sorteia
senhas novas e as imprime no serial; **abra o monitor antes de reiniciar**:

```bash
pio pkg exec -p tool-esptoolpy -- esptool.py --port "$(realpath /dev/serial/by-id/usb-1a86_USB_Single_Serial_58EF052375-if00)" erase_region 0x9000 0x6000
```

O caminho `by-id` não é frescura: o `/dev/ttyACM*` da placa **muda de número** entre
replugs (já aconteceu na Fase 4, ver
[DEPURACAO_FASE_4_MODEM_PPP.md](DEPURACAO_FASE_4_MODEM_PPP.md)). Num comando destrutivo
como este, apontar para o dispositivo errado é bem pior do que num upload. Offset e
tamanho vêm de `partitions.csv` — se a tabela mudar, esses números mudam junto.

Apagar a flash inteira (perde firmware e config, exige regravar):

```bash
pio run -t erase
```

## Segurança — o que está em aberto

A senha compartilhada de fábrica acabou (ver acima e em
[PRD 08](prd/08-segredos-por-unidade.md)). Continuam em aberto:

- As senhas ficam gravadas na NVS **sem criptografia**. `CONFIG_NVS_ENCRYPTION` não está
  ligado, e não é flag independente: exige flash encryption, que queima eFuse de forma
  permanente. Quem tiver acesso físico à placa lê a flash e extrai tudo — só que também
  leva o cartão SIM junto, que é o ativo com custo recorrente.
- A página de config usa **HTTP Basic Auth sobre HTTP puro**, sem TLS. As credenciais
  trafegam em base64 no ar, protegidas apenas pelo WPA2 do AP — e a PSK é compartilhada,
  então qualquer cliente já associado consegue capturá-las.

Nada disso é problema em bancada, mas precisa ser resolvido antes de qualquer unidade ir a
campo. Rastreado em [DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md).
