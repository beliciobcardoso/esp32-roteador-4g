# Setup do ambiente — ESP32 Roteador 4G (LilyGO T-A7670E R2)

Guia para configurar o ambiente de desenvolvimento do zero em um computador novo (Ubuntu/Debian).

Se você **já tem o ambiente montado** e só quer retomar o trabalho em outra máquina, ou está
**assumindo o projeto de outra pessoa**, vá direto para as seções 11 e 12.

## 1. Instalar dependências do sistema

```bash
sudo apt install python3-pip -y
sudo apt install python3.12-venv -y
```

> Se sua distro usa outra versão do Python (ex: 3.10, 3.11), troque `python3.12-venv` pelo pacote correspondente (`python3.10-venv`, etc). Confira com `python3 --version`.

## 2. Instalar o PlatformIO Core

```bash
pip install platformio --break-system-packages
```

Confirma a instalação:

```bash
pio --version
```

## 3. Permissão de porta serial

O seu usuário precisa estar no grupo `dialout` para acessar a porta serial sem `sudo`.

```bash
sudo usermod -aG dialout $USER
```

**Importante:** essa mudança só entra em vigor após um **logout/login completo** ou **reboot** — reabrir só o terminal não é suficiente. Confirme depois de logar de novo:

```bash
id
```

Deve aparecer `dialout` na lista de grupos.

## 4. Clonar o projeto

```bash
git clone https://github.com/beliciobcardoso/esp32-roteador-4g.git
cd esp32-roteador-4g
git checkout developer
```

**O `checkout developer` não é opcional.** O clone cai em `main`, que não acompanha o
desenvolvimento. `developer` é a branch de integração — é dela que sai toda branch de
feature e é nela que o trabalho validado é mergeado.

## 5. O que vem do git e o que é gerado localmente

Nada precisa ser criado à mão. Estes arquivos são versionados e já vêm no clone:

| Arquivo | Para que serve |
|---|---|
| `platformio.ini` | Board, framework, portas serial, tabela de partições, C++17 |
| `sdkconfig.defaults` | Flags de Kconfig (NAT, PPP, PAP, LCP echo, flash 4MB, teto de clientes) |
| `partitions.csv` | Tabela própria: dois slots de app de 1.875 MB para OTA |
| `src/idf_component.yml` + `dependencies.lock` | Dependência `espressif/esp_modem` e a versão exata resolvida |
| `include/config.h` | Pinos da placa (`BOARD_POWERON_PIN` etc.) |

Cada flag do `sdkconfig.defaults` tem um comentário no próprio arquivo explicando por que
existe. **Leia lá, não em cópia neste guia** — versões anteriores deste documento
duplicavam o conteúdo dos dois arquivos e a cópia envelheceu enquanto os originais
evoluíam. Uma flag errada aqui não dá erro de compilação: dá comportamento errado em
campo, que é muito mais caro de achar.

Estes **não** vêm no clone e são recriados sozinhos pelo primeiro `pio run`:

- `.pio/` — toolchain, ESP-IDF e objetos de build
- `sdkconfig.esp-wrover-kit` — gerado a partir do `sdkconfig.defaults`
- `managed_components/` — baixado pelo Component Manager, na versão travada em `dependencies.lock`

## 6. Primeira compilação

```bash
pio run
```

A primeira execução baixa toolchain, ESP-IDF e todas as dependências — demora vários minutos.

Se você **mudar o `sdkconfig.defaults`** e a mudança não refletir, o motivo quase sempre é
o mesmo: o PlatformIO não reaplica os defaults enquanto o `sdkconfig.<env>` existir, e
limpar só o `.pio/build` não basta.

```bash
rm -f sdkconfig.esp-wrover-kit
rm -rf .pio
pio run
```

## 7. Upload para a placa

Conecte a placa via USB-C e rode:

```bash
pio run --target upload
```

**Erros comuns:**

- `Permission denied` na porta serial → grupo `dialout` não aplicado ainda nesta sessão. Confirme com `id`; se faltando, refaça o passo 3 e faça logout/login.
- `[Errno 11] Could not exclusively lock port` ou `Device or resource busy` → outro processo tem a porta aberta, quase sempre o monitor serial em outra aba. Identifique com `fuser -v /dev/ttyACM*` e feche o monitor antes do upload.
- Upload "funciona" mas o firmware não muda → conferir se a placa realmente resetou. O `platformio.ini` aponta para um caminho `/dev/serial/by-id/...`; se a placa não estiver conectada, o upload falha de forma clara em vez de gravar na porta errada.

## 8. Monitor serial

Pelo terminal:

```bash
pio device monitor
```

Ou, no VSCode com a extensão **PlatformIO IDE** instalada: ícone da "formiga" na barra lateral → **PROJECT TASKS → esp-wrover-kit → General → Monitor**. Certifique-se de ter aberto a **pasta do projeto inteira** (`File → Open Folder`) e não um arquivo avulso, senão o painel do PlatformIO não reconhece o projeto.

> Só pode haver um processo por vez usando a porta serial. Feche o monitor antes de tentar um novo upload.

## 9. VSCode — extensão necessária

Instale a extensão **PlatformIO IDE** (não apenas a extensão C/C++ da Microsoft) pela aba de Extensions (`Ctrl+Shift+X`, busca "PlatformIO IDE"). Isso resolve também os erros de IntelliSense do tipo `não é possível abrir o arquivo fonte "Arduino.h"` — a extensão gera o `includePath` correto automaticamente.

