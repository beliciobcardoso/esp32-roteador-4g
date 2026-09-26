#include "link_supervisor.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "../domain/firmware_update.h"
#include "timestamped_serial.h"

namespace {

// A sequencia de conexao pode bloquear ~160s. A task nao pode competir com o lwIP nem
// ficar abaixo do loop(), que so precisa de fatias curtas pro HTTP.
constexpr uint32_t kTaskStackBytes = 6144;
constexpr UBaseType_t kTaskPriority = 5;
// Core 1 e onde o Arduino roda; o WiFi/lwIP fica no core 0. Manter o supervisor longe do
// core da pilha de rede evita que uma espera longa dele atrase o tratamento de pacote.
constexpr BaseType_t kTaskCore = 1;

// Attach LTE ja terminou quando chegamos aqui; falta so LCP/IPCP, questao de segundos.
// Um minuto e folga pra rede ruim, nao expectativa.
constexpr uint32_t kIpTimeoutMs = 60000;

// Com o LCP echo ligado (CONFIG_LWIP_ENABLE_LCP_ECHO), uma queda silenciosa vira
// ERRORPEERDEAD em ~9s. Sondar o estado a cada 1s e barato e nao adianta nada mais rapido.
constexpr uint32_t kOnlinePollMs = 1000;

// Backoff progressivo: reconexao imediata nao resolve queda de cobertura e ainda queima
// bateria repetindo o power-on do modem, que sozinho ja custa ~3s.
constexpr uint32_t kBackoffStepsMs[] = {5000, 10000, 20000, 40000, 60000};
constexpr int kBackoffStepCount = sizeof(kBackoffStepsMs) / sizeof(kBackoffStepsMs[0]);

// Dez falhas seguidas passam de 4 minutos de tentativa. A essa altura o que sobrou de
// causa plausivel e estado travado que so o boot limpa — o reset do modem ja foi tentado
// dez vezes e nao adiantou. Derruba o AP junto, mas um AP no ar sem internet nenhuma ja
// nao esta servindo pra nada.
constexpr uint32_t kMaxFailuresBeforeReboot = 10;

// Mas o reboot so cura o que e estado travado de software. Se dois ciclos completos nao
// resolveram, a causa e externa — SIM fora, sem cobertura, sem credito — e reiniciar de
// novo nao traz o 4G de volta: so derruba o AP a cada ~12 min, que e justamente por onde
// alguem chegaria na pagina de configuracao para descobrir o que houve. Validado em
// bancada com o SIM removido: um cliente associou durante a oitava falha, e o reboot
// seguinte o desconectou sem nenhum ganho.
constexpr uint32_t kMaxRebootsWithoutUplink = 2;

// Vive na RTC RAM sem inicializacao de propósito: o esp_restart() preserva o valor, que e
// como o contador atravessa o reboot que ele mesmo dispara. Depois de um power-on de
// verdade o conteudo e lixo — quem separa os dois casos e o esp_reset_reason() no begin(),
// e nao um magic word, que custaria o mesmo trabalho para ainda errar 1 em 2^32.
//
// volatile pela mesma razao que state_ e consecutiveFailures_: escrita so pela task do
// supervisor, lida tambem pela task do loop() em status(). Palavra alinhada, sem
// leitura-modificacao-escrita cruzada entre as duas.
RTC_NOINIT_ATTR volatile uint32_t gRebootsWithoutUplink;

uint32_t backoffForFailure(uint32_t failureCount) {
  int index = static_cast<int>(failureCount) - 1;
  if (index < 0) {
    index = 0;
  }
  if (index >= kBackoffStepCount) {
    index = kBackoffStepCount - 1;
  }
  return kBackoffStepsMs[index];
}

}  // namespace

LinkSupervisor::LinkSupervisor(ModemPpp& modem, NatBridge& nat) : modem_(modem), nat_(nat) {}

UplinkStatus LinkSupervisor::status() const {
  UplinkStatus current;
  current.state = state_;
  current.consecutive_failures = consecutiveFailures_;
  current.rebooted_for_uplink = gRebootsWithoutUplink > 0;
  // Esgotado significa que os reinicios acabaram E que o numero de falhas ja justificaria
  // mais um. Sem a segunda metade, uma queda nova depois de dois reinicios gastos cairia
  // direto na mensagem de causa externa na primeira falha, antes de a placa ter tentado
  // as dez vezes que costumam resolver.
  current.reboot_budget_exhausted = gRebootsWithoutUplink >= kMaxRebootsWithoutUplink &&
                                    consecutiveFailures_ >= kMaxFailuresBeforeReboot;
  return current;
}

