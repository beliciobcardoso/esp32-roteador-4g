#!/usr/bin/env python3
# Monitor serial da bancada. Mostra o serial como a placa escreve, sem os codigos de cor,
# e faz as duas coisas que a bancada precisa toda hora e o `pio device monitor` nao faz:
# gravar o serial em arquivo enquanto o usuario testa pelo celular (ele nao aceita stdin
# redirecionado, ver docs/ONDE_PARAMOS.md, "Bancada") e reiniciar a placa com a captura ja
# anexada, que e a unica forma de o log pegar o boot inteiro.
#
# Nao poe hora: a placa ja poe nas linhas do projeto (`infra/timestamped_serial`), e uma
# segunda hora, a do PC, so duplicaria a coluna.
#
# Nao e um build script: roda sozinho, fora do SCons.
#
#   python3 scripts/serial_monitor.py                 # so olha
#   python3 scripts/serial_monitor.py --log ota.log   # olha e grava
#   python3 scripts/serial_monitor.py --reset         # reinicia a placa antes de ler
#
# A porta sai da mesma configuracao que o `pio run -t upload` usa — `monitor_port` do
# `platformio_override.ini`, se existir, senao do `platformio.ini` (debito 20) —, para uma
# maquina com outro adaptador nao precisar editar este arquivo. `--port` passa por cima.
#
# Fechar com Ctrl+C antes de gravar a placa: a porta presa faz o upload falhar.

import argparse
import configparser
import os
import re
import sys
import time

import serial

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV_SECTION = "env:esp-wrover-kit"
BAUD_RATE = 115200

# Pulso em RTS com DTR baixo: EN vai a zero e IO0 fica alto, entao a placa reinicia no
# firmware, nao no bootloader de gravacao. 200 ms e o que funcionou na bancada.
RESET_PULSE_S = 0.2

ANSI_COLOR = re.compile(rb"\x1b\[[0-9;]*m")


def configured_port():
    # Ordem de leitura = ordem de precedencia: o override, lido depois, ganha. Sem
    # interpolacao porque o platformio.ini usa ${...}, que o configparser nao entende.
    config = configparser.ConfigParser(interpolation=None)
    config.read([os.path.join(PROJECT_DIR, name)
                 for name in ("platformio.ini", "platformio_override.ini")])
    return config.get(ENV_SECTION, "monitor_port", fallback=None)


def open_port(path):
    # DTR e RTS baixos ANTES de abrir: com o padrao do pyserial eles sobem na abertura e a
    # placa reinicia sem ninguem pedir, o que apaga justamente o estado que se queria ver.
    port = serial.Serial()
    port.port = path
    port.baudrate = BAUD_RATE
    port.timeout = 0.2
    port.dtr = False
    port.rts = False
    # O Linux deixa dois processos abrirem a mesma porta, e os dois disputam cada leitura.
    # O exclusivo barra so quem tambem pede exclusivo (outra copia deste script); contra um
    # `pio device monitor` esquecido em outra aba quem avisa e o tratamento em main().
    port.exclusive = True
    port.open()
    return port


def reset_board(port):
    port.rts = True
    time.sleep(RESET_PULSE_S)
    port.rts = False


def stream(port, log_file):
    pending = b""
    while True:
        pending += port.read(4096)
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = ANSI_COLOR.sub(b"", raw).decode("utf-8", "replace").rstrip("\r")
            print(line, flush=True)
            if log_file is not None:
                log_file.write(line + "\n")
                log_file.flush()


def busy_port_exit(path, error):
    sys.exit(f"porta {path}: {error}\nporta presa? `fuser {path}` mostra o PID")


def main():
    parser = argparse.ArgumentParser(description="Monitor serial da bancada.")
    parser.add_argument("--port", help="porta serial; padrao: monitor_port do platformio")
    parser.add_argument("--log", help="grava as linhas neste arquivo, acrescentando")
    parser.add_argument("--reset", action="store_true",
                        help="reinicia a placa depois de abrir a porta, para pegar o boot")
    args = parser.parse_args()

    path = args.port or configured_port()
    if not path:
        sys.exit(f"sem porta: passe --port ou defina monitor_port em [{ENV_SECTION}]")

    try:
        port = open_port(path)
    except serial.SerialException as error:
        # O caso comum e a porta presa por outro monitor ou por um upload em andamento.
        busy_port_exit(path, error)

    log_file = open(args.log, "a", encoding="utf-8") if args.log else None
    try:
        if args.reset:
            reset_board(port)
        stream(port, log_file)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as error:
        # Com outro processo lendo a mesma porta, a abertura passa e a falha so aparece aqui,
        # como "device reports readiness to read but returned no data" — que nao diz isso.
        busy_port_exit(path, error)
    finally:
        port.close()
        if log_file is not None:
            log_file.close()


if __name__ == "__main__":
    main()
