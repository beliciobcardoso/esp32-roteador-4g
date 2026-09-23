#pragma once

#include <cstdint>

// ENTIDADE — quando tentar conectar no broker, e quanto esperar depois de falhar. Regra
// pura: nao conhece esp_mqtt, socket nem TLS, entao roda no host.
//
// Existe como dominio e nao como parametro do cliente porque o auto-reconnect do
// `esp_mqtt_client` foi desligado de proposito (PRD 14). Ele tenta a cada
// `reconnect_timeout_ms` fixos sem saber se ha rota; com o PPP caido isso e um handshake
// TLS condenado a cada 10 s, caro em heap e em dado movel. Quem sabe se ha rota e o
// `LinkSupervisor`, e a decisao de tentar passa a depender dele.

// Primeiro intervalo depois da primeira falha. Cinco segundos e curto o bastante para uma
// queda momentanea do broker nao virar minuto de silencio.
extern const uint32_t kInitialBackoffMs;

// Teto. Acima disto a espera nao protege mais ninguem e so atrasa a volta: o custo de uma
// tentativa e um handshake, nao uma tempestade.
extern const uint32_t kMaxBackoffMs;

// Quanto tempo conectado antes de considerar a conexao estavel. Sem este patamar, um
// enlace que sobe e cai em dois segundos zeraria o backoff a cada ciclo e a "protecao"
// viraria justamente a tempestade que ela evita.
extern const uint32_t kStableConnectionMs;

// Fracao do backoff sorteada como jitter, em porcentagem para os dois lados.
extern const uint32_t kJitterPercent;

// Espera depois de `consecutiveFailures` falhas seguidas. Zero falhas devolve zero: a
// primeira tentativa nao espera nada, e isso evita um caso especial de "nunca tentou" em
// quem chama.
//
// Dobra a cada falha ate o teto. A progressao satura por comparacao e nao por shift: com
// falha suficiente o shift estouraria o uint32 e a espera voltaria a ser curta bem quando
// deveria ser longa.
uint32_t nextBackoffMs(uint32_t consecutiveFailures);

// Espalha o backoff em +-kJitterPercent usando um valor ja sorteado por quem chama.
//
// O sorteio fica de fora de proposito: dominio nao chama `esp_random`, e um backoff que
// depende de fonte de entropia nao seria testavel. Quem chama passa o numero; aqui so se
// decide o que fazer com ele.
//
// O jitter nao e enfeite de livro. Broker reiniciando derruba a frota inteira no mesmo
// segundo, e sem espalhamento todas voltam no mesmo segundo tambem — o servidor recebe
// N handshakes TLS simultaneos justamente quando acabou de subir.
uint32_t applyJitter(uint32_t backoffMs, uint32_t randomValue);

// Ha tentativa a fazer agora?
//
// Sem uplink, nunca: tentar sem rota gasta heap e dado para falhar, e a placa ja sabe que
// nao ha rota. A subtracao e de unsigned de proposito, para atravessar a virada de
// millis() (~49 dias) — comparacao direta de instantes prenderia a reconexao ate a volta
// seguinte, que e o mesmo cuidado que `dropReportDue` toma em link_diagnostics.
//
// O instante e `uint32_t` e nao `unsigned long`, e a diferenca importa: `unsigned long`
// tem 4 bytes na placa e 8 no host. Com 8 a conta nunca vira, entao um teste de virada
// escrito contra `unsigned long` passa por nao exercitar nada — verde pelo motivo errado.
// `millis()` cabe exatamente em uint32_t, entao nada se perde na placa.
bool shouldAttemptConnect(uint32_t now, uint32_t lastAttemptMs, uint32_t backoffMs,
                          bool uplinkOnline);

// A conexao atual ja durou o bastante para o backoff poder voltar ao inicio?
bool connectionIsStable(uint32_t now, uint32_t connectedSinceMs);
