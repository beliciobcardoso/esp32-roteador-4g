# PRD 05 — NAT / Roteamento

**Status: concluída**

Fonte: [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md), Fase 5.

## Objetivo

Rotear tráfego dos clientes WiFi (AP) pra internet através da interface PPP, via NAPT (habilitado em `sdkconfig.defaults`: `LWIP_IPV4_NAPT`, `LWIP_IP_FORWARD`).

⚠️ Este PRD citava um terceiro símbolo, `LWIP_NAPT`, que **não existe** no Kconfig do lwIP — foi removido durante a implementação. O que vale é `LWIP_IPV4_NAPT`, que já faz `select LWIP_L2_TO_L3_COPY` sozinho (`lwip/Kconfig:215`); sem essa cópia L2→L3 o driver WiFi entrega buffers que o forward não pode reescrever, então era um requisito silencioso que ninguém tinha listado.

## Escopo

- `infra/nat_bridge.{h,cpp}` — habilita NAPT entre `esp_netif` da AP e `esp_netif` do PPP
- ~~`usecases/start_routing.{h,cpp}`~~ — **não foi criado**, ver "Desvios" abaixo

## Depende de

PRD 01, 02, 04.

## Fora de escopo

Testes de carga com múltiplos clientes, reconexão automática (fase 6).

## Critérios de aceite

- Celular conectado no AP navega na internet através do modem 4G
- Ordem de subida (`start_routing`) respeitada: settings antes de AP, AP antes de modem, modem antes de NAT
- Falha em qualquer etapa da orquestração não deixa o sistema em estado parcial silencioso (loga em qual etapa falhou)

## Desvios em relação ao escopo original

### 1. `usecases/start_routing` não existe; a orquestração ficou em `main.cpp`

O escopo pedia um caso de uso para orquestrar `WifiAp` + `ModemPpp` + `NatBridge`. Isso
esbarrava na regra de camadas do [AGENTS.md](../../AGENTS.md): *"`usecases/` — depende só
de interfaces, nunca de implementação concreta"*. As três opções eram:

- **(a)** `start_routing` falando direto com as classes concretas — viola a regra de camadas
- **(b)** criar três interfaces para isolá-las — viola *"não criar abstração antes de duas
  ocorrências reais a justificarem"*; nunca vai existir um segundo modem ou um segundo AP
  neste hardware
- **(c)** deixar a orquestração no `main.cpp`, que já é o ponto de injeção

Escolhida a **(c)**. A sequência aqui é side-effect em hardware, não regra de domínio:
criar uma camada só para cumprir o desenho adicionaria indireção sem adicionar teste,
troca de implementação ou regra de negócio. `startRouting()` vive em
[src/main.cpp](../../src/main.cpp).

Os critérios de aceite de ordem e de falha explícita continuam valendo e foram atendidos —
só não por um arquivo em `usecases/`.

### 2. A flag de NAPT vai na interface **AP**, não na PPP

Contraintuitivo o bastante para valer registro. O comentário do próprio lwIP em
`ip4.c:385` é explícito: *"if the output netif uses NAPT, we will not perform NAPT
forwarding"*. Na saída (`ip4.c:387`) a tradução acontece quando o netif de **saída** não
tem a flag; na entrada (`ip4.c:599`) a destradução acontece quando o netif de **entrada**
não tem. A flag marca a interface **interna**. Ligar no PPP inverteria os dois sentidos.

`ip_napt_enable()` é chamado via `esp_netif_tcpip_exec()`, não direto: ele percorre
`netif_list` e escreve em `netif->napt`, estrutura que só a tarefa lwIP pode tocar com
segurança.

### 3. DNS via DHCP entrou no escopo

Não estava no PRD, mas sem isso o critério de aceite não fecha. O `dhcpserver` **sempre**
emite a opção 6; sem `OFFER_DNS` ele a preenche com o IP do próprio AP
(`dhcpserver.c:383-395`) — e não há resolvedor escutando em 192.168.4.1. O cliente pegaria
IP, rotearia por NAT e mesmo assim não resolveria nome nenhum.

A ordem importa: `esp_netif_dhcps_option()` recusa com `ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED`
enquanto o servidor estiver de pé (`esp_netif_lwip.c:1929`), então é
`dhcps_stop → set_dns_info → dhcps_option → dhcps_start`.

### 4. `ModemPpp` ganhou `waitForIp()`

O DNS da operadora só chega no IPCP, depois do `set_mode(DATA)`. O `NatBridge` depende
dele, então a orquestração precisa de um ponto de sincronização. `start()` continua não
bloqueando; quem precisa do IP chama `waitForIp()`.

## O que a rota default *não* precisou

Nenhuma chamada. `PPP_DEF` tem `route_prio` 20 contra 10 do `WIFI_AP_DEF`
(`esp_netif_defaults.h`), e o `esp_netif_update_default_netif()` promove o PPP a interface
default sozinho quando o `GOT_IP` chega. `esp_netif_set_default_netif()` é `static` em
`esp_netif_lwip.c:202` — nem daria para chamar de fora.

## Validação em hardware

Boot completo em ~17 s: AP em 192.168.4.1 → PAP `ESP_OK` → registro LTE (`CEREG=1`) em 3 s,
RSSI 31 → PPP com IP e DNS da operadora (187.50.250.115 / .215) → NAT ativo. Celular
conectado no AP navegou normalmente.

## Em aberto

Cliente que associa ao AP **antes** do PPP subir recebe DNS `192.168.4.1` no lease e só
resolve nome depois de renovar. No boot normal não acontece (o AP ainda não tem cliente),
mas numa reconexão de PPP em campo, sim. Tratado como débito 9, território da Fase 6.
