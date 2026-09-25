#include "loop_health.h"

const uint32_t kLoopStallLimitMs = 30000;

bool loopHasStalled(uint32_t now, uint32_t lastBeatMs) {
  return (now - lastBeatMs) >= kLoopStallLimitMs;
}
