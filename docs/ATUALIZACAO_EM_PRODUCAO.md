# Atualizar firmware de unidades em produção

Procedimento para trocar o firmware de uma unidade já instalada, usando a página de
configuração em vez do cabo serial.

Para gerar os arquivos que exercitam as recusas do upload, ver
[TESTE_OTA.md](TESTE_OTA.md) — aquele documento é de bancada, este é de campo.

## O alcance real de "à distância"

**O operador precisa estar dentro do Wi-Fi da unidade.** A página só responde no AP: o
`WebServer` escuta em `0.0.0.0` e em tese atenderia também pelo PPP, mas dados móveis saem
por CGNAT e não há porta alcançável da internet.

O que este procedimento elimina é o cabo e o notebook, não a viagem. Atualizar de outro
município depende de alcançar esta mesma página de fora, o que exige um túnel partindo da
placa — não existe hoje, e está registrado como débito 24. Quando existir, este procedimento
vale igual: muda só de onde o operador abre o navegador.

Se alguma unidade receber IP público de operadora, a conta muda e piora: a página de
configuração passaria a estar exposta na internet, protegida só por Basic Auth. Confirmar
antes de contratar qualquer plano diferente do atual.

## Antes de gerar o binário

Quatro perguntas que decidem se o OTA serve. Qualquer "sim" nas duas primeiras exige cabo
serial, e nenhum OTA resolve.

1. **`partitions.csv` mudou?** A tabela de partições não viaja no `firmware.bin`. Uma
   imagem que assume um mapa diferente do gravado não sobe, e a unidade fica no firmware
   anterior — no melhor caso.
2. **`kCurrentSchema` da NVS subiu sem um degrau novo em `domain/settings_migration`?**
   O OTA não toca a NVS, então SSID, senhas e calibração de cada unidade sobrevivem — é
   justamente por isso que um schema sem degrau é grave: `load()` falha, o
   `ProvisionSettingsUseCase` sorteia senha nova e **todos os clientes daquela unidade
   caem**, sem ninguém por perto para ler a senha no serial.
3. **O binário cabe?** O slot é de 1.900.544 bytes (`0x1D0000` em `partitions.csv`). O
   `pio run` imprime a ocupação no fim; se passar de 100%, o upload é recusado pela placa,
   mas descobrir isso em campo é tarde.
4. **A faixa do AP mudou?** Ela é fixa em `infra/wifi_ap` (`kApIp`), e trocá-la derruba o
   endereçamento de **todos** os clientes daquela unidade de uma vez. Não impede o OTA nem
   exige cabo, mas muda o que o operador vê depois do reboot — ver a seção seguinte.

### Se esta imagem troca a faixa do AP

Aconteceu uma vez, em 23/09/2026: `192.168.4.0/24` → `192.168.10.0/24`. Se voltar a
acontecer, o operador precisa saber **antes** de subir a imagem.

O lease que cada cliente tem é da faixa antiga, e o servidor DHCP da placa passa a responder
na nova. O aparelho continua associado ao Wi-Fi e para de navegar até renovar o lease — o
que, dependendo do sistema, leva minutos ou só acontece quando alguém desliga e religa o
Wi-Fi. O sintoma é **"conectou e não tem internet"**, que é exatamente como uma queda de
uplink 4G se parece.

Consequências práticas para a visita:

- O passo 1 de "Na unidade" passa a valer no endereço novo. Abrir o antigo não dá erro
  claro: pode não responder, ou pior, responder de outro aparelho da rede onde você estiver
- O passo 7 (testar a unidade de verdade antes de confirmar) exige **desassociar e associar
  de novo** o celular, senão o teste falha pelo lease velho e não pelo firmware. Confirmar
  ou reverter por causa disso seria decidir pelo motivo errado
- Quem ficar sem confirmar dentro do prazo por estar brigando com o lease perde a imagem: a
  placa reverte sozinha. Renove o lease primeiro, teste depois, confirme por último

## Gerar o binário de release