Se o erro do IntelliSense persistir mesmo com a extensão instalada:

```bash
pio project init --ide vscode
```

Depois recarregue a janela (`Ctrl+Shift+P` → "Developer: Reload Window").

## 10. Notas de hardware

- **Placa real:** LilyGO T-A7670E R2 — ESP32-WROVER-E, modem A7670E (4G LTE Cat1 / GSM/GPRS/EDGE), bateria 18650 via JST, carregamento por USB-C.
- **Board PlatformIO usado:** `esp-wrover-kit` — o mais próximo do catálogo, não é um board dedicado a esta placa. Os ajustes reais (4 MB de flash, tabela de partições própria) estão no `sdkconfig.defaults` e no `partitions.csv`; o build reporta `4MB Flash` corretamente.
- **`BOARD_POWERON_PIN` (GPIO12):** precisa ir para `HIGH` no `setup()`. Sem isso a placa desliga sozinha rodando só na bateria.
- **Pulso de PWRKEY do A7670E:** 1000 ms. Com 100 ms o handshake AT demora ou falha.
- **Porta serial:** o `platformio.ini` usa o caminho `by-id` em vez de `/dev/ttyACMx`, porque o número do ACM muda conforme a ordem de enumeração USB. O `by-id` contém o serial do chip USB **da placa**, então continua válido em qualquer computador — não edite ao trocar de máquina.

## 11. Continuidade — trocar de computador

Tudo que é código, configuração e decisão está no git. O que **não** está:

**A placa.** Leve o hardware: placa, SIM com dados ativos, cabo USB-C e a bateria 18650.
Sem ela não dá para validar nada — a verificação deste projeto é leitura de log serial com
o modem real conectado à operadora.

**A configuração gravada.** SSID, senha do AP, APN e credenciais de admin vivem na NVS da
**placa**, não no repositório. Elas viajam junto com o hardware. O que está no código são
apenas os defaults de fábrica, aplicados quando a NVS está vazia ou com schema antigo. Ver
[CONFIGURACAO_NVS.md](CONFIGURACAO_NVS.md).

**O ambiente local.** Refaça as seções 1 a 3 na máquina nova. O resto (`.pio/`,
`sdkconfig.<env>`, `managed_components/`) se regenera no primeiro `pio run`.

Antes de sair da máquina antiga:

```bash
git status            # working tree tem que estar limpo
git push origin developer
```

Na máquina nova, a partir da seção 4. Se o repositório já estiver clonado lá:

```bash
git checkout developer && git pull
```

> Não confie em cópia de pasta por pendrive ou rede. O `.pio/` tem caminhos absolutos da
> máquina de origem e quebra em silêncio. Clone do git.

## 12. Continuidade — passar o projeto para outra pessoa

Quem assume o projeto não precisa de contexto verbal. A ordem de leitura abaixo entrega o
estado inteiro, e cada documento tem uma função distinta:

1. **[AGENTS.md](../AGENTS.md)** — comece aqui. É a fonte única de verdade: stack, comandos,
   arquitetura, estado atual das fases, decisões já fechadas (e por quê), cuidados
   obrigatórios de hardware e as regras de workflow.
2. **[PLANO_ROTEADOR.md](PLANO_ROTEADOR.md)** — o plano por fases, com o que foi validado em
   campo em cada uma e as ressalvas de critérios que não se sustentaram.
3. **[prd/](prd/)** — um PRD por fase, com as decisões de implementação e as alternativas
   descartadas. O PRD da fase corrente diz onde o trabalho parou. O PRD 06 traz uma
   retrospectiva com os acertos e os erros da fase.
4. **[DEBITOS_TECNICOS.md](DEBITOS_TECNICOS.md)** — o que está consciente e deliberadamente
   pendente. Ler antes de "consertar" algo que parece esquecido: pode ser uma escolha
   registrada, com o motivo escrito.
5. **[DEPURACAO_FASE_4_MODEM_PPP.md](DEPURACAO_FASE_4_MODEM_PPP.md)** — armadilhas do
   `esp_modem` e diagnósticos já descartados. Consultar **antes** de mexer no modem ou em
   flags de Kconfig, para não repetir investigação já feita.
6. **[CONFIGURACAO_NVS.md](CONFIGURACAO_NVS.md)** — chaves persistidas, defaults de fábrica,
   como consultar e como apagar.

### Regras de workflow que valem para quem continuar

Estão em [AGENTS.md](../AGENTS.md) e são obrigatórias:

- **Ao iniciar** uma feature de um PRD: criar branch a partir de `developer`, nunca direto em `main`.
- **Ao finalizar** uma feature: aguardar validação do responsável antes de qualquer `git commit` ou merge para `developer`.
- **Proibido criar PRs sem autorização explícita.**

### Checklist de quem assume

- [ ] Ambiente montado (seções 1 a 3) e `pio run` passa
- [ ] `git checkout developer` — não trabalhar a partir de `main`
- [ ] Placa, SIM ativo, cabo e bateria em mãos
- [ ] `pio run --target upload` grava e `pio device monitor` mostra o boot
- [ ] AGENTS.md e o PRD da fase corrente lidos
- [ ] DEBITOS_TECNICOS.md lido, para não retrabalhar decisão já tomada
