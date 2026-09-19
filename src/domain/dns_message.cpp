#include "dns_message.h"

namespace {

const uint8_t kFlagQueryResponse = 0x80;   // bit alto do byte 2
const uint8_t kFlagRecursionAvailable = 0x80;  // bit alto do byte 3
const uint8_t kRcodeMask = 0x0F;
const uint8_t kRcodeServerFailure = 0x02;
const uint8_t kLabelPointerMask = 0xC0;  // dois bits altos ligados = ponteiro (RFC 1035 4.1.4)
const size_t kQuestionTrailerLength = 4;  // QTYPE + QCLASS

// Devolve o offset logo depois da secao de pergunta, ou 0 se ela nao fecha dentro do
// datagrama. Percorre os labels sem interpretar nenhum: o forwarder nao precisa do nome,
// so precisa saber onde a pergunta termina para copiar ate ali.
size_t endOfQuestion(const uint8_t* datagram, size_t length) {
  size_t offset = kDnsHeaderLength;

  while (offset < length) {
    uint8_t label = datagram[offset];

    if ((label & kLabelPointerMask) != 0) {
      // Ponteiro de compressao numa pergunta nao tem para onde apontar — nao existe nome
      // anterior no datagrama. Aceitar abriria caminho para ponteiro para si mesmo, que
      // e laco infinito em quem tentar seguir.
      return 0;
    }

    ++offset;  // consome o byte de comprimento

    if (label == 0) {
      // Fim do nome. Sobram QTYPE e QCLASS, e eles tambem precisam caber.
      if (length - offset < kQuestionTrailerLength) {
        return 0;
      }
      return offset + kQuestionTrailerLength;
    }

    // O label promete `label` bytes: sem esta conferencia, um comprimento maior que o
    // datagrama faria a copia adiante ler memoria de outra pessoa.
    if (length - offset < label) {
      return 0;
    }
    offset += label;
  }

  return 0;
}

}  // namespace

bool dnsReadTransactionId(const uint8_t* datagram, size_t length, uint16_t& out) {
  if (datagram == nullptr || length < kDnsHeaderLength) {
    return false;
  }
  out = static_cast<uint16_t>((datagram[0] << 8) | datagram[1]);
  return true;
}

bool dnsIsQuery(const uint8_t* datagram, size_t length) {
  if (datagram == nullptr || length < kDnsHeaderLength) {
    return false;
  }
  if ((datagram[2] & kFlagQueryResponse) != 0) {
    return false;
  }
  uint16_t questions = static_cast<uint16_t>((datagram[4] << 8) | datagram[5]);
  return questions > 0;
}

size_t dnsBuildServfail(const uint8_t* query, size_t queryLength, uint8_t* out,
                        size_t outCapacity) {
  if (query == nullptr || out == nullptr || queryLength < kDnsHeaderLength) {
    return 0;
  }

  size_t end = endOfQuestion(query, queryLength);
  if (end == 0 || end > outCapacity) {
    return 0;
  }

  for (size_t i = 0; i < end; ++i) {
    out[i] = query[i];
  }

  // QR ligado, e o resto do byte 2 preservado: RD e do cliente e a resposta tem que
  // devolver o que ele pediu, senao ele trata como se tivesse falado com outro servidor.
  out[2] |= kFlagQueryResponse;
  // RA ligado: quem pergunta ao roteador esta pedindo recursao, e e recursao que o
  // forwarder faz quando ha uplink. RCODE substituido, o resto do byte descartado.
  out[3] = kFlagRecursionAvailable | (kRcodeServerFailure & kRcodeMask);

  // QDCOUNT fica como veio; as outras tres contagens vao a zero. Sem isso o cliente le
  // ANCOUNT do proprio pedido e procura registro que a resposta nao carrega.
  out[6] = 0;
  out[7] = 0;
  out[8] = 0;
  out[9] = 0;
  out[10] = 0;
  out[11] = 0;

  return end;
}
