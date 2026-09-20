# PRD 11 — Atualização de firmware pela página (OTA por upload)

**Status: implementada em 19/09/2026 — validação em hardware pendente**

Fonte: continuação do plano de fases, 19/09/2026.

## Objetivo

Permitir trocar o firmware da unidade sem cabo. A placa fica em campo com link celular
próprio; gravar por serial a cada correção obriga deslocamento, e é isso que hoje torna
qualquer bug de campo caro.

É também o que destrava o débito 6: os dois slots de OTA existem desde a Fase 4, mas o
rollback do bootloader estava desligado justamente porque não havia quem confirmasse a
imagem nova.

## Escopo

- `domain/firmware_update.{h,cpp}` — o que a placa aceita como imagem, e o que ela diz
  sobre a que está rodando
- `adapters/firmware_writer.h` — porta; `infra/ota_updater.{h,cpp}` — implementação sobre
  o `Update` do Arduino e o `esp_ota_*`
- `POST /update` no mesmo `WebServer`, com Basic Auth conferido **antes** da primeira
  escrita na flash
- Bloco de firmware no `GET /`: slot em execução, versão, data de compilação e estado da
  imagem
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` e a confirmação da imagem no `loop()`

## Depende de

PRD 03 (página de configuração com Basic Auth) e a tabela de partições da Fase 4.

## Fora de escopo

- **OTA por pull** (a placa busca uma URL pelo 4G). Precisa de hospedagem, CA bundle,
  publicador e política de quando buscar — é o sucessor natural, não esta fase.
- **Montar a partição `spiffs`** (débito 7). O firmware novo vai para `app0`/`app1`; o
  upload passa direto do socket para a flash, sem arquivo intermediário.
- **Histórico de versões instaladas.** Nada disso persiste na NVS — não há campo novo e
  `kCurrentSchema` continua 3.

## Decisões já tomadas

### Push e não pull

O navegador manda o `firmware.bin` pelo AP. Pull exige um servidor com certificado válido e
alguém responsável por publicar; push funciona com o que já está no ar hoje, e o operador
que está em frente à unidade é quem decide quando atualizar.

### Sem checksum no formulário

`Update.end()` chama `esp_ota_set_boot_partition()`, que roda `esp_image_verify()` antes de
tocar no otadata (`image_validate()`, `app_update/esp_ota_ops.c`), e essa verificação
confere o SHA-256 que a própria imagem carrega (`hash_appended`, `esp_app_format.h`). Um
campo de MD5 no formulário seria um segundo checksum dos mesmos bytes, com a desvantagem de
alguém precisar calculá-lo à mão.

## Decidido na implementação

- **A credencial é conferida no primeiro bloco do upload, não no handler que responde.** O
  `WebServer` chama o handler de upload de dentro do `_parseForm()`, durante a leitura da
  requisição (`Parsing.cpp`), e só invoca o handler de POST depois do corpo inteiro. Quem
  autentica só no fim grava ~900 KB no slot de OTA de quem não se identificou, e só então
  recusa. O tráfego não dá para evitar — o parser lê o corpo de qualquer jeito —, mas a
  escrita na flash dá.
- **A imagem é confirmada depois de 120 s de pé, não no `setup()`.** O plano original dizia
  `setup()`. Escrever o código mostrou que ali o rollback pega só o firmware que morre antes
  de o AP subir — e o defeito que interessa aparece depois, no primeiro ciclo de bateria, na
  primeira resposta HTTP. O teto do prazo vem do `LinkSupervisor`, que reinicia a placa após
  10 falhas de uplink: com backoff de 5/10/20/40/60 s isso passa de 7 min, e esse reboot é
  por falta de sinal, não defeito do firmware. Uma janela que encostasse nele faria uma área
  sem cobertura reverter uma atualização boa.
- **O exame da imagem no domínio é raso de propósito, e em duas partes.** `inspectImageHead()`
  roda no primeiro bloco (magic `0xE9` e tamanho mínimo de cabeçalho — é o que pega o `.elf`
  subido no lugar do `.bin`, que começa com `0x7F`); `inspectImageSize()` roda no fim, porque
  o `WebServer` não expõe o `Content-Length` e, mesmo se expusesse, ele mede o corpo
  multipart inteiro e não a imagem. Quem valida de verdade é o `esp_image_verify()`.
- **O reboot sai do `loop()`, não do handler.** `esp_restart()` dentro do handler cortaria a
  resposta ainda na fila do socket: o navegador mostraria erro de conexão depois de uma
  atualização que deu certo. O adaptador avisa por ponteiro de função, como já faz para o
  uplink, e o `main.cpp` reinicia uma volta depois.
- **`FirmwareWriter` é porta em `adapters/`, no molde do `settings_repository.h`.** O handler
  precisa de seis operações sobre a gravação; seis ponteiros de função seriam pior do que a
  interface, e depender do `OtaUpdater` concreto inverteria a direção que o projeto cobra.

## Riscos conhecidos

- **A senha de admin passa a valer execução de código.** Antes ela reconfigurava o roteador;
  agora troca o firmware. A página continua em HTTP puro sobre um AP de PSK compartilhada
  (débito 10), então qualquer cliente associado que capture o handshake de outro consegue as
  credenciais e, com elas, a placa inteira. Aceitável em bancada, bloqueante para campo — e
  registrado como tal no débito 10, ao lado do TLS.
- **Queda de energia dentro da janela de 120 s reverte a atualização.** É o comportamento
  desejado do rollback, mas quem não souber disso vai achar que o upload falhou. A resposta
  do `POST /update` diz o prazo, calculado do mesmo `kVerificationWindowMs`.
- **Upload pelo AP com o 4G ativo disputa CPU e rádio.** Não medido. Se der problema, o
  sintoma é upload lento ou queda do PPP durante a gravação, não firmware corrompido — o
  `esp_image_verify()` fecha esse caminho.

## Critérios de aceite

- [x] `POST /update` sem credencial não escreve um byte na flash — a autenticação acontece
      no `UPLOAD_FILE_START`, antes do `begin()` do slot
- [x] Arquivo que não é imagem do ESP32 é recusado com frase em português, e o formulário
      enviado sem arquivo também
- [x] `pio test -e native` cobre o exame da imagem, a tabela de estados e a janela de
      verificação, incluindo o teste que amarra a janela ao piso de reboot do supervisor —
      106 testes
- [x] `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` aplicado no `sdkconfig` gerado
- [ ] Upload de um `firmware.bin` válido pela página → placa reinicia na imagem nova e o
      `GET /` mostra o outro slot
- [ ] Dentro da janela de 120 s a página mostra "em verificacao"; depois dela, "confirmado"
- [ ] Reset físico dentro da janela → a placa volta para o firmware anterior
- [ ] Upload de arquivo truncado → recusado no fim, e a placa continua no firmware atual
- [ ] Upload interrompido no meio (cabo/Wi-Fi) → a placa continua no firmware atual

Os cinco últimos são de bancada e ficam abertos até a placa rodar esta versão.
