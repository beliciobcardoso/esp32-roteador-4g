#!/usr/bin/env python3
"""Serve http/index.html no PC, com as rotas da API respondendo dados de mentira.

Existe para trabalhar a modelagem da pagina sem gravar a placa a cada ajuste de CSS. As
rotas e os campos sao os mesmos que o firmware devolve — quando divergirem, e aqui que
tem que ser corrigido, porque o contrato de verdade esta em
adapters/http_config_handler.cpp.

Os valores sao os da unidade de bancada, copiados de um print da placa real, e nao
valores curtinhos inventados: foi justamente uma versao de firmware comprida
("88d4901-dirty (Sep 20 2026 20:15:33) build d7385182") que estourou o layout numa tela
de celular enquanto o mock antigo, com um hash de 7 caracteres, mostrava tudo certo.

    python3 tools/preview.py            # http://127.0.0.1:8765
    python3 tools/preview.py --porta 9000
    python3 tools/preview.py --cenario caindo
"""

import argparse
import http.server
import json
import pathlib
import socketserver

RAIZ = pathlib.Path(__file__).resolve().parent.parent
PAGINA = RAIZ / "http" / "index.html"

# O firmware longo do cenario padrao nao e enfeite: e o caso que quebra o layout.
VERSAO_LONGA = "88d4901-dirty (Sep 20 2026 20:15:33) build d7385182"

CENARIOS = {
    "online": {
        "uplink_state": "online",
        "uplink_text": "Conectado. Os clientes do Wi-Fi saem pela rede 4G.",
        "uplink_failures": 0,
        "uplink_rebooted": False,
        "uplink_exhausted": False,
        "clock_text": "20/09/2026 20:44:21",
        "clock_synced": True,
        "battery_volts": 4.16,
        "battery_percent": 96,
        "firmware_slot": "app0",
        "firmware_version": VERSAO_LONGA,
        "firmware_state": "confirmado",
        "firmware_needs_confirmation": False,
        "firmware_deadline_min": 10,
        "admin_password_pending": False,
        "admin_notice": "",
    },
    "caindo": {
        "uplink_state": "backoff",
        "uplink_text": "Sem conexao apos 4 tentativas. Nova tentativa em instantes.",
        "uplink_failures": 4,
        "uplink_rebooted": True,
        "uplink_exhausted": False,
        "clock_text": "ainda nao sincronizado (precisa do uplink 4G).",
        "clock_synced": False,
        "battery_volts": 3.42,
        "battery_percent": 18,
        "firmware_slot": "app1",
        "firmware_version": VERSAO_LONGA,
        "firmware_state": "Imagem em verificacao: aguardando confirmacao.",
        "firmware_needs_confirmation": True,
        "firmware_deadline_min": 10,
        "admin_password_pending": True,
        "admin_notice": "A senha de admin precisa ser trocada antes de qualquer outra gravacao.",
    },
}

# SSID com acento e um nome de APN comprido: o que passa por escape de JSON e o que estica
# um campo de formulario em tela estreita.
CONFIG = {
    "wifi_ssid": "esp32-roteador-4g",
    "apn": "zap.vivo.com.br",
    "apn_user": "vivo",
    "admin_user": "admin",
    "timezone": "<-03>3",
    "battery_ratio": 2.19,
    "timezones": [
        {"posix": "<-02>2", "label": "Fernando de Noronha (UTC-2)"},
        {"posix": "<-03>3", "label": "Brasilia (UTC-3)"},
        {"posix": "<-04>4", "label": "Manaus (UTC-4)"},
        {"posix": "<-05>5", "label": "Rio Branco (UTC-5)"},
    ],
}


def construir_handler(cenario):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, formato, *args):
            print("  %s %s" % (self.command, self.path))

        def _json(self, corpo, codigo=200):
            bruto = json.dumps(corpo, ensure_ascii=False).encode("utf-8")
            self.send_response(codigo)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(bruto)))
            self.end_headers()
            self.wfile.write(bruto)

        def _caminho(self):
            return self.path.split("?")[0]

        def do_GET(self):
            caminho = self._caminho()
            if caminho == "/":
                # Lido a cada pedido, nao no import: editar o HTML e dar F5 tem que bastar.
                # Sem isso o servidor serve a versao que existia quando ele subiu, e a
                # correcao "nao aparece" por um motivo que nao esta no codigo da pagina.
                corpo = PAGINA.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(corpo)))
                self.end_headers()
                self.wfile.write(corpo)
            elif caminho == "/api/status":
                self._json(CENARIOS[cenario])
            elif caminho == "/api/config":
                self._json(CONFIG)
            else:
                self._json({"erro": "nao encontrado"}, 404)

        def do_POST(self):
            self.rfile.read(int(self.headers.get("Content-Length", 0)))
            caminho = self._caminho()
            if caminho == "/api/config":
                self._json({
                    "mensagem": "Configuracao salva. Fuso e calibracao da bateria ja valem.",
                    "uplink_reconectando": False,
                    "wifi_exige_reboot": False,
                    "local_ja_vale": True,
                })
            elif caminho == "/update":
                # ?falha=1 exercita o caminho de erro, que e o que ninguem testa a mao.
                if "falha=1" in self.path:
                    self._json({"erro": "a imagem enviada nao passou na verificacao"}, 400)
                else:
                    self._json({"mensagem": "Firmware gravado. A placa reinicia agora."})
            elif caminho == "/firmware/confirmar":
                self._json({"mensagem": "Atualizacao confirmada."})
            else:
                self._json({"erro": "nao encontrado"}, 404)

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--porta", type=int, default=8765)
    parser.add_argument("--cenario", choices=sorted(CENARIOS), default="online",
                        help="online: tudo certo. caindo: sem uplink, bateria baixa, "
                             "firmware por confirmar e senha de admin pendente.")
    argumentos = parser.parse_args()

    socketserver.TCPServer.allow_reuse_address = True
    endereco = ("127.0.0.1", argumentos.porta)
    with socketserver.TCPServer(endereco, construir_handler(argumentos.cenario)) as servidor:
        print("Preview em http://127.0.0.1:%d (cenario: %s)" % (argumentos.porta, argumentos.cenario))
        print("Edite http/index.html e recarregue — a pagina e lida a cada pedido.")
        try:
            servidor.serve_forever()
        except KeyboardInterrupt:
            print("\nencerrado")


if __name__ == "__main__":
    main()
