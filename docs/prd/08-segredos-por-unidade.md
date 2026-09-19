# PRD 08 — Segredos por Unidade

Fonte: débito 10 em [DEBITOS_TECNICOS.md](../DEBITOS_TECNICOS.md).

**Status: parcial** — os itens reversíveis estão implementados; a criptografia de NVS
depende de queimar eFuse e está documentada aqui sem ser executada.

## Objetivo

Tirar do firmware a premissa de que toda unidade nasce com a mesma senha. Hoje o
`config.h` versiona `roteador4g` e `admin1234`, iguais em toda placa e públicos para quem
tem o repositório — quem clona o projeto sabe entrar em qualquer unidade que ninguém
reconfigurou.

## Modelo de ameaça

Sem isso escrito, cada item vira discussão de gosto. As ameaças em ordem de probabilidade:

1. **Vizinho de rádio.** Vê o SSID, não tem nenhuma credencial. É contra ele que a senha
   de AP por unidade vale — e é a ameaça mais provável, porque não exige nada além de estar
   perto.
2. **Cliente associado ao AP.** Tem a PSK (é como ele associou). Com a PSK e o 4-way
   handshake dos outros clientes, decripta o tráfego deles: a senha de admin em Basic Auth
   sobre HTTP puro está exposta a ele. Não é hipótese remota — é todo mundo a quem já se
   deu o WiFi.
3. **Quem tem a placa na mão.** Faz dump da flash e lê a NVS. É contra ele que a
   criptografia de NVS vale. Só que ele também tem o cartão SIM, que é o ativo com custo
   recorrente. Proteger a NVS sem tirar o SIM da conta é proteger a fechadura e deixar a
   janela aberta.

A ordem acima é o que justifica fazer o item 1 agora, o item 2 depois e o item 3 por
último — é o inverso da ordem em que o débito 10 os lista.

## Escopo desta entrega

- Senha do AP sorteada por unidade no primeiro boot, persistida na NVS
- Senha de admin sorteada por unidade no primeiro boot, com troca obrigatória no primeiro
  acesso à página de configuração
- Credenciais geradas impressas uma vez no serial, no boot em que foram criadas
- Geração de aleatoriedade com entropia real — não com o MAC, não com `millis()`

## Fora de escopo (e por quê)

### Senha derivada do MAC — rejeitada

O débito 10 sugere "senha de AP derivada por dispositivo (ex.: sufixo do MAC)". Isso não
protege contra a ameaça 1. O MAC do SoftAP é o MAC base com o último octeto incrementado
(`esp_hw_support/mac_addr.c`, `case ESP_MAC_WIFI_SOFTAP: mac[5] += 1`), e esse MAC é o
BSSID, transmitido em todo beacon. Qualquer pessoa no alcance lê o BSSID num scan passivo
e calcula a senha. Seria trocar uma senha pública no GitHub por uma senha pública no ar.

Segredo por unidade precisa de um valor que não esteja em nenhum lugar público: sorteado.

### TLS na página de configuração — adiado

Fecha a ameaça 2 e não tem substituto: qualquer coisa sobre HTTP puro dentro de um AP com
PSK compartilhada está exposta aos próprios clientes. Fica fora daqui porque troca o
`WebServer` do Arduino inteiro por `esp_https_server`, custa RAM de TLS e traz certificado
autoassinado (aviso no navegador em todo acesso). É um PRD próprio, não um item de lote.

### Criptografia de NVS — bloqueada por eFuse

`CONFIG_NVS_ENCRYPTION` não é uma flag independente:

```
config NVS_ENCRYPTION
    depends on SECURE_FLASH_ENC_ENABLED
```

(`components/nvs_flash/Kconfig`, ESP-IDF 4.4.7). Ligar criptografia de NVS exige ligar
flash encryption, e flash encryption **queima eFuse: é permanente e por placa**. O que
muda depois de queimado:

- **Release mode:** regravação em texto claro deixa de existir. `pio run --target upload`
  como está hoje para de funcionar; passa a exigir imagem criptografada ou OTA. Firmware
  ruim que quebre o OTA vira recuperação pelo slot A/B, e só.
- **Development mode:** permite um número limitado de regravações em claro (contador em
  eFuse), e depois recai no caso acima.

O que já está pronto para quando a decisão for tomada:

- A partição `nvs_keys` (0x1000, flag `encrypted`) cabe no vão de 56 KB em `0x12000` que o
  comentário do [partitions.csv](../../partitions.csv) descreve como inevitável. Cabe **sem
  mover** `nvs` nem `phy_init` — mover esses dois é que forçaria `erase_flash` e apagaria a
  configuração do usuário.
- Não há mudança de código. Com `CONFIG_NVS_ENCRYPTION`, o `nvs_flash_init()` acha a
  partição de chaves, gera as chaves no primeiro boot se estiver vazia e chama
  `nvs_flash_secure_init_partition()` (`nvs_flash/src/nvs_api.cpp`). O `Preferences`
  continua igual.

