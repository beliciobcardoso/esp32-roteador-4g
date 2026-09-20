#pragma once

#include <cstdint>

#include "string_type.h"

// ENTIDADE — estado do uplink 4G e como ele se traduz para quem abriu a pagina de
// configuracao. Regra pura: nao conhece modem, PPP nem HTTP, entao roda no host.
// Quem produz o estado e infra/link_supervisor; quem o exibe e adapters/http_config_handler.

enum class UplinkState {
  Connecting,  // tentativa em andamento
  Online,      // PPP com IP e NAT armado
  Backoff,     // esperando a proxima tentativa
};

struct UplinkStatus {
  UplinkState state = UplinkState::Connecting;

  // Tentativas malsucedidas seguidas. Zera a cada conexao bem-sucedida, entao continua
  // preenchida com o valor da ultima queda enquanto o estado for Online.
  uint32_t consecutive_failures = 0;

  // A placa ja reiniciou pelo menos uma vez por falta de uplink desde o ultimo power-on.
  // Nao e derivavel de consecutive_failures: o contador de falhas e membro da classe e
  // zera no restart, enquanto este atravessa (RTC RAM).
  bool rebooted_for_uplink = false;

  // Dois ciclos completos de reinicio nao trouxeram o uplink de volta, e a placa parou de
  // reiniciar. E o unico estado em que esperar nao resolve nada — a causa esta fora da
  // placa. Ver kMaxRebootsWithoutUplink em infra/link_supervisor.cpp.
  bool reboot_budget_exhausted = false;
};

// Frase para a pessoa que esta associada ao AP sem internet. Responde uma pergunta so:
// esperar resolve? Por isso `reboot_budget_exhausted` ganha do estado corrente — sem
// orcamento a placa segue alternando entre Connecting e Backoff, e mostrar "conectando"
// nesse caso sugere progresso que nao existe.
String describeUplinkStatus(const UplinkStatus& status);
