# PRD 13 — Acesso remoto à página de configuração

**Status: proposto** — não implementado.

Fonte: conversa de 21/09/2026, depois de a Fase 8 (OTA pela página) ser validada em campo e
o débito 24 registrar que "remoto" ainda significa presencial.

## Objetivo

Permitir que o operador abra a página de configuração de uma unidade instalada **sem estar
no alcance do Wi-Fi dela**, e por ali suba um `firmware.bin` como já faz hoje.

O alvo não é um canal só de firmware. A página já faz status, configuração e atualização;
alcançá-la de fora resolve as três, e o procedimento de campo documentado em
[ATUALIZACAO_EM_PRODUCAO.md](../ATUALIZACAO_EM_PRODUCAO.md) continua valendo — muda só de
onde o navegador é aberto.

## O problema

O `WebServer` escuta em `0.0.0.0` e em tese atenderia pelo PPP, mas dados móveis saem por
CGNAT: a unidade tem endereço privado do lado da operadora e não há porta alcançável da
internet. Nenhuma configuração do firmware muda isso — o bloqueio está na rede da operadora.

Consequência atual: toda intervenção custa uma visita. É esse número que decide se vale
corrigir um defeito pequeno, e com centenas de unidades ele decide o roadmap inteiro.

## A decisão

**Túnel WireGuard partindo da placa, contra um servidor nosso.** A conexão nasce na unidade,
o que contorna o CGNAT sem depender de nada da operadora. O operador entra na mesma rede
privada pelo celular e abre o endereço interno da unidade.

WireGuard e não um túnel TCP caseiro porque o caseiro obriga a reimplementar cifra e
autenticação — e a alternativa de expor a porta com TLS não existe sob CGNAT.

### Por que não o canal só de firmware

Considerada: a placa consultando periodicamente um servidor por uma imagem assinada, e
gravando sozinha. É mais barata, dispensa conexão permanente e é mais segura por ter
superfície menor. Foi recusada porque cobre um caso só: continuaria exigindo visita para ler
status ou corrigir um APN errado, que são os chamados mais comuns. Fica registrada porque,
com centenas de unidades, ela volta a fazer sentido **junto** com o túnel — ver "Escala".

## As camadas de segurança

Nenhuma delas substitui as outras. Em ordem, de fora para dentro:

1. **A placa não aceita conexão de entrada.** Quem inicia é ela. Não há porta aberta na
   internet, e o WireGuard não responde a pacote que não passe na autenticação — a unidade
   fica invisível para varredura, sem sequer devolver "recusado"
2. **Cifra e autenticação mútua no transporte.** ChaCha20-Poly1305 com troca Curve25519.
   Cada unidade tem seu par de chaves; o servidor só aceita chave pública que conhece
3. **A chave privada nasce e morre na placa.** Gerada no primeiro boot pelo
   `infra/entropy`, o mesmo caminho que hoje sorteia a senha de admin. Nunca trafega, nem no
   provisionamento: o que sai é só a pública
4. **Basic Auth continua.** Hoje ele manda usuário e senha em base64, que é reversível —
   dentro do AP já é discutível, na internet seria senha em claro. Dentro do túnel volta a
   ser aceitável, porque o que trafega já está cifrado. **Não é substituto do túnel: é a
   segunda barreira para quem já está na VPN**
5. **A imagem tem que estar assinada.** `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` com
   esquema ECDSA, e `CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT` (default `y`): toda
   gravação por `esp_ota_*` — que é o que o `Update` do Arduino usa por baixo — verifica a
   assinatura antes de aceitar a imagem. Firmware não assinado é recusado mesmo por quem
   tiver a senha de admin
6. **Segregação no servidor.** O operador alcança a porta 80 das unidades e nada mais; uma
   unidade não alcança outra. Isso é `AllowedIPs` por peer mais regra de firewall, e é o que
   impede uma unidade comprometida de virar ponte para as demais
7. **Registro de quem gravou o quê.** Sem isso, "a unidade 47 está com firmware errado" não
   tem como ser investigado

### Sobre a assinatura: ECDSA e não RSA

O ESP32 aceita dois esquemas. O RSA-3072 (Secure Boot V2) exige `CONFIG_ESP32_REV_MIN` em 3,
e o `sdkconfig` deste projeto está em `CONFIG_ESP32_REV_MIN_FULL=0`. Subir para 3 faz o
binário recusar chip de revisão anterior — a placa de bancada é v3.1 e passaria, mas o
firmware ficaria amarrado a ECO3 sem ganho correspondente.

O ECDSA funciona em qualquer revisão e ainda habilita
`CONFIG_SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT`, que faz o bootloader conferir a assinatura
também no boot, e não só na gravação.

**O que esta opção NÃO protege:** quem tiver a placa na mão. Sem eFuse queimado, o
bootloader pode ser regravado por serial e a verificação desaparece junto. A documentação do
IDF diz isso com todas as letras — *"the device can be secured against remote network
access, but not physical access"*. Foi a escolha consciente: proteger o vetor remoto, que é
o que esta fase abre, sem tornar irreversível um dispositivo que ainda está em evolução.

