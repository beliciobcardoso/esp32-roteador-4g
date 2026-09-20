#include "clock.h"

#include <esp_sntp.h>
#include <time.h>

namespace {

// Servidor nacional primeiro: o NIC.br mantem o ntp.br e o RTT daqui e menor. O pool
// global fica como segundo — duas fontes porque uma indisponivel deixaria a placa sem hora
// por tempo indeterminado, e o custo de um servidor a mais e um nome resolvido.
//
// Sao dois de verdade: `CONFIG_LWIP_SNTP_MAX_SERVERS` vale 1 por default e esta subido
// para 2 no `sdkconfig.defaults`. Com o default, o segundo `setservername` seria ignorado
// em silencio.
const char* kPrimaryServer = "a.st1.ntp.br";
const char* kSecondaryServer = "pool.ntp.org";

// Latch de "ja sincronizou alguma vez". Nao da para usar `sntp_get_sync_status()`: ele
// volta a SNTP_SYNC_STATUS_RESET depois que a atualizacao completa (esp_sntp.h), entao
// quem le depois nao distingue "nunca sincronizou" de "sincronizou e o ciclo reiniciou".
//
// Fora da classe porque o callback do lwIP nao carrega contexto — a assinatura e
// `void(struct timeval*)`. Escrito na task do SNTP e lido na do HTTP; bool alinhado, sem
// leitura-modificacao-escrita, entao volatile basta, como no link_supervisor.
volatile bool gSynchronized = false;

// Formata a hora corrente no fuso ja aplicado. Fora da classe porque o callback do lwIP
// nao tem instancia, e os dois caminhos tem que imprimir exatamente o mesmo texto.
String formatNow() {
  time_t now = time(nullptr);
  struct tm local = {};
  localtime_r(&now, &local);

  char text[20];
  strftime(text, sizeof(text), "%d/%m/%Y %H:%M:%S", &local);
  return String(text);
}

void onTimeSynchronized(struct timeval* /*received*/) {
  gSynchronized = true;

  // A unica evidencia de relogio que existe sem um cliente associado. Em campo a placa fica
  // sozinha com o cabo serial, e "a hora esta certa?" nao pode depender de abrir a pagina.
  // Uma linha por sincronizacao: a cadencia e o CONFIG_LWIP_SNTP_UPDATE_DELAY, 1 h.
  Serial.printf("Relogio: sincronizado — %s\n", formatNow().c_str());
}

}  // namespace

void Clock::applyTimezone(const String& posixTimezone) {
  // O relogio do sistema e UTC por dentro; TZ so muda a conversao na leitura. Por isso a
  // troca vale a quente e nao pede ressincronizacao.
  //
  // O valor chega validado pelo dominio (`isKnownTimezone`, chamado dentro do validate()).
  // Isso nao e detalhe: nem `setenv` nem `tzset` reclamam de string sem sentido — a hora
  // sai errada em silencio, que e o defeito que so aparece semanas depois num timestamp.
  setenv("TZ", posixTimezone.c_str(), 1);
  tzset();
}

void Clock::begin(const String& posixTimezone) {
  applyTimezone(posixTimezone);

  // Modo POLL: a placa pergunta. LISTENONLY esperaria broadcast de servidor na LAN, que
  // nao existe aqui — do lado do AP quem responde somos nos.
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, kPrimaryServer);
  esp_sntp_setservername(1, kSecondaryServer);
  sntp_set_time_sync_notification_cb(&onTimeSynchronized);

  // Sem esp_sntp_init() aqui, de proposito: no setup() nao ha uplink, e iniciar agora
  // gastaria uma rodada de consultas que so pode falhar. Quem inicia e o onUplinkOnline().
}

void Clock::onUplinkOnline() {
  // Primeira sessao PPP: inicia. Reconexao: reinicia, para nao esperar o ciclo de
  // `CONFIG_LWIP_SNTP_UPDATE_DELAY` (1 h) depois de a placa ter passado um tempo sem rota.
  //
  // A guarda nao e cosmetica: esp_sntp_init() chamado com o servico ja de pe reinicializa
  // o PCB UDP por baixo do lwIP.
  if (esp_sntp_enabled()) {
    sntp_restart();
    return;
  }
  esp_sntp_init();
}

bool Clock::synchronized() const {
  return gSynchronized;
}

String Clock::nowText() const {
  if (!gSynchronized) return "";
  return formatNow();
}
