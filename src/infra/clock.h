#pragma once

#include <Arduino.h>

// INFRA — hora real por SNTP, com fuso vindo da configuracao.
//
// Existe porque todo log sai com tick do IDF (`I (56015)`), que zera a cada boot e nao diz
// nada depois do fato. Esta fase entrega so o relogio: historico de quedas, agendamento de
// reboot e carimbo de OTA dependem dele, mas nao entram aqui (PRD 07).
//
// A fonte e NTP e nao o `AT+CCLK?` do modem: NITZ depende de a operadora entregar a hora,
// e nem toda entrega. Se aparecer rede sem NTP alcancavel, NITZ vira fallback — hoje seria
// abstracao antes da segunda ocorrencia.
class Clock {
 public:
  // Aplica o fuso e prepara o SNTP. Nao bloqueia e nao sincroniza: sem uplink nao ha
  // servidor a consultar. Chamar no setup().
  void begin(const String& posixTimezone);

  // Troca o fuso sem reiniciar nada. O relogio do sistema e UTC por dentro, entao a hora
  // corrigida vale na proxima leitura — nao ha nada a ressincronizar.
  void applyTimezone(const String& posixTimezone);

  // Chamar quando o uplink entra em Online. Sincroniza na primeira vez e ressincroniza a
  // cada reconexao, que e o unico momento em que se sabe que ha rota para fora.
  void onUplinkOnline();

  // Se alguma sincronizacao ja completou desde o boot. Falso significa que `nowText()` nao
  // vale nada — e esse e o ponto: a placa nunca finge um horario.
  bool synchronized() const;

  // "19/09/2026 14:32:07" no fuso configurado, ou string vazia enquanto nao sincronizou.
  String nowText() const;
};
