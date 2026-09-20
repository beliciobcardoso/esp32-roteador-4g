#include "html_page.h"

const char kConfigPageTemplate[] = R"HTML(
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Config Roteador 4G</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background-color: #0056b3;
        }

        nav {
            margin-bottom: 20px;
        }

        nav a {
            text-decoration: none;
            color: #007BFF;
            background-color: #0056b3;
            padding: 5px 10px;
            margin-right: 10px;
        }

        nav a:hover {
            text-decoration: underline;
        }

        form {
            max-width: 400px;
        }

        label {
            display: block;
            margin-bottom: 10px;
        }

        input[type="text"],
        input[type="password"],
        input[type="number"],
        input[type="file"],
        select {
            width: 100%;
            padding: 8px;
            margin-top: 4px;
            box-sizing: border-box;
        }

        button {
            padding: 10px 15px;
            background-color: #007BFF;
            color: white;
            border: none;
            cursor: pointer;
        }

        button:hover {
            background-color: #0056b3;
        }

        .alert {
            background-color: #ffdd57;
            color: #3d3000;
            padding: 10px;
            max-width: 400px;
            font-weight: bold;
        }
    </style>
</head>

<body>
    <nav>
        <a href="/">Home</a> |
        <a href="/status">Status</a> |
        <a href="/config">Configuração</a>
    </nav>
    <div id="home">
        <h1>Bem-vindo ao Roteador 4G</h1>
        <p>Use o menu acima para navegar entre as páginas de status e configuração.</p>
    </div>
    <div id="status">
        <h1>Status do Roteador 4G</h1>
        <p>Uplink 4G: {{UPLINK_STATUS}}</p>
        <p>SSID WiFi: {{SSID}}</p>
        <p>APN: {{APN}}</p>
        <p>Usuario APN: {{APN_USER}}</p>
        <p>Usuario admin: {{ADMIN_USER}}</p>
        <p>Relogio: {{CLOCK}}</p>
    </div>
    <div id="config">
    <h1>Configuracao do Roteador 4G</h1>
    {{ADMIN_NOTICE}}
    <h2>Uplink 4G</h2>
    <p>{{UPLINK_STATUS}}</p>
    <form method="POST" action="/">
        <label>SSID WiFi: <input type="text" name="wifi_ssid" value="{{SSID}}"></label><br>
        <label>Senha WiFi: <input type="password" name="wifi_password"
                placeholder="deixe em branco para manter"></label><br>
        <label>APN: <input type="text" name="apn" value="{{APN}}"></label><br>
        <label>Usuario APN: <input type="text" name="apn_user" value="{{APN_USER}}"></label><br>
        <label>Senha APN: <input type="password" name="apn_password"
                placeholder="deixe em branco para manter"></label><br>
        <label>Usuario admin: <input type="text" name="admin_user" value="{{ADMIN_USER}}"></label><br>
        <label>Senha admin: <input type="password" name="admin_password"
                placeholder="deixe em branco para manter"></label><br>
        <label>Fuso horario: <select name="timezone">{{TIMEZONE_OPTIONS}}</select></label><br>
        <label>Divisor da bateria: <input type="number" name="battery_ratio" step="0.01"
                min="1.4" max="10" value="{{BATTERY_RATIO}}"></label><br>
        <button type="submit">Salvar</button>
    </form>
    <h2>Firmware</h2>
    <p>Slot em execucao: {{FIRMWARE_SLOT}}</p>
    <p>Versao: {{FIRMWARE_VERSION}}</p>
    <p>Estado da imagem: {{FIRMWARE_STATE}}</p>
    <!-- enctype e obrigatorio: sem ele o navegador manda o nome do arquivo como texto e o
         corpo chega sem um byte do firmware. -->
    <form method="POST" action="/update" enctype="multipart/form-data">
        <label>Arquivo firmware.bin: <input type="file" name="firmware" accept=".bin"></label><br>
        <button type="submit">Enviar e reiniciar</button>
    </form>
    </div>
</body>

</html>
)HTML";

String escapeForHtmlAttribute(const String& value) {
  String escaped;
  escaped.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value[i];
    switch (character) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      // Este nao e escape de HTML: '{' vira entidade para que um valor gravado nao consiga
      // forjar um placeholder. Sem isso um SSID igual a "{{APN}}" atravessa o replace do
      // SSID intacto e o replace seguinte o troca pelo APN, enchendo o campo errado.
      // Renderiza como '{' do mesmo jeito.
      case '{': escaped += "&#123;"; break;
      default: escaped += character; break;
    }
  }

  return escaped;
}
