// Arduino.h vem antes do proprio cabecalho de proposito: o IPAddress.h do core declara
// `extern IPAddress INADDR_NONE`, e se o lwip/inet.h entrar primeiro esse nome ja virou a
// macro ((u32_t)0xffffffffUL) e a declaracao nao compila.
#include <Arduino.h>

#include "dns_forwarder.h"

#include <lwip/dns.h>

#include <cstring>

#include "../domain/dns_message.h"

namespace {

const uint16_t kDnsPort = 53;
// Cinco segundos e o que os resolvedores de sistema costumam esperar antes de retransmitir;
// segurar o slot por mais tempo so ocupa a tabela com pergunta que o cliente ja desistiu.
const uint32_t kPendingTimeoutMs = 5000;
const uint32_t kSelectTimeoutSec = 1;

uint32_t nowMs() {
  return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
}

}  // namespace

void DnsForwarder::closeSockets() {
  if (clientSocket_ >= 0) close(clientSocket_);
  if (upstreamSocket_ >= 0) close(upstreamSocket_);
  clientSocket_ = -1;
  upstreamSocket_ = -1;
}

bool DnsForwarder::begin(uint32_t listenIp) {
  if (task_ != nullptr) return true;
  listenIp_ = listenIp;

  clientSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  upstreamSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (clientSocket_ < 0 || upstreamSocket_ < 0) {
    Serial.printf("DNS: socket() falhou (errno %d)\n", errno);
    closeSockets();
    return false;
  }

  // Bind explicito no IP do AP, nunca INADDR_ANY: com INADDR_ANY o mesmo socket atenderia
  // a interface PPP, e o roteador viraria resolvedor aberto para a rede da operadora.
  sockaddr_in listenAddress = {};
  listenAddress.sin_family = AF_INET;
  listenAddress.sin_port = htons(kDnsPort);
  listenAddress.sin_addr.s_addr = listenIp_;
  if (bind(clientSocket_, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) <
      0) {
    Serial.printf("DNS: bind em %s:53 falhou (errno %d)\n",
                  IPAddress(listenIp_).toString().c_str(), errno);
    closeSockets();
    return false;
  }

  if (xTaskCreate(&DnsForwarder::taskEntry, "dns_fwd", 3072, this, 5, &task_) != pdPASS) {
    Serial.println("DNS: nao foi possivel criar a task do forwarder");
    task_ = nullptr;
    closeSockets();
    return false;
  }
  return true;
}

void DnsForwarder::taskEntry(void* context) {
  static_cast<DnsForwarder*>(context)->run();
}

void DnsForwarder::run() {
  for (;;) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(clientSocket_, &readable);
    FD_SET(upstreamSocket_, &readable);
    int highest = clientSocket_ > upstreamSocket_ ? clientSocket_ : upstreamSocket_;

    timeval timeout = {};
    timeout.tv_sec = kSelectTimeoutSec;
    int ready = select(highest + 1, &readable, nullptr, nullptr, &timeout);
    if (ready > 0) {
      if (FD_ISSET(clientSocket_, &readable)) handleClientDatagram();
      if (FD_ISSET(upstreamSocket_, &readable)) handleUpstreamDatagram();
    }
    // Tambem no timeout do select: sem trafego nenhum, os slots precisam vencer do mesmo jeito.
    expirePending(nowMs());
  }
}

bool DnsForwarder::upstreamAddress(sockaddr_in& out) const {
  // Numa interface ponto-a-ponto o DNS vive no global do lwIP, escrito pelo PPP no IPCP —
  // e a mesma fonte que o esp_netif_get_dns_info() leria, sem precisar carregar o netif
  // do modem ate aqui. Reconexao que troque o servidor da operadora passa a valer na
  // proxima pergunta, sem ninguem avisar o forwarder.
  const ip_addr_t* upstream = dns_getserver(0);
  if (upstream == nullptr || !IP_IS_V4(upstream)) return false;

  uint32_t address = ip_2_ip4(upstream)->addr;
  if (address == IPADDR_ANY) return false;
  // Sem esta guarda, ficar sem uplink com o DNS apontando para nos mesmos viraria um laco
  // de pergunta e resposta entre o forwarder e ele proprio.
  if (address == listenIp_) return false;

  out = sockaddr_in{};
  out.sin_family = AF_INET;
  out.sin_port = htons(kDnsPort);
  out.sin_addr.s_addr = address;
  return true;
}

