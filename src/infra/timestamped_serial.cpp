#include "timestamped_serial.h"

#include <time.h>

TimestampedSerial logSerial;

namespace {

// "HH:MM:SS " — oito caracteres, o espaco e o terminador.
constexpr size_t kPrefixBufferSize = 10;

}  // namespace

void TimestampedSerial::begin(const Clock& clock) { clock_ = &clock; }

void TimestampedSerial::writePrefix() {
  char prefix[kPrefixBufferSize] = "--:--:-- ";
  if (clock_ != nullptr && clock_->synchronized()) {
    time_t now = time(nullptr);
    struct tm local = {};
    localtime_r(&now, &local);
    strftime(prefix, sizeof(prefix), "%H:%M:%S ", &local);
  }
  Serial.print(prefix);
}

size_t TimestampedSerial::write(uint8_t byte) { return write(&byte, 1); }

// Repassa em trechos que terminam no '\n', e nao byte a byte: cada Serial.write toma a
// trava do UART, e um printf de 100 caracteres viraria 100 travas.
size_t TimestampedSerial::write(const uint8_t* buffer, size_t size) {
  size_t start = 0;
  while (start < size) {
    if (atLineStart_) {
      writePrefix();
      atLineStart_ = false;
    }
    size_t end = start;
    while (end < size && buffer[end] != '\n') ++end;
    if (end < size) {
      ++end;  // o '\n' vai junto com a linha que ele fecha
      atLineStart_ = true;
    }
    Serial.write(buffer + start, end - start);
    start = end;
  }
  return size;
}
