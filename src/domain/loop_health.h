#pragma once

#include <cstdint>

// ENTIDADE — quanto tempo sem o loop() dar sinal de vida ja e travamento. Regra pura: nao
// conhece FreeRTOS, watchdog nem esp_restart.
//
// Existe por causa de uma medicao de bancada em 24/09/2026. Um upload de firmware pela
// pagina foi interrompido do jeito que acontece de verdade — o celular saiu de alcance, sem
// fechar a conexao —, e a placa parou. Nao por alguns segundos: o serial ficou mudo por 137
// s enquanto o aparelho esteve fora, e seguiu mudo depois que ele voltou e ate reassociou.
// Com `kBatteryReportIntervalMs` em 3 s, eram ~45 linhas de bateria que nao sairam.
//
// A causa esta no core: `WebServer::_uploadReadByte` espera em
// `while(!client.available() && client.connected()) delay(2);`. Sem FIN do outro lado e sem
// keepalive no socket, `connected()` nunca vira falso e o laco gira para sempre, dentro do
// loop().
//
// Nenhuma das protecoes existentes pega isso. O `CONFIG_ESP_TASK_WDT_PANIC=y` esta ligado,
// mas o core deixa `loopTaskWDTEnabled = false` e o projeto nunca chamou `enableLoopWDT()`,
// entao o loopTask nao esta registrado em watchdog nenhum; as duas idle tasks, essas sim
// vigiadas, continuam rodando porque o `delay(2)` cede a CPU a elas. O resultado e a pior
// forma de falha: o AP e o DHCP seguem de pe, porque vivem em tasks do driver, entao a
// unidade associa o celular e entrega IP enquanto a pagina nao responde, a bateria nao e
// lida e o uplink nao e supervisionado. Parece viva.

// Silencio do loop() a partir do qual a placa se reinicia.
//
// 30 s e escolhido contra dois lados. Por baixo, precisa ser folgado o bastante para nao
// disparar em travessia legitima: o `handleClient()` de um upload de ~1 MB passa dezenas de
// segundos sem devolver o controle, e por isso quem bate o coracao durante o upload e o
// proprio callback de bloco — o limite vale para ausencia de progresso, nao para duracao da
// operacao. Por cima, precisa ser curto o bastante para a unidade voltar sozinha antes de
// alguem precisar ir ate ela, que e o custo que este debito cobra hoje.
//
// Tambem fica bem acima da morte por keepalive (~11 s), de proposito: quando as duas
// correcoes estao ligadas, o keepalive age primeiro e o reinicio so acontece se ele falhar.
// Isso mantem as duas distinguiveis na bancada — abort sem reboot e uma coisa, reboot aos
// ~30 s e outra.
extern const uint32_t kLoopStallLimitMs;

// O loop() passou do limite sem dar sinal?
//
// A conta e por subtracao de unsigned, para atravessar a virada de millis() sem caso
// especial. `uint32_t` e nao `unsigned long` pela regra do AGENTS.md: `unsigned long` tem 8
// bytes no host e 4 na placa, entao a virada nunca seria exercitada pelo teste nativo.
bool loopHasStalled(uint32_t now, uint32_t lastBeatMs);
