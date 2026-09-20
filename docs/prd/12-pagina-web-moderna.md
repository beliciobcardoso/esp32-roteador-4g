# PRD 12 — Página de configuração moderna (HTML5/CSS3/JS + API JSON)

**Status: em implementação**

Fonte: conversa de 20/09/2026, logo após o PR #6 tornar `http/index.html` a fonte única
da página.

## Objetivo

A página de configuração é a única interface da unidade em campo. Hoje ela é um formulário
sem estilo, com três `<div>` empilhados (`#home`, `#status`, `#config`) que aparecem todos
de uma vez, e um `<nav>` que aponta para `/status` e `/config` — rotas que o handler nunca
registrou. Quem abre a página no celular, que é o cliente real do AP, vê os três blocos em
sequência e leva 404 em dois dos três links.

Esta fase troca a página por uma aplicação de arquivo único com HTML5, CSS3 e JavaScript,
e move os dados do firmware para uma API JSON. O ganho não é estético: é de arquitetura e
de heap.

## O problema, nos dois pedaços em que ele aparece

### 1. Interpolação de placeholder não escala em bytes

`handleGetRoot()` faz `String page = kConfigPageTemplate;` e em seguida ~13 `.replace()`.
Cada `replace()` do Arduino `String` que muda o tamanho realoca o buffer inteiro e copia.
Com os 4 KB de hoje isso passa despercebido. Com uma página de 25–40 KB são 13 realocações
de dezenas de KB por request, no mesmo heap em que o PPP aloca seus buffers de entrada.
Fragmentação de heap no ESP32 não aparece em bancada calma: aparece depois de horas de
tráfego, como um `malloc` que falha na hora errada.

Interpolar também impede cache: cada GET devolve bytes diferentes, então não há `ETag`
estável, e o celular rebaixa 30 KB a cada visita pelo próprio link 4G que estamos tentando
não desperdiçar.

E é a interpolação que obriga o escape de `{` em `escapeForHtmlAttribute` — aquele
`case '{': escaped += "&#123;"` existe só para impedir que um SSID gravado como `{{APN}}`
seja trocado pelo replace seguinte. Some a interpolação, some a classe de bug.

### 2. O `<nav>` mente

`begin()` registra `/` (GET e POST), `/update` e `/firmware/confirmar`. O `<nav>` oferece
`/`, `/status` e `/config`. Dois dos três links caem no `handleNotFound()`.

## A decisão

**Página estática servida como está, dados por API JSON.**

- `GET /` devolve `http/index.html` byte a byte, sem nenhum `replace`. Com isso a resposta
  pode ser servida direto do ponteiro do `.rodata` embutido, sem cópia para o heap, e ganha
  `ETag` fixo (o hash do build) mais `Cache-Control`. Segunda visita é um `304`.
- `GET /api/status` — o que muda sozinho: uplink, relógio, bateria, firmware.
- `GET /api/config` — o que o formulário edita. **Nunca devolve senha**, nem mascarada: o
  campo vem ausente e a página mostra o placeholder "deixe em branco para manter", igual
  ao comportamento atual do POST.
- `POST /api/config` — grava, reaproveitando o `SaveSettingsUseCase` e o `validate()` do
  domínio sem mudança. Erro volta como JSON com a mensagem de
  `to_string(SettingsValidationError)`, que já existe.

  **O corpo do POST continua `application/x-www-form-urlencoded`, não JSON** — desvio do
  que este PRD dizia antes da implementação. O `WebServer` já parseia esse formato em
  `server_.arg()`, enquanto aceitar JSON exigiria escrever um parser no firmware
  (aninhamento, escapes, `\uXXXX`) para oito campos planos que o navegador codifica com
  `URLSearchParams` em uma linha. Escrever um parser aqui seria superfície de bug nova em
  troca de simetria. As respostas são JSON em todas as rotas.
- `POST /update` e `POST /firmware/confirmar` — ficam como estão no protocolo
  (`multipart/form-data` e form POST). O que muda é o cliente: a página passa a enviar por
  `XMLHttpRequest` para ter `upload.onprogress`, que o `fetch()` não expõe.

Navegação vira client-side por hash (`#/`, `#/status`, `#/config`), com os links do `<nav>`
apontando para o hash. Isso resolve o 404 sem inventar rotas no servidor que devolveriam a
mesma página.

### Por que não manter os placeholders

Modernizar mantendo o `replace` foi considerado e recusado: é exatamente o caminho que
multiplica o custo de heap por 8 e não entrega cache nem status ao vivo. O híbrido
(config por placeholder, status por API) foi recusado por manter dois mecanismos vivos
para a mesma coisa no mesmo arquivo.

### Por que arquivo único

`EMBED_TXTFILES` continua com um arquivo só, CSS e JS inline. A página de configuração é
acessada raramente e por poucos clientes; cache granular por arquivo não paga três rotas,
três embeds e um passo de gzip no build. Gzip fica registrado como débito, não como
escopo: ele exigiria `EMBED_FILES` binário e um `.gz` gerado, e um `.gz` versionado
reintroduz a cópia duplicada que o PR #6 acabou de eliminar.

## Escopo

- `http/index.html` — aplicação de arquivo único: HTML5 semântico, CSS3 com custom
  properties e `prefers-color-scheme`, JS sem dependência externa
- `adapters/http_config_handler` — rotas novas de API, `GET /` sem interpolação, `ETag`
- `domain/` — serialização JSON das respostas, com teste nativo. Mora no domínio porque
  escapar JSON é regra pura e é justamente o que precisa de teste sem placa
