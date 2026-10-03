# ADR-0001: Integração do túnel WireGuard na placa (Fase 10)

**Date**: 2026-09-30
**Status**: proposed
**Deciders**: beliciobcardoso, assistente de engenharia

## Context

A placa ESP32 roteador 4G opera atrás de CGNAT da operadora: não há porta alcançável da
internet, e toda intervenção — status, configuração, atualização de firmware — exige
presença física no Wi-Fi do AP. Com centenas de unidades, o custo de visita decide o roadmap
inteiro (débito 24, PRD 13).

O servidor já está 100% em produção no repositório `telemetria-mqtt` (PRs #21–#29, deploy
Coolify final confirmado): container WireGuard em `168.138.238.120:51820`, daemon interno
com proxy reverso, backoffice NestJS+Next.js com cadastro/revogação de chaves, dashboard e
alertas no Grafana. **O que falta é a integração no firmware da placa** — este ADR
documenta as decisões de arquitetura para essa integração.

O spike `infra/wg_spike` (branch `tmp/spike-wireguard`, nunca mergear) já rodou em placa e
revelou dois problemas concretos:

1. **Deadlock no `esp_wireguard_init` + `CORE_LOCKING`.** O `init` faz `netif_add` /
   `udp_new` / `udp_bind` (API crua do lwIP, precisa do lock da tcpip), enquanto o
   `connect` faz `getaddrinfo` (API de socket que posta na thread tcpip e espera). Com o
   lock seguro, o `connect` bloqueia para sempre e o `LoopWatchdog` reinicia a placa.
2. **O handshake do WireGuard exige relógio válido.** O Noise_IK do protocolo carrega um
   timestamp TAI64N no initiator, e o peer rejeita se o relógio estiver muito fora. Sem
   SNTP, o relógio é zero e o handshake nunca completa.

Forças em jogo:
- O lwIP já faz NAT dos clientes do AP para o PPP. Uma terceira interface (tun WireGuard)
  entra num arranjo de roteamento que funciona e não tem teste automatizado.
- A PSRAM de 8 MB não está compilada; todo o heap é DRAM interna (~170 KB livres). O
  handshake do WireGuard (ChaCha20-Poly1305 + Curve25519) soma ao mbedTLS do MQTT.
- O `persistent_keepalive` de 25 s gera tráfego constante no enlace 4G — dados móveis são
  pagos. O custo precisa ser medido por 24 h antes de aceitar.
- O `esp_wireguard` (`trombik/esp_wireguard`) é componente de terceiro, já presente em
  `managed_components/`, mas o `init`/`connect` acoplados o tornam incompatível com
  `CORE_LOCKING` em produção — precisa de fork.

## Decision

Integrar o cliente WireGuard como `infra/wireguard_tunnel`, seguindo a mesma Clean
Architecture das Fases 4–9, com cinco decisões técnicas:

1. **Fork local do `esp_wireguard`:** separar `init` (que opera sob o lock tcpip via
   `esp_netif_tcpip_exec`) de `connect` (que faz `getaddrinfo` fora do lock). Em produção,
   **não é aceitável a corrida** que o spike tolerou. O fork fica em `components/` e
   substitui o `managed_components/trombik__esp_wireguard`.

2. **Subida pós-SNTP, disparada pelo `LinkSupervisor`.** O túnel sobe apenas quando o PPP
   está ativo **e** o relógio já sincronizou via SNTP — o mesmo callback `onUplinkOnline`
   que hoje dispara o `systemClock.onUplinkOnline()`. Reconexão do PPP reconecta o túnel.

3. **Rota padrão fica no modem PPP.** `esp_wireguard_set_default()` **não é chamado**.
   O tráfego dos clientes do AP continua saindo pelo PPP/NAT; só o tráfego
   originado para a sub-rede `10.8.0.0/22` (operador via proxy) entra no túnel. Na direção
   servidor→placa, o proxy do backoffice alcança a porta 80 via `10.8.x.y` e a placa
   responde porque o túnel é a rota para essa sub-rede.

4. **Chave privada gerada na placa, salva na NVS.** O mesmo `infra/entropy` do
   provisionamento (Fase 2/8) gera o par Curve25519 no primeiro boot. A chave privada
   **nunca sai da placa** — o que vai para o backoffice é a chave pública, cadastrada
   manualmente na interface `registre-unid.belloinfo.com.br`.

5. **Configuração (endpoint, IP, chave do servidor) na NVS e editável pela página.**
   `RouterSettings` ganha campos `wg_endpoint`, `wg_address`, `wg_server_pubkey`,
   `wg_enabled`. O túnel pode ser desligado pela página sem reflash.

## Alternatives Considered