// Le o estado da imagem direto do otadata em vez de receber por parametro: a decisao e
// tomada dentro da task do supervisor, que roda por ate 160s por volta, e um valor passado
// no begin() estaria velho justamente no momento que importa — logo depois de uma
// atualizacao. A leitura e de flash, mas so acontece na decima falha seguida de uplink.
bool LinkSupervisor::mayRebootNow() const {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t raw = ESP_OTA_IMG_UNDEFINED;
  if (running == nullptr || esp_ota_get_state_partition(running, &raw) != ESP_OK) {
    // Sem leitura confiavel, reiniciar segue permitido: o reboot por falta de uplink e a
    // recuperacao que existe ha mais tempo e que funciona sem depender deste caminho.
    return true;
  }
  return supervisorMayRebootForUplink(raw == ESP_OTA_IMG_PENDING_VERIFY
                                          ? FirmwareImageState::PendingVerify
                                          : FirmwareImageState::Valid);
}

bool LinkSupervisor::begin(const RouterSettings& settings) {
  // Reset por software e o unico que preserva a RTC RAM com significado aqui: foi o
  // esp_restart() logo abaixo. Qualquer outro motivo (power-on, botao de reset, brownout)
  // e um comeco novo, e o orcamento de reinicios volta ao cheio.
  if (esp_reset_reason() != ESP_RST_SW) {
    gRebootsWithoutUplink = 0;
  }

  settingsMutex_ = xSemaphoreCreateMutex();
  if (settingsMutex_ == nullptr) {
    logSerial.println("Uplink: sem memoria para o mutex de configuracao");
    return false;
  }

  // Semeia a configuracao inicial pelo mesmo caminho de uma troca em runtime: a task
  // acorda, ve o pending e conecta com ele. Um caminho so, sem inicializacao especial.
  pendingSettings_ = settings;
  settingsDirty_ = true;

  BaseType_t created = xTaskCreatePinnedToCore(&LinkSupervisor::taskEntry, "uplink", kTaskStackBytes,
                                               this, kTaskPriority, &task_, kTaskCore);
  if (created != pdPASS) {
    logSerial.println("Uplink: nao foi possivel criar a task de supervisao");
    return false;
  }
  return true;
}

