#include "modem_identity.h"

namespace {

bool isBlank(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Se a linha comeca com o rotulo, devolve o valor sem espacos nas pontas.
bool readField(const String& line, const char* label, String& value) {
  size_t labelLength = 0;
  while (label[labelLength] != '\0') labelLength++;
  if (line.length() < labelLength) return false;
  for (size_t i = 0; i < labelLength; i++) {
    if (line[i] != label[i]) return false;
  }

  size_t begin = labelLength;
  size_t end = line.length();
  while (begin < end && isBlank(line[begin])) begin++;
  while (end > begin && isBlank(line[end - 1])) end--;

  String result;
  for (size_t i = begin; i < end; i++) result += line[i];
  value = result;
  return true;
}

}  // namespace

ModemIdentity parseSimcomati(const String& response) {
  ModemIdentity identity;
  String line;

  // Uma passada a mais com '\n' fecha a ultima linha quando a resposta nao termina em quebra.
  for (size_t i = 0; i <= response.length(); i++) {
    const char c = i < response.length() ? response[i] : '\n';
    if (c != '\n') {
      line += c;
      continue;
    }
    String value;
    if (readField(line, "Model:", value)) {
      identity.model = value;
    } else if (readField(line, "Revision:", value)) {
      identity.revision = value;
    }
    line = String();
  }
  return identity;
}

bool modemHasGnss(const String& model) {
  const char* suffix = "-FASE";
  const size_t suffixLength = 5;
  if (model.length() < suffixLength) return false;
  const size_t start = model.length() - suffixLength;
  for (size_t i = 0; i < suffixLength; i++) {
    if (model[start + i] != suffix[i]) return false;
  }
  return true;
}

String describeModem(const ModemIdentity& identity) {
  if (identity.model.length() == 0) {
    return String("Modem: modelo nao identificado — AT+SIMCOMATI sem linha Model:");
  }
  String line = "Modem: " + identity.model;
  line += " | firmware ";
  line += identity.revision.length() > 0 ? identity.revision : String("?");
  line += modemHasGnss(identity.model) ? " | GNSS interno: sim" : " | GNSS interno: nao";
  return line;
}
