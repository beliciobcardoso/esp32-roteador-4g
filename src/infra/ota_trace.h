#pragma once

#include <cstddef>

// SPIKE (tmp/spike-wireguard) — instrumentacao de bancada do travamento da OTA pelo tunel
// (CONTINUIDADE-DISPOSITIVO.md, secao 6). Nao vai para a developer.
//
// Responde duas perguntas que o serial de hoje nao responde:
//   1. Onde o upload para: no meio dos dados, no UPLOAD_FILE_END ou dentro do Update.end().
//   2. Quando o LoopWatchdog dispara, o loop esta esperando socket ou preso no lock da tcpip
//      — e, se for o lock, qual task o segura.
namespace OtaTrace {

// A cada bloco do upload. Imprime uma linha a cada 64 KB com vazao da janela, bytes do
// enlace, eventos da UART do modem, descartes do PPP e heap. Chamada pelo loopTask.
void onChunk(size_t totalBytes, bool isStart);

// Marca uma fase ("end", "finish:antes", "finish:ok", "abortado") com o tempo desde o
// inicio. Chamada pelo loopTask.
void mark(const char* phase);

// Chamada pela task do LoopWatchdog logo antes do esp_restart(). Le estado escrito pelo
// loopTask sem lock, de proposito: o loop pode estar preso justamente num lock.
void dumpStall();

}  // namespace OtaTrace