void LinkSupervisor::applySettings(const RouterSettings& settings) {
  if (settingsMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(settingsMutex_, portMAX_DELAY);
  pendingSettings_ = settings;
  settingsDirty_ = true;
  xSemaphoreGive(settingsMutex_);
}

// Puxa a configuracao pendente para a copia de trabalho, se houver uma. Devolve true
// quando algo mudou de fato.
bool LinkSupervisor::refreshSettings() {
  bool changed = false;

  xSemaphoreTake(settingsMutex_, portMAX_DELAY);
  if (settingsDirty_) {
    activeSettings_ = pendingSettings_;
    settingsDirty_ = false;
    changed = true;
  }
  xSemaphoreGive(settingsMutex_);

  return changed;
}

bool LinkSupervisor::waitInterruptible(uint32_t delayMs) {
  for (uint32_t waited = 0; waited < delayMs; waited += kOnlinePollMs) {
    vTaskDelay(pdMS_TO_TICKS(kOnlinePollMs));
    if (refreshSettings()) {
      return true;
    }
  }
  return false;
}

void LinkSupervisor::taskEntry(void* context) {
  static_cast<LinkSupervisor*>(context)->run();
}

// Uma tentativa completa: derruba o que sobrou da sessao anterior, sobe o modem, espera o
// IP e rearma o NAT. O NAT precisa ser refeito toda vez — o netif e novo e o DNS da
// operadora pode ter mudado entre sessoes.
bool LinkSupervisor::connectOnce(const RouterSettings& settings) {
  modem_.stop();

  if (!modem_.start(settings)) {
    logSerial.println("Uplink: modem nao subiu");
    return false;
  }

  if (!modem_.waitForIp(kIpTimeoutMs)) {
    logSerial.println("Uplink: operadora nao entregou IP");
    return false;
  }

  if (!nat_.enable(modem_.netif())) {
    logSerial.println("Uplink: NAT nao armou — clientes do AP nao saem pra internet");
    return false;
  }

  return true;
}

void LinkSupervisor::run() {
  for (;;) {
    bool changed = refreshSettings();

    if (state_ == UplinkState::Online) {
      if (changed) {
        logSerial.println("Uplink: configuracao nova — reconectando");
      } else if (!modem_.hasIp()) {
        logSerial.println("Uplink: enlace caiu — reconectando");
      } else {
        vTaskDelay(pdMS_TO_TICKS(kOnlinePollMs));
        continue;
      }
      state_ = UplinkState::Connecting;
    }

    if (connectOnce(activeSettings_)) {
      consecutiveFailures_ = 0;
      // Uplink de pe: se algum reinicio anterior ajudou, ele cumpriu o papel. Zerar aqui
      // evita que quedas separadas por dias de funcionamento normal somem no mesmo
      // orcamento e acabem suprimindo um reboot que teria resolvido.
      gRebootsWithoutUplink = 0;
      state_ = UplinkState::Online;
      logSerial.println("Uplink: online — clientes do AP saem pelo 4G");
      // Depois do estado e do log: o ouvinte roda nesta task, entao qualquer coisa que ele
      // demore atrasa o proximo ciclo de supervisao. Avisar por ultimo mantem o estado
      // consistente para quem consultar de fora enquanto isso acontece.
      if (uplinkOnline_ != nullptr) {
        uplinkOnline_();
      }
      continue;
    }

    consecutiveFailures_++;
    modem_.stop();

    if (consecutiveFailures_ >= kMaxFailuresBeforeReboot) {
      if (!mayRebootNow()) {
        // So na transicao, pelo mesmo motivo da mensagem de causa externa mais abaixo.
        if (consecutiveFailures_ == kMaxFailuresBeforeReboot) {
          logSerial.println(
              "Uplink: reinicio segurado — ha uma atualizacao de firmware esperando "
              "confirmacao, e reiniciar agora faria o bootloader reverte-la por falta de "
              "sinal, que nao e defeito dela. Segue tentando sem reiniciar.");
        }
      } else if (gRebootsWithoutUplink < kMaxRebootsWithoutUplink) {
        gRebootsWithoutUplink++;
        logSerial.printf("Uplink: %u falhas seguidas — reiniciando a placa (reinicio %u/%u)\n",
                      consecutiveFailures_, gRebootsWithoutUplink, kMaxRebootsWithoutUplink);
        Serial.flush();
        esp_restart();
      }

      // So na transicao. Daqui pra frente a condicao segue verdadeira a cada falha, e
      // repetir a mesma linha a cada 60s afogaria o resto do serial numa depuracao.
      if (consecutiveFailures_ == kMaxFailuresBeforeReboot) {
        logSerial.printf(
            "Uplink: %u reinicios nao resolveram — causa externa (SIM, cobertura ou credito). "
            "Segue tentando sem reiniciar; o AP continua no ar.\n",
            kMaxRebootsWithoutUplink);
      }
    }

    uint32_t backoffMs = backoffForFailure(consecutiveFailures_);
    if (gRebootsWithoutUplink < kMaxRebootsWithoutUplink) {
      logSerial.printf("Uplink: falha %u/%u — nova tentativa em %us\n", consecutiveFailures_,
                    kMaxFailuresBeforeReboot, backoffMs / 1000);
    } else {
      // Sem orcamento de reinicio o denominador nao significa mais nada: o contador segue
      // subindo e o serial mostrava "falha 12/10", que sugere um limite ja estourado quando
      // na verdade nao ha mais limite nenhum a atingir.
      logSerial.printf("Uplink: falha %u — nova tentativa em %us\n", consecutiveFailures_,
                    backoffMs / 1000);
    }

    state_ = UplinkState::Backoff;
    if (waitInterruptible(backoffMs)) {
      logSerial.println("Uplink: configuracao nova durante o backoff — tentando ja");
    }
    state_ = UplinkState::Connecting;
  }
}