## Pré-requisito: o servidor

Não existe ainda. A fase não começa sem ele, e ele precisa de:

- **IP público fixo e porta UDP** para o WireGuard. UDP, não TCP — túnel sobre TCP degrada
  feio quando a camada de baixo já retransmite
- **Banda de subida** proporcional ao número de unidades atualizando ao mesmo tempo: cada
  `firmware.bin` tem cerca de 1 MB e desce do servidor até o celular do operador, e sobe
  dele até a placa. O gargalo real é o enlace móvel da unidade, não o servidor
- **Backup das chaves públicas e do mapa de endereços.** Perder isso significa reprovisionar
  todas as unidades presencialmente, que é exatamente o custo que a fase existe para eliminar
- **A chave privada de assinatura fora do servidor.** Se ela morar na mesma máquina que
  recebe os túneis, comprometer o servidor passa a valer firmware assinado em toda a frota

Dimensionamento e provedor ficam para a implementação; o que este PRD fixa é que sem esses
quatro itens a fase não fecha.

## Escala: centenas de unidades

É o número que muda o desenho, e em dois pontos:

**Provisionamento não pode ser manual.** Gerar e cadastrar chave a chave não sobrevive à
terceira dezena. A placa gera o próprio par no primeiro boot; falta o caminho por onde a
pública chega ao servidor de forma autenticada — senão qualquer um cadastra uma unidade. As
saídas plausíveis são um segredo de lote gravado junto com o firmware na produção, ou
registro durante a gravação por serial, quando a placa ainda está na bancada. A segunda é
mais simples e não deixa segredo compartilhado no binário.

**Atualizar pela página não escala.** O túnel resolve o acesso, mas continua sendo um
operador abrindo uma página por vez. Trezentas unidades a poucos minutos cada são dias de
trabalho. É aqui que o canal de pull assinado volta: com o túnel de pé para diagnóstico e
configuração, a atualização em massa pode virar a placa buscando a imagem sozinha. **Fora de
escopo desta fase**, mas o desenho não deve impedir — em particular, a mesma chave de
assinatura serve aos dois caminhos.

## Escopo

- `infra/` — cliente WireGuard sobre lwIP, subindo depois do PPP e reconectando junto com ele
- `domain/` — identidade da unidade e estado do túnel, como regra pura e testável
- `adapters/http_config_handler` — nada muda. É o ponto: a página é a mesma
- `sdkconfig.defaults` — assinatura de imagem
- Ferramenta de provisionamento, rodando na gravação por serial
- Documentação do servidor e do procedimento de registro de uma unidade nova

## Fora de escopo

- Atualização em massa automatizada (ver "Escala")
- Secure Boot com eFuse
- Substituir o Basic Auth por sessão ou certificado de cliente. Dentro do túnel ele serve;
  trocar isso é fase própria
- Painel de inventário. O servidor vai saber quem conectou, mas transformar isso em tela é
  outro trabalho

## Riscos

- **WireGuard e NAT no mesmo lwIP.** A unidade já roteia os clientes do AP para o PPP. Uma
  terceira interface entra num arranjo de roteamento que hoje funciona e não tem teste
  automatizado. É o risco técnico número um desta fase
- **MTU.** O WireGuard soma ~60 bytes de cabeçalho ao pacote. Com o PPP já perto do limite,
  o resultado é fragmentação ou pacote descartado em silêncio — e o débito 13 mostra que
  descarte no caminho do PPP já é difícil de enxergar
- **Heap.** A placa roda PPP, NAT, DNS e servidor HTTP. O handshake do WireGuard e as filas
  do túnel disputam o mesmo heap, e a página já custa 34 KB de `.rodata`
- **Dado móvel é pago.** Um túnel permanente gasta keepalive o tempo todo, em toda unidade,
  para um acesso que acontece raramente. Talvez o túnel precise subir sob demanda — e aí
  falta o canal para pedir que ele suba, que é um problema circular a resolver no desenho
- **A superfície de ataque cresce de verdade.** Hoje a página só é alcançável por quem está
  fisicamente perto. Depois desta fase, um comprometimento do servidor alcança toda a frota.
  É o preço da fase, e a razão de a chave de assinatura ter que morar em outro lugar

## Critérios de aceite

1. Unidade em campo, com o túnel de pé, responde à página a partir de outro município
2. A placa continua roteando os clientes do AP normalmente com o túnel ativo — sem perda de
   pacote mensurável e sem queda de MTU que quebre navegação
3. Túnel cai e volta sozinho quando o PPP cai e volta
4. Imagem não assinada é recusada na gravação, com mensagem clara na página
5. Imagem assinada grava, confirma e sobrevive ao rollback como hoje
6. Chave privada da unidade não aparece em nenhum log, nem no serial, nem na página
7. Uma unidade não alcança outra pela rede privada
8. Provisionamento de uma unidade nova é um procedimento escrito, sem edição manual de
   arquivo de configuração no servidor
9. Consumo de dado do túnel em repouso medido por 24 h e registrado aqui
10. Testes nativos para a parte de domínio da identidade e do estado do túnel

## Validação em hardware

Pendente — fase não iniciada.
