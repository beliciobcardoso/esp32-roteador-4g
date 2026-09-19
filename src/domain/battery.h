#pragma once

// ENTIDADE — conversao tensao -> percentual de carga. Regra pura: nao le pino, nao conhece
// ADC e nao inclui Arduino, entao compila e roda no host (`pio test -e native`).
// A leitura do hardware e de infra/battery_adc.

// Percentual estimado de carga para `voltage`, pela curva de descarga de uma Li-ion 1S.
// Satura em 100 acima do topo da curva e em 0 abaixo da base — a curva nao extrapola,
// porque fora dela a relacao tensao/carga deixa de valer.
//
// Devolve int arredondado, nao truncado. Se algum consumidor futuro precisar de resolucao
// abaixo de 1% (log, telemetria), a assinatura e que tem que mudar — arredondar aqui e
// deixar o chamador lidar com a perda seria esconder a decisao de novo.
int voltageToPercent(float voltage);