```bash
git status --porcelain          # tem que sair VAZIO
git describe --tags --always    # anote: é o que vai identificar esta imagem
pio run -e esp-wrover-kit
```

O `git status` vazio não é formalidade. A versão que o firmware reporta na página sai do
`git describe`, e com a árvore suja ela vira algo como `88d4901-dirty` — que não identifica
código nenhum, porque "dirty" não diz *o que* estava modificado. Uma unidade em campo
rodando uma imagem `dirty` é uma unidade cujo código ninguém consegue reproduzir.

Hoje nada impede um build sujo de sair; a disciplina é manual. Está registrado como
débito 25.

O arquivo fica em `.pio/build/esp-wrover-kit/firmware.bin`. Copie para fora do `.pio` com o
nome da versão — `firmware-<versão>.bin` — porque o próximo `pio run` sobrescreve o
original sem avisar.

Leve o arquivo para o celular **antes** de ir a campo: o seletor do navegador lê do próprio
aparelho, e no AP da unidade não há internet para baixar nada — o uplink pode estar
justamente caído.

## Na unidade

1. Associe ao AP e abra `http://192.168.10.1/`
2. Anote o que está lá **antes**: aba Firmware, slot e versão. É o que permite dizer depois
   se a atualização pegou
3. Aba **Firmware** → escolha o arquivo → **Enviar e reiniciar**. Cerca de um minuto
4. A mensagem manda confirmar em até 10 minutos, e não some sozinha
5. A placa reinicia e o Wi-Fi cai. Reconecte e reabra a página
6. Confira: o slot trocou e o estado é `Em verificação`. No `<nav>`, a aba Firmware carrega
   um ponto
7. **Antes de confirmar, teste a unidade de verdade**: abra um site pelo 4G a partir do
   celular associado. É a única coisa que o rollback consegue proteger, e confirmar sem
   testar joga essa proteção fora
8. Se estiver tudo bem: **Confirmar atualização**. O estado vira `Confirmado`

### A janela de 10 minutos

Sem o clique, a placa reinicia sozinha e volta ao firmware anterior. Isso é a rede de
proteção funcionando, não um defeito — mas em campo significa que **ninguém pode sair do
alcance do Wi-Fi antes de confirmar**. Uma unidade deixada em `Em verificação` volta atrás
por conta própria, e quem fez a visita só descobre na visita seguinte.

O prazo vem de `kConfirmationDeadlineMs` em `domain/firmware_update`. A página não o escreve
à mão: ele viaja no JSON e é o mesmo número que o `loop()` usa para decidir.

### Se a placa reverteu

Nada a recuperar: ela está no firmware anterior, com a configuração intacta. A causa é uma
das três, e a aba Firmware distingue as duas primeiras:

- Ninguém confirmou dentro do prazo → repetir, confirmando desta vez
- A imagem nova travou ou entrou em pânico antes do `loop()` girar → o firmware é que está
  errado, não o procedimento
- A imagem nem chegou a ser gravada → a mensagem de erro do envio diz qual regra recusou

### Se o envio falhar no meio

`O envio foi interrompido antes do fim. Nada foi gravado.` significa o que diz: o slot ficou
com lixo, mas não bootável, e a unidade segue no firmware que estava. Basta reenviar.

Não confundir com `Envio concluído, mas a placa não respondeu…` — nesse caso o arquivo
inteiro subiu e a placa provavelmente reiniciou já com a imagem nova. Reconecte e olhe o
slot antes de reenviar qualquer coisa.

## Uma unidade por vez

Atualize, confirme e teste uma antes de ir para a próxima. Um firmware que passa na bancada
e falha em campo — por causa de uma operadora, de um SIM, de uma antena — falha em todas, e
descobrir isso na primeira custa uma visita em vez de dez.

## Rastreabilidade

Não existe inventário central: a única fonte do que roda em cada unidade é a própria página.
Enquanto for assim, anote por unidade a versão anterior, a nova e a data. Registrado como
parte do débito 24.