- `adapters/html_page` — `escapeForHtmlAttribute` perde a razão de existir e sai junto com
  o último `replace`

### Layout

Desktop e celular, com o celular como caso principal: é o cliente real do AP. Um `<main>`
em grid que colapsa para coluna única abaixo de 640 px, alvos de toque de 44 px, e nada de
fonte ou folha de estilo externa — a página tem que abrir antes de o uplink 4G subir, e
com o uplink caído ela é a única coisa que o operador consegue ver.

### Status ao vivo

Polling de `GET /api/status`. Intervalo de 3 s enquanto a aba está visível, pausado em
`document.hidden` — sem essa pausa uma aba esquecida aberta bate no `handleClient()` de um
loop que também roda a supervisão do uplink.

Campos: estado do uplink (já vem pronto de `describeUplinkStatus()`), falhas consecutivas,
relógio ou a frase de "ainda não sincronizou", tensão da bateria, slot e estado da imagem
de firmware.

### OTA com progresso

`XMLHttpRequest` com `upload.onprogress`, barra de progresso e mensagem de erro do corpo
da resposta. Hoje o submit é cego: o navegador fica com a página travada até o reboot, e
uma falha de upload aparece como página em branco.

## Fora de escopo

- Gzip dos assets e `Content-Encoding` — vira débito
- WebSocket ou SSE para o status: polling de 3 s resolve, e um socket aberto por cliente
  custa memória num ESP32 que já roda PPP, NAT e DNS
- Mudar o Basic Auth. Ele continua cobrindo as rotas novas de API — `fetch()` com
  `credentials: 'same-origin'` reenvia a credencial que o navegador já tem
- Internacionalização
- Qualquer mudança em `domain/router_settings` ou no schema da NVS. Nenhum campo novo
  entra nesta fase, então não há degrau de migração para escrever

## Riscos

- **Heap durante o POST de config.** O corpo JSON chega inteiro na memória pelo
  `WebServer`. Limitar o tamanho aceito e recusar acima disso, em vez de confiar no cliente
- **Página grande sem gzip.** O orçamento é 40 KB de `.rodata`; a flash está em 49,4% de
  1,9 MB, então cabe, mas o número tem que ser medido e registrado, não estimado
- **JS obrigatório.** A página deixa de funcionar com JS desligado. É aceito: o cliente é
  um celular moderno associado ao próprio AP, não um navegador de museu
- **Ordem de gravação.** Trocar SSID ou senha do WiFi derruba o cliente que está gravando.
  Já é assim hoje; a página agora tem que avisar antes, porque o `fetch()` não tem a
  desculpa do navegador de mostrar erro de conexão

## Critérios de aceite

1. `GET /` não faz nenhum `replace` e responde `304` na segunda visita com `If-None-Match`
2. Os três links do `<nav>` funcionam; nenhum 404
3. Formulário grava e o valor volta persistido após recarregar
4. Gravação inválida (SSID vazio, fuso desconhecido, ratio fora da faixa) mostra a
   mensagem do domínio, e nada é gravado
5. Senha nunca aparece no JSON de `GET /api/config`, nem mascarada
6. Status atualiza sozinho com a aba visível e para com a aba escondida
7. Upload de firmware mostra progresso, e upload interrompido mostra erro em vez de
   página em branco
8. Página legível em 360 px de largura e em desktop, nos dois esquemas de cor
9. `pio test -e native` passando, com teste novo cobrindo a serialização JSON
10. Tamanho final do `.rodata` embutido medido e registrado aqui

## Tamanho medido

`http/index.html` ficou em **32.491 bytes**, dentro do orçamento de 40 KB. Confirmado no
binário: `_binary_index_html_end - _binary_index_html_start` = 32.492, os bytes do arquivo
mais o terminador. A flash do app saiu de 49,4% para **51,1%** de 1,9 MB — +27 KB, que é o
tamanho da página nova menos os 4 KB da antiga. RAM estática inalterada em 10,6%.

## Validação fora da placa

Feita em 20/09/2026 contra um servidor de mentira que devolve as mesmas rotas e os mesmos
campos do firmware. Cobre o que não depende de hardware:

- Renderização em 360 px e em desktop, nos dois esquemas de cor, sem scroll horizontal
- Gravação: `POST` sai, mensagem do firmware aparece, campos de senha são limpos
- Aviso de troca de Wi-Fi: não aparece numa gravação comum, aparece ao mudar o SSID
- OTA: progresso chega a 100% e mostra a mensagem; erro 400 esconde a barra e mostra o
  motivo que veio do firmware
- Confirmação de firmware: mostra a resposta
- Queda do polling: a tela é marcada como velha e **mantém** os números em vez de zerá-los,
  e se recupera sozinha quando as respostas voltam

Dois defeitos foram encontrados e corrigidos nessa passagem:

1. O aviso de troca de Wi-Fi disparava em toda gravação. `input.defaultValue` reflete o
   atributo do HTML, que nunca é escrito quando o campo é preenchido pela propriedade
   `.value` — ficava sempre vazio, e cancelar o aviso abortava um salvamento legítimo
2. Em 360 px o valor "não" quebrava em "nã" e "o". As linhas de `dt`/`dd` eram um flex com
   `space-between` e `word-break: break-all`; viraram grid `1fr auto` com
   `overflow-wrap: anywhere`, onde quem quebra é o rótulo e não o valor

## Validação em hardware

Pendente.
