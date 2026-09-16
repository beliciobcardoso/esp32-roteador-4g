#include "http_config_handler.h"

#include "html_page.h"

namespace {
const int kServerPort = 80;
const char* kAuthRealm = "roteador-4g";
}  // namespace

HttpConfigHandler::HttpConfigHandler(LoadSettingsUseCase& loadUseCase, SaveSettingsUseCase& saveUseCase)
    : loadUseCase_(loadUseCase), saveUseCase_(saveUseCase), server_(kServerPort) {}

void HttpConfigHandler::begin() {
  server_.on("/", HTTP_GET, [this]() { handleGetRoot(); });
  server_.on("/", HTTP_POST, [this]() { handlePostRoot(); });
  server_.begin();
}

void HttpConfigHandler::handleClient() {
  server_.handleClient();
}

bool HttpConfigHandler::authenticate(const RouterSettings& current) {
  if (server_.authenticate(current.admin_user.c_str(), current.admin_password.c_str())) {
    return true;
  }
  server_.requestAuthentication(BASIC_AUTH, kAuthRealm);
  return false;
}

void HttpConfigHandler::handleGetRoot() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  String page = kConfigPageTemplate;
  page.replace("{{SSID}}", current.wifi_ssid);
  page.replace("{{APN}}", current.apn);
  page.replace("{{ADMIN_USER}}", current.admin_user);
  server_.send(200, "text/html", page);
}

void HttpConfigHandler::handlePostRoot() {
  RouterSettings current = loadUseCase_.execute();
  if (!authenticate(current)) return;

  // Campos de senha em branco mantem o valor atual — evita forcar o usuario
  // a retypar a senha toda vez que so quer mudar o SSID, por exemplo.
  RouterSettings updated;
  updated.wifi_ssid = server_.arg("wifi_ssid");
  String newWifiPassword = server_.arg("wifi_password");
  updated.wifi_password = newWifiPassword.length() > 0 ? newWifiPassword : current.wifi_password;
  updated.apn = server_.arg("apn");
  updated.admin_user = server_.arg("admin_user");
  String newAdminPassword = server_.arg("admin_password");
  updated.admin_password = newAdminPassword.length() > 0 ? newAdminPassword : current.admin_password;

  SaveSettingsResult result = saveUseCase_.execute(updated);
  if (!result.success) {
    server_.send(400, "text/plain", to_string(result.error));
    return;
  }

  server_.send(200, "text/plain", "Configuracao salva.");
}
