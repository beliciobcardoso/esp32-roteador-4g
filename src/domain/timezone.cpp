#include "timezone.h"

#include <cstring>

namespace {

// A ordem e a do <select>: Brasilia primeiro porque e o fuso da maioria das unidades, e a
// primeira entrada tambem e o default de fabrica.
const TimezoneOption kOptions[] = {
    {"<-03>3", "Brasília (UTC-3)"},
    {"<-04>4", "Manaus (UTC-4)"},
    {"<-05>5", "Acre (UTC-5)"},
    {"<-02>2", "Fernando de Noronha (UTC-2)"},
};

const size_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);

}  // namespace

const char* const kDefaultTimezone = kOptions[0].posix;

const TimezoneOption* timezoneOptions() {
  return kOptions;
}

size_t timezoneOptionCount() {
  return kOptionCount;
}

const char* timezoneLabel(const String& posix) {
  for (size_t i = 0; i < kOptionCount; ++i) {
    if (posix == kOptions[i].posix) return kOptions[i].label;
  }
  return nullptr;
}

bool isKnownTimezone(const String& posix) {
  return timezoneLabel(posix) != nullptr;
}
