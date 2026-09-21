#include "uplink_status.h"

String describeUplinkStatus(const UplinkStatus& status) {
  if (status.reboot_budget_exhausted) {
    // Nao diz "aguarde": e o unico caso em que aguardar e a acao errada. Nomeia as tres
    // causas externas plausiveis porque todas as tres se resolvem longe da placa, e sem
    // elas a frase vira so um erro sem saida.
    return "Sem conexão 4G após " + numberToString(status.consecutive_failures) +
           " tentativas e reinícios que não resolveram. A causa provável está fora da placa: "
           "SIM mal encaixado ou sem serviço, área sem cobertura, ou crédito/plano de dados "
           "esgotado. A placa continua tentando, mas só esperar não deve resolver.";
  }

  switch (status.state) {
    case UplinkState::Online:
      // Curta de proposito, ao contrario das outras: esta frase fica na barra do topo da
      // pagina, visivel em todas as telas, e no unico estado em que nao ha nada a fazer.
      // As demais sao compridas porque explicam o que esperar ou o que corrigir.
      //
      // Sem contagem de falha aqui: o campo guarda o valor da ultima queda ate a proxima
      // falha, e exibi-lo diria que algo esta errado agora.
      return "Conexão 4G ativa";

    case UplinkState::Connecting:
      if (status.consecutive_failures == 0) {
        if (status.rebooted_for_uplink) {
          // "Primeira vez" seria mentira aqui: a contagem de falhas zerou junto com o
          // restart que a propria falta de uplink disparou.
          return "Reconectando depois de a placa reiniciar por falta de conexão 4G. Pode "
                 "levar até um minuto.";
        }
        return "Conectando pela primeira vez. Pode levar até um minuto: inclui ligar o "
               "modem, registrar na operadora e negociar o IP.";
      }
      return "Reconectando após " + numberToString(status.consecutive_failures) +
             " tentativa(s) malsucedida(s). Pode levar até um minuto.";

    case UplinkState::Backoff:
      return "Sem conexão 4G. " + numberToString(status.consecutive_failures) +
             " tentativa(s) malsucedida(s) até agora; a próxima tentativa é automática, em "
             "até um minuto. Se o APN estiver errado, corrigi-lo aqui dispara uma tentativa "
             "na hora.";
  }

  // Inalcancavel enquanto o switch cobrir o enum. Fica como rede: estado novo sem caso
  // proprio devolveria texto indefinido numa pagina que existe para informar.
  return "Estado do uplink desconhecido.";
}