void DnsForwarder::handleClientDatagram() {
  sockaddr_in client = {};
  socklen_t clientLength = sizeof(client);
  int length = recvfrom(clientSocket_, datagram_, sizeof(datagram_), 0,
                        reinterpret_cast<sockaddr*>(&client), &clientLength);
  if (length <= 0) return;
  size_t queryLength = static_cast<size_t>(length);

  // Resposta chegando na porta 53 e lixo ou tentativa de envenenamento; nao vira pergunta.
  if (!dnsIsQuery(datagram_, queryLength)) return;

  sockaddr_in upstream = {};
  if (!upstreamAddress(upstream)) {
    // Sem uplink, SERVFAIL na hora. E mais util que silencio: o cliente para de esperar e
    // o navegador mostra erro de DNS em vez de travar ate o timeout.
    replyServfail(datagram_, queryLength, client);
    return;
  }

  size_t slot = kMaxPending;
  uint32_t now = nowMs();
  expirePending(now);
  for (size_t i = 0; i < kMaxPending; ++i) {
    if (!pending_[i].used) {
      slot = i;
      break;
    }
  }
  if (slot == kMaxPending) {
    replyServfail(datagram_, queryLength, client);
    return;
  }

  uint16_t clientId = 0;
  dnsReadTransactionId(datagram_, queryLength, clientId);

  // O ID que vai para o upstream e nosso, nao o do cliente: dois clientes podem escolher o
  // mesmo ID, e ai a resposta de um voltaria para o outro. O slot ocupa o byte alto, entao
  // dois pendentes nunca colidem.
  uint16_t outboundId = static_cast<uint16_t>((slot << 8) | (nextSequence_++ & 0xFF));
  datagram_[0] = static_cast<uint8_t>(outboundId >> 8);
  datagram_[1] = static_cast<uint8_t>(outboundId & 0xFF);

  int sent = sendto(upstreamSocket_, datagram_, queryLength, 0,
                    reinterpret_cast<sockaddr*>(&upstream), sizeof(upstream));
  if (sent < 0) {
    datagram_[0] = static_cast<uint8_t>(clientId >> 8);
    datagram_[1] = static_cast<uint8_t>(clientId & 0xFF);
    replyServfail(datagram_, queryLength, client);
    return;
  }

  pending_[slot].used = true;
  pending_[slot].clientId = clientId;
  pending_[slot].outboundId = outboundId;
  pending_[slot].client = client;
  pending_[slot].deadlineMs = now + kPendingTimeoutMs;
}

void DnsForwarder::handleUpstreamDatagram() {
  sockaddr_in from = {};
  socklen_t fromLength = sizeof(from);
  int length = recvfrom(upstreamSocket_, datagram_, sizeof(datagram_), 0,
                        reinterpret_cast<sockaddr*>(&from), &fromLength);
  if (length <= 0) return;
  size_t responseLength = static_cast<size_t>(length);

  uint16_t outboundId = 0;
  if (!dnsReadTransactionId(datagram_, responseLength, outboundId)) return;

  size_t slot = outboundId >> 8;
  if (slot >= kMaxPending) return;
  Pending& entry = pending_[slot];
  if (!entry.used || entry.outboundId != outboundId) return;

  // O socket upstream escuta numa porta efemera alcancavel a partir do AP: sem conferir a
  // origem, um cliente podia forjar a resposta de qualquer dominio para outro cliente.
  sockaddr_in expected = {};
  if (!upstreamAddress(expected)) return;
  if (from.sin_addr.s_addr != expected.sin_addr.s_addr || from.sin_port != expected.sin_port) {
    return;
  }

  datagram_[0] = static_cast<uint8_t>(entry.clientId >> 8);
  datagram_[1] = static_cast<uint8_t>(entry.clientId & 0xFF);
  sendto(clientSocket_, datagram_, responseLength, 0,
         reinterpret_cast<sockaddr*>(&entry.client), sizeof(entry.client));
  entry.used = false;
}

void DnsForwarder::expirePending(uint32_t now) {
  for (size_t i = 0; i < kMaxPending; ++i) {
    // Vence em silencio em vez de responder SERVFAIL: montar a resposta exigiria guardar a
    // pergunta inteira de cada pendente, e o resolvedor do cliente ja retransmite sozinho.
    if (pending_[i].used && static_cast<int32_t>(now - pending_[i].deadlineMs) >= 0) {
      pending_[i].used = false;
    }
  }
}

void DnsForwarder::replyServfail(const uint8_t* query, size_t length, const sockaddr_in& client) {
  size_t responseLength = dnsBuildServfail(query, length, response_, sizeof(response_));
  if (responseLength == 0) return;
  sendto(clientSocket_, response_, responseLength, 0,
         reinterpret_cast<const sockaddr*>(&client), sizeof(client));
}