Ou seja: o trabalho restante é decisão, não implementação.

## Decisões de projeto

### O sorteio acontece antes do WiFi subir, e isso importa

`esp_random()` só devolve número aleatório de verdade com Wi-Fi ou Bluetooth ligado — está
no contrato da função (`esp_hw_support/include/esp_random.h`). A senha do AP é necessária
*para* subir o AP, então o sorteio é obrigatoriamente anterior ao rádio, justo na janela em
que a RNG é fraca.

A saída documentada pela própria IDF é `bootloader_random_enable()`, que usa o SAR ADC como
fonte de ruído enquanto o RF está desligado. O contrato exige `bootloader_random_disable()`
**antes** de inicializar RF, ADC ou I2S — por isso o provisionamento roda no topo do
`setup()`, antes de `batteryAdc.begin()` e antes do SoftAP.

Sem esse par, o firmware geraria senhas com aparência de aleatórias e entropia baixa, e
nada no comportamento denunciaria o problema. É o tipo de defeito que não aparece em teste.

### Sem bump de schema da NVS — de propósito

A flag de "senha de admin ainda é a sorteada" é uma chave nova na NVS. Subir
`kCurrentSchema` para 3 resolveria a leitura, mas o `load()` trata `schema < kCurrentSchema`
como registro ausente: **toda unidade já configurada perderia SSID, senha e APN salvos**.
Migração destrutiva por causa de um booleano não se paga.

A chave nova é lida com default para registro antigo, que é compatível por construção.
Consequência registrada: unidade de bancada que já tem `admin1234` gravado na NVS continua
com ela — provisionamento novo só vale para NVS vazia. Para essas, o caminho é apagar a
partição (`esptool erase_region`, ver [CONFIGURACAO_NVS.md](../CONFIGURACAO_NVS.md)), o que
é operação manual e consciente, não migração silenciosa.

### A geração do texto da senha é regra de domínio

Transformar bytes aleatórios em senha legível é decisão de produto (alfabeto, comprimento,
quais caracteres evitar), não detalhe de hardware. Mora em `domain/secret.h` e tem teste
nativo. O que é hardware — de onde vêm os bytes — fica em `infra/`.

O alfabeto é `ABCDEFGHIJKLMNOPQRSTUVWXYZ234679` — 26 letras maiúsculas mais seis dígitos.
Ficaram de fora os dígitos `0`, `1`, `5` e `8`, que colidem com `O`, `I`, `S` e `B` na
transcrição à mão; a escolha foi manter a letra e descartar o dígito, então nenhum desses
quatro pares aparece. Essas senhas são lidas de log serial ou de etiqueta e digitadas por
uma pessoa: confundir um caractere numa senha de AP custa uma viagem até o equipamento.

O tamanho 32 também não é estético. `256 % 32 == 0`, então o mapeamento byte → símbolo é
uniforme e não perde entropia por viés de módulo — um alfabeto de 31 ou 33 símbolos daria
senhas com aparência idêntica e distribuição torta. Há teste nativo amarrando os dois
pontos.

### Senha vazia não é senha

Tirar o default de fábrica criou um caso que não existia: `RouterSettings` com senha vazia.
Ele aparece no fallback do `LoadSettingsUseCase` quando a NVS fica ilegível, e é intencional
— `validate()` reprova e o AP não sobe. Mas o `authenticate()` do handler compararia contra
`""` e deixaria entrar quem mandasse `admin:` sem senha. No boot o AP nem sobe, então
ninguém chega lá; uma falha de leitura com o AP já no ar chegaria, e o erro de
armazenamento viraria porta aberta. Por isso `authenticate()` recusa usuário ou senha vazios
antes de comparar qualquer coisa.

## Consequência operacional (ler antes de gravar em placa nova)

Depois desta mudança, **não existe mais senha conhecida de antemão**. Numa placa com NVS
vazia, as credenciais aparecem uma única vez no serial, no boot em que são criadas:

```
=== Provisionamento (primeiro boot) ===
AP    "esp32-roteador-4g"  senha: <sorteada>
Admin "admin"              senha: <sorteada> (troca obrigatoria no primeiro acesso)
=======================================
```

Quem gravar a placa sem o monitor serial aberto não vê esse bloco, e a única saída é apagar
a NVS e reprovisionar. Não é bug: é o preço de não ter senha padrão. Em campo, esse é o
momento de gerar a etiqueta da unidade.

## Critérios de aceite

- [ ] Duas placas com NVS vazia sobem com senhas de AP diferentes
- [ ] A mesma placa reiniciada mantém a senha sorteada (persistiu, não regerou)
- [ ] O primeiro acesso à página de configuração recusa POST que não troque a senha de admin
- [ ] Depois da troca, a exigência não volta em acesso nenhum
- [ ] Unidade já configurada antes desta versão continua com sua configuração intacta
- [ ] `pio test -e native` cobre a geração de senha a partir de bytes
- [ ] Config com senha de admin vazia não autentica ninguém