### Alternative 1: Túnel TCP caseiro (reverse proxy sobre TLS)
- **Pros**: sem dependência de biblioteca cripto extra; sem overhead de keepalive UDP
- **Cons**: reimplementa cifra e autenticação; TCP sobre TCP degrada feio com perda de
  pacote no enlace 4G; não resolve a raiz (CGNAT)
- **Why not**: complexidade de segurança inaceitável para firmware de campo, e o desempenho
  de TCP-over-TCP em enlace celular é comprovadamente ruim

### Alternative 2: Canal pull de firmware (placa busca imagem em servidor)
- **Pros**: dispensa conexão permanente; superfície menor; sem keepalive
- **Cons**: só cobre atualização, não status/configuração; exige URL, TLS e política de
  versão; cada intervenção de config continua exigindo visita
- **Why not**: resolve um caso de uso e deixa os outros dois (status e config) iguais ao
  estado atual. Registrada porque volta a fazer sentido **junto** com o túnel, em escala
  (PRD 13, "Escala")

### Alternative 3: Usar `esp_wireguard` sem fork, separando init/connect por timing
- **Pros**: nenhuma manutenção de fork
- **Cons**: corrida real entre o `connect` (fora do lock) e as chamadas cruas do lwIP que
  ele faz por dentro; o spike mediu o deadlock em 29/09/2026
- **Why not**: o problema não é de timing, é estrutural — o `getaddrinfo` dentro do connect
  posta na thread tcpip enquanto o lock tcpip está seguro. Fork é a única saída que não
  viola a garantia do `CORE_LOCKING`

### Alternative 4: Desligar `CORE_LOCKING` para acomodar a biblioteca
- **Pros**: `esp_wireguard` funciona sem alteração
- **Cons**: volta o descarte PPP (`pppos_input_tcpip failed with -1`) que o `CORE_LOCKING`
  resolveu na Fase 6 (débito 13, 3166 descartes medidos); todo o trabalho de estabilidade
  do enlace é desfeito
- **Why not**: trocar um problema resolvido por outro é regressão, não integração

## Consequences

### Positive
- Operador acessa status, configuração e OTA de qualquer município, sem visita
- O procedimento de campo documentado em `ATUALIZACAO_EM_PRODUCAO.md` continua exatamente
  igual — muda só de onde o navegador é aberto
- Custo de intervenção cai de uma viagem para um clique no backoffice ("Acessar Placa")
- Chave privada nunca sai da placa: comprometer o servidor não revela chaves das unidades
- Reconexão automática do túnel herdada do `LinkSupervisor` (mesma lógica de backoff)
- Observabilidade já pronta: dashboard e alertas no Grafana (`wireguard-down`,
  `wg-tunnel-stalled`) provisionados no servidor

### Negative
- **Terceira interface no lwIP.** O NAT dos clientes do AP e o DNS local já roteiam pelo
  PPP; o túnel adiciona uma rota e uma interface. Revalidar reconexão, queda de RF e perda
  de SIM é obrigatório — o risco técnico número um da fase (PRD 13)
- **Heap interno disputado.** ChaCha20-Poly1305 + Curve25519 no handshake disputam com o
  mbedTLS do MQTT. Livre atual ~170 KB, mínimo ~150 KB pós-OTA. Margem apertada
- **Fork de biblioteca.** `trombik/esp_wireguard` recebe atualizações upstream; o fork
  desacopla. Manter atualizado é custo contínuo, mitigado pela interface mínima (4 funções)
- **Keepalive permanente gasta dados móveis.** 25 s × 128 B ≈ ~440 KB/dia ≈ ~13 MB/mês só
  de keepalive, somados ao MQTT (~35–50 MB/mês). Aceitável com os planos atuais; medir em
  produção (critério 9 do PRD 13)
- **MTU.** WireGuard soma ~60 bytes de cabeçalho. Com o PPP perto do limite, fragmentação
  ou descarte silencioso é possível. MSS clamping (MTU 1420) já configurado no servidor

### Risks
- **Deadlock em produção se o fork não separar init/connect corretamente.** Mitigação:
  teste em placa com `CONFIG_LWIP_CHECK_THREAD_SAFETY=y` (já validado no spike para o lado
  do init)
- **Heap esgotado com MQTT TLS + WG handshake simultâneos.** Mitigação: medir pico de heap
  em bancada antes de promover; considerar habilitar PSRAM se a margem ficar abaixo de 30 KB
- **Placa inalcançável por bug no túnel.** Mitigação: o AP local e a página em
  `192.168.10.1` continuam funcionando independentemente do túnel — o acesso presencial
  nunca é bloqueado pelo WireGuard
- **Servidor comprometido → acesso a toda a frota.** Mitigação: assinatura de imagem
  (ECDSA, PRD 13); chave de assinatura fora do servidor; isolamento por `AllowedIPs` +
  firewall (placa não alcança outra)
