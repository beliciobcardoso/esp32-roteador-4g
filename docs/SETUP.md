# Setup do ambiente — ESP32 Roteador 4G (LilyGO T-A7670E R2)

Guia para configurar o ambiente de desenvolvimento do zero em um computador novo (Ubuntu/Debian).

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

O seu usuário precisa estar no grupo `dialout` para acessar `/dev/ttyACM0` (ou `/dev/ttyUSB0`) sem `sudo`.

```bash
sudo usermod -aG dialout $USER
```

**Importante:** essa mudança só entra em vigor após um **logout/login completo** ou **reboot** — reabrir só o terminal não é suficiente. Confirme depois de logar de novo:

```bash
id
```

Deve aparecer `dialout` na lista de grupos.

## 4. Clonar/copiar o projeto

Copie a pasta do projeto **inteira** para o novo computador — isso inclui obrigatoriamente os três itens abaixo, não só os arquivos de configuração:

- `platformio.ini`
- `sdkconfig.defaults`
- pasta `src/` com o `main.cpp`

> Erro comum: copiar só `platformio.ini` e `sdkconfig.defaults` e esquecer a pasta `src/`. O build falha com `Error: Missing the 'src' folder with project sources.` Confirme com `ls` antes de rodar `pio run`.

```bash
cd ~/Projetos/esp32-roteador-4g
ls
```

## 5. Arquivos de configuração do projeto

Estes dois arquivos já devem estar na pasta do projeto. Se precisar recriar do zero:

**`platformio.ini`:**
```ini
[env:esp-wrover-kit]
platform = espressif32
board = esp-wrover-kit
framework = espidf, arduino

monitor_speed = 115200
upload_port = /dev/ttyACM0
monitor_port = /dev/ttyACM0

; Board real e LilyGO T-A7670E R2 (ESP32-WROVER-E, 4MB flash, 8MB PSRAM QSPI).
; esp-wrover-kit e o board mais proximo disponivel no PlatformIO;
; ajustes finos de flash/PSRAM ficam no sdkconfig.defaults.
```

**`sdkconfig.defaults`:**
```
CONFIG_LWIP_IPV4_NAPT=y
CONFIG_LWIP_IP_FORWARD=y
CONFIG_LWIP_NAPT=y
CONFIG_FREERTOS_HZ=1000
CONFIG_AUTOSTART_ARDUINO=y
```

> Por que cada linha existe:
> - `LWIP_IPV4_NAPT` / `LWIP_IP_FORWARD` / `LWIP_NAPT`: habilitam NAT no lwIP — necessário para o ESP32 rotear tráfego entre WiFi AP e a interface PPP do modem (fase futura do roteador).
> - `FREERTOS_HZ=1000`: o componente Arduino como parte do ESP-IDF exige FreeRTOS rodando a 1000Hz (o default do ESP-IDF puro é 100Hz); sem isso o build falha no CMake.
> - `AUTOSTART_ARDUINO=y`: gera automaticamente a função `app_main()` que chama `setup()`/`loop()`. Sem essa flag, o linker falha com `undefined reference to 'app_main'`.

## 6. Primeira compilação

```bash
pio run
```

A primeira execução baixa toolchain, ESP-IDF e todas as dependências — demora vários minutos. Se der erro de cache (ex: mudança no `sdkconfig.defaults` não refletida), limpe antes de rodar de novo:

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

- `Permission denied: /dev/ttyACM0` → grupo `dialout` não aplicado ainda nesta sessão. Confirme com `id`; se faltando, refaça o passo 3 e faça logout/login.
- `Device or resource busy` → outro processo já tem a porta serial aberta (Monitor Serial rodando em outra aba/terminal). Identifique com `lsof /dev/ttyACM0` e finalize o processo (`kill -9 <PID>`), ou feche o monitor antes de fazer upload.

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
- **Board PlatformIO usado:** `esp-wrover-kit` — é o mais próximo disponível no catálogo, não é um board dedicado para esta placa LilyGO. O aviso `Flash memory size mismatch detected. Expected 4MB, found 2MB!` que aparece no build é decorrente disso e pode ser ignorado por enquanto (ou ajustado depois, se necessário, especificando o tamanho real de flash no `sdkconfig.defaults`).
- **`BOARD_POWERON_PIN` (GPIO12):** precisa ser setado em `HIGH` no `setup()`, depois do boot — placas T-A7670/T-A7608 desligam sozinhas quando rodando só na bateria (sem USB) se esse pino não for ativado pelo firmware.