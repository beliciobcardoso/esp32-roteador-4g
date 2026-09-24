# Testar a atualização de firmware pela página

Como gerar, com um comando cada, os arquivos que exercitam todos os caminhos do upload — os
que a placa recusa e o que ela grava de verdade. Os inválidos são instantâneos e não tocam a
flash; o válido reinicia a placa.

As regras que recusam cada arquivo estão em `domain/firmware_update` (`inspectImageHead` e
`inspectImageSize`), com teste nativo. Esta página é como provocá-las pela interface.

## O arquivo válido

Sai do build normal. Não há nada a gerar:

```bash
pio run -e esp-wrover-kit
```

O arquivo fica em `.pio/build/esp-wrover-kit/firmware.bin`, com quase 1 MB. É o mesmo
binário que o `--target upload` grava por serial.

Enviar **a imagem que já está rodando** continua sendo um teste válido, e é o mais seguro:
o que muda visivelmente é o slot (`app0` ↔ `app1`) e o estado da imagem. Se algo der errado,
o firmware para o qual a placa volta é o mesmo.

## Os arquivos que a placa recusa

Copie e cole. Os três primeiros cabem em qualquer lugar; o quarto ocupa 2 MB.

```bash
mkdir -p ~/Downloads/ota-teste && cd ~/Downloads/ota-teste

# 1. Vazio — nenhuma parte de arquivo chega ao servidor.
: > vazio.bin

# 2. Curto demais: menos que os 24 bytes do cabeçalho do ESP32.
head -c 10 /dev/zero > curto-demais.bin

# 3. Não é imagem do ESP32: primeiro byte diferente de 0xE9, mas comprido
#    o bastante para passar da checagem de tamanho e chegar na de magic.
{ printf 'NAO-E-FIRMWARE'; head -c 200 /dev/zero; } > nao-e-imagem.bin

# 4. Maior que a partição de destino (1.900.544 bytes). Começa com o magic
#    certo para ser recusado pelo tamanho, e não pelo cabeçalho.
{ printf '\351'; head -c 2000000 /dev/zero; } > grande-demais.bin

ls -lh
```

O `printf '\351'` é o 0xE9 em octal — é o magic que todo binário de aplicação do ESP32
carrega no primeiro byte.

## O que cada um tem que mostrar

| arquivo | regra que pega | mensagem na página | linha no serial |
|---|---|---|---|
| `vazio.bin` | `EmptyImage` | `nenhum arquivo foi enviado` | `OTA: recusado (sem_arquivo) \| 0 B recebidos` |
| `curto-demais.bin` | `TooShortToBeAnImage` | `arquivo pequeno demais para ser um firmware` | `OTA: recusado (curto_demais) \| 10 B recebidos` |
| `nao-e-imagem.bin` | `NotAnEspImage` | `arquivo não é uma imagem de firmware do ESP32 (envie o firmware.bin)` | `OTA: recusado (nao_e_imagem_esp32) \| 214 B recebidos` |
| `grande-demais.bin` | `TooLargeForSlot` | `firmware maior do que a partição de destino` | `OTA: recusado (maior_que_o_slot) \| 2000001 B recebidos` |
| `firmware.bin` | passa | `Firmware gravado. A placa reinicia agora…` | `OTA: gravado \| <tamanho> B \| reiniciando` |

Toda requisição ao `/update` deixa **uma** linha `OTA:` no serial, dê certo ou não (débito 23).
Envio sem credencial sai como `OTA: recusado (sem_credencial)`. Se a página mostrou um
resultado e o serial não tem a linha, a requisição não chegou à placa — foi o navegador.

Nos quatro primeiros a barra de progresso some e a mensagem fica vermelha **até você tocar
nela** — erro não some sozinho. Nada é gravado: as duas primeiras checagens rodam no
primeiro bloco do upload, antes de a partição de destino ser aberta.

O `grande-demais.bin` é o único que gasta tempo: o corpo inteiro sobe mesmo depois de
recusado, porque o parser do `WebServer` lê a requisição até o fim. A recusa em si acontece no
bloco que passa do tamanho do slot, antes de escrevê-lo. Até 24/09/2026 ela só era conferida
no fim, e esse arquivo nunca chegava lá: o gravador estourava antes, e a página respondia
"arquivo grande demais **ou flash com defeito**" — um erro de arquivo com cara de defeito de
hardware. A linha do serial do débito 23 foi o que mostrou.

## O caminho do sucesso, passo a passo

1. Envie o `firmware.bin`. A barra anda até 100% em cerca de um minuto pelo AP
2. A mensagem manda confirmar em até 10 minutos, e **não some sozinha** — ela instrui
3. A placa reinicia e o Wi-Fi cai. Reconecte ao AP e reabra a página
4. Na aba **Firmware**: o slot trocou, e o estado é `Em verificação`
5. No `<nav>`, a aba Firmware carrega um ponto — é assim que quem está no Status descobre
   que há algo pendente

A partir daqui, dois desfechos, e vale exercitar os dois em dias diferentes:

- **Confirmar**: o estado vira `Confirmado` e a placa fica no slot novo
- **Não confirmar**: em 10 minutos ela reinicia sozinha e volta ao slot anterior. É o
  rollback do bootloader funcionando, e como a imagem é a mesma, não se perde nada

O prazo é o `kConfirmationDeadlineMs` de `domain/firmware_update`. A página não o traz
escrito à mão: ele viaja no JSON, é o mesmo número que o `loop()` usa para decidir, e se a
constante mudar a frase muda junto.

## Upload interrompido

Desligue o Wi-Fi do celular no meio do envio. A página tem que dizer
`O envio foi interrompido antes do fim. Nada foi gravado.`

Não confunda com o fim normal: quando o envio termina e a placa reinicia antes de responder,
a mensagem é outra — `Envio concluído, mas a placa não respondeu…`. A página distingue os
dois pelo momento em que a conexão caiu, e essa distinção existe porque a mensagem errada
convidava a reenviar um firmware que já estava na flash.

## Levar os arquivos para o celular

O celular precisa ter os arquivos localmente — o seletor do navegador lê do próprio
aparelho. Cabo USB, ou qualquer serviço de arquivos, antes de associar ao AP do roteador.
