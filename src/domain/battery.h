#pragma once

// ENTIDADE — conversao tensao -> percentual de carga. Regra pura: nao le pino, nao conhece
// ADC e nao inclui Arduino, entao compila e roda no host (`pio test -e native`).
// A leitura do hardware e de infra/battery_adc.

// Percentual estimado de carga para `voltage`, pela curva de descarga de uma Li-ion 1S.
// Satura em 100 acima do topo da curva e em 0 abaixo da base — a curva nao extrapola,
// porque fora dela a relacao tensao/carga deixa de valer.
int voltageToPercent(float voltage);
