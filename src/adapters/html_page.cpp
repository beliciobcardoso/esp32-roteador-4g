#include "html_page.h"

const char kConfigPageTemplate[] = R"HTML(<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>Config Roteador 4G</title></head>
<body>
  <h1>Configuracao do Roteador 4G</h1>
  <form method="POST" action="/">
    <label>SSID WiFi: <input type="text" name="wifi_ssid" value="{{SSID}}"></label><br>
    <label>Senha WiFi: <input type="password" name="wifi_password" placeholder="deixe em branco para manter"></label><br>
    <label>APN: <input type="text" name="apn" value="{{APN}}"></label><br>
<label>Usuario APN: <input type="text" name="apn_user" value="{{APN_USER}}"></label><br>
<label>Senha APN: <input type="password" name="apn_password" placeholder="deixe em branco para manter"></label><br>
    <label>Usuario admin: <input type="text" name="admin_user" value="{{ADMIN_USER}}"></label><br>
    <label>Senha admin: <input type="password" name="admin_password" placeholder="deixe em branco para manter"></label><br>
    <button type="submit">Salvar</button>
  </form>
</body>
</html>
)HTML";
