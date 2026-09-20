# PRD 09 — DNS Local para os Clientes do AP

Fonte: débitos 9 e 12 em [DEBITOS_TECNICOS.md](../DEBITOS_TECNICOS.md).

## Objetivo

Fazer com que o endereço de DNS entregue no lease do DHCP seja permanentemente válido, em
vez de válido só enquanto a sessão PPP corrente durar. O caminho é colocar um resolvedor de
verdade no endereço que o cliente já recebe: `192.168.4.1`, o IP do próprio AP.

## O problema, nos dois pedaços em que ele aparecia

**Débito 9 — DNS entregue antes de existir uplink.** Quando a opção 6 do DHCP não está
configurada, o `dhcpserver` da IDF não omite a opção: ele preenche com o IP do próprio AP
(`components/lwip/apps/dhcpserver/dhcpserver.c`, montagem das opções do OFFER — o ramo
`else` de `dhcps_dns_enabled()` emite `ipadd`). Até aqui não havia ninguém escutando nesse
endereço. Um cliente que associasse antes do PPP subir recebia `192.168.4.1` como DNS e não
resolvia nada.

**Débito 12 — a janela de DHCP a cada reconexão.** O contorno para o débito 9 era o
`NatBridge` reescrever a opção 6 com o DNS da operadora ao fim de cada sessão PPP. Mas
`esp_netif_dhcps_option()` recusa com `ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED` enquanto o
servidor estiver de pé (`esp_netif_lwip.c`), então era preciso `dhcps_stop` → reconfigurar →
`dhcps_start` toda vez. Quem tentasse associar dentro dessa janela não pegava endereço
nenhum.

Os dois são o mesmo problema visto de ângulos diferentes: o valor entregue no lease
dependia de um estado que muda. E não dava para resolver encurtando o lease. O padrão da
IDF é `DHCPS_LEASE_TIME_DEF` = 120 minutos (`dhcpserver.h`), T1 ≈ 60 min, então um cliente
que pegou o valor errado fica com ele por até uma hora. Encurtar o lease reduz esse tempo,
mas aumenta o número de renovações — ou seja, **aumenta** a chance de cair na janela do
débito 12. Um débito piorava o outro.

## A decisão

Endereço estável vence valor correto. O IP do AP nunca muda, então se houver um resolvedor
escutando nele, o valor que o `dhcpserver` já entrega por padrão passa a estar certo para
sempre — e o `NatBridge` não precisa mais tocar no DHCP. O débito 12 morre por remoção de
código, não por contorno.

Custo: ~200 linhas de infraestrutura que não existiam. Não há forwarder DNS na IDF 4.4.7 —
o `examples/protocols/http_server/captive_portal/main/dns_server.c` é um sequestrador
(responde o IP do AP para qualquer nome), não um repassador.

## Desenho

`DnsForwarder` (`src/infra/dns_forwarder.h/.cpp`) sobe uma task com dois sockets UDP e uma
tabela de perguntas em voo.

**Bind explícito em `192.168.4.1:53`, nunca `INADDR_ANY`.** Esta é a decisão de segurança
central do arquivo. Com `INADDR_ANY` o mesmo socket atenderia também a interface PPP, e o
roteador viraria um resolvedor aberto exposto à rede da operadora — recurso clássico de
amplificação de DDoS.

**O upstream sai de `dns_getserver(0)`, lido a cada pergunta.** Numa interface
ponto-a-ponto o DNS vive no global do lwIP, escrito pelo PPP no IPCP; é a mesma fonte que
`esp_netif_get_dns_info()` leria. Ler na hora significa que uma reconexão que troque o
servidor da operadora passa a valer na pergunta seguinte, sem ninguém precisar avisar o
forwarder.

**Reescrita do ID da transação.** O ID que vai para o upstream é nosso, não o do cliente:
dois clientes podem escolher o mesmo ID, e a resposta de um voltaria para o outro. O índice
do slot ocupa o byte alto do ID de saída, então dois pendentes nunca colidem.

**Conferência da origem da resposta.** O socket de upstream escuta numa porta efêmera que é
alcançável a partir do AP. Sem comparar o endereço de origem com o upstream esperado, um
cliente do AP poderia forjar a resposta de qualquer domínio para outro cliente.

**SERVFAIL em vez de silêncio.** Sem uplink, com a tabela cheia, ou se o DNS da operadora
for o nosso próprio endereço (guarda de laço), a resposta é SERVFAIL imediato. O cliente
para de esperar e o navegador mostra erro de DNS em vez de travar até o timeout.

**Vencimento silencioso.** Pendente que passa de 5 s só libera o slot. Responder SERVFAIL
no vencimento exigiria guardar a pergunta inteira de cada pendente, e o resolvedor do
cliente já retransmite sozinho.

O parsing de DNS fica no domínio (`src/domain/dns_message.h/.cpp`): ler o ID, decidir se o
datagrama é uma pergunta, e montar o SERVFAIL. É a parte que lida com entrada hostil, e é
onde estão os testes — inclusive os casos de pergunta que passa do fim do datagrama e de
ponteiro de compressão na seção de pergunta, ambos recusados.

## Fora de escopo

- **DNS over TCP.** Resposta truncada faz o cliente reperguntar por TCP, e aí ele fala com
  `192.168.4.1:53/tcp`, onde não há ninguém. Na prática respostas de navegação normal cabem
  em UDP com EDNS0, mas isso é um limite real e não uma omissão esquecida.
- **Cache.** Cada pergunta vira uma pergunta ao upstream. Com 15 clientes o volume não
  justifica a memória, e cache errado é pior que cache nenhum.
- **DNSSEC, DoT, DoH.** O forwarder repassa; a validação, se importar, é do cliente.

## Critérios de aceite

- [x] Cliente que associa antes do PPP subir resolve nomes assim que o uplink conecta, sem
      renovar o lease — validado em hardware em 19/09/2026
- [x] Reconexão PPP não derruba o DHCP do AP (nenhum `dhcps_stop` no caminho) — verificado
      por inspeção: não existe mais nenhuma chamada a `dhcps_*` em `src/`
- [ ] Cliente associado durante uma reconexão pega endereço normalmente
- [ ] Sem uplink, `nslookup` contra `192.168.4.1` devolve SERVFAIL rápido, não timeout
- [ ] Da rede da operadora, a porta 53 do endereço PPP não responde
- [x] `pio test -e native` cobre datagrama truncado, ponteiro de compressão e estouro do
      buffer de saída — 11 testes em `test/test_dns_message/`

## Validação em hardware — 19/09/2026

O AP sobe em ~1,2 s e o uplink fechou em 15,3 s, então a janela do débito 9 era de 14 s.
Celular associado dentro dela recebeu `192.168.4.1` no lease e resolveu nome assim que o
`Uplink: online` apareceu — sem renovar o lease e sem reconectar, que é exatamente o que
não acontecia antes. O forwarder subiu em silêncio, como projetado: ele só escreve no log
quando falha, e nenhuma linha `DNS:` saiu no boot.

Os três critérios restantes continuam sem teste de bancada.
