#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <cstdint>

// INFRA — resolvedor que os clientes do AP enxergam em 192.168.4.1. Nao resolve nada
// sozinho: repassa a pergunta para o DNS que a operadora entregou no IPCP e devolve a
// resposta ao cliente.
//
// Existe por causa do endereco, nao do servico. O servidor DHCP do AP entrega o IP do
// proprio AP como DNS sempre que a opcao 6 nao esta configurada (dhcpserver.c, montagem
// das opcoes do OFFER), e ate aqui nao havia ninguem escutando nesse endereco — o
// NatBridge corrigia isso reescrevendo a opcao 6 com o DNS da operadora a cada sessao
// PPP. Com um resolvedor de verdade em 192.168.4.1, o endereco entregue no lease vale
// para sempre: cliente associado durante uma reconexao nao fica sem DNS ate renovar, e o
// DHCP nao precisa mais parar e subir a cada sessao (debitos 9 e 12).
class DnsForwarder {
 public:
  // `listenIp` e o IP do AP em network byte order. Cria os sockets e a task; nao bloqueia.
  bool begin(uint32_t listenIp);

 private:
  // Numero de perguntas em voo. 15 clientes navegando fazem rajadas curtas, nao 8
  // perguntas simultaneas sustentadas; com a tabela cheia a pergunta nova e recusada com
  // SERVFAIL, que o resolvedor do cliente retenta.
  static const size_t kMaxPending = 8;
  // MTU: resposta com EDNS0 passa fácil de 512 bytes, e o que nao couber no buffer o
  // recvfrom descarta em silencio.
  static const size_t kMaxDatagram = 1500;

  struct Pending {
    bool used = false;
    uint16_t clientId = 0;    // ID que o cliente escolheu
    uint16_t outboundId = 0;  // ID que mandamos para o upstream
    sockaddr_in client = {};
    uint32_t deadlineMs = 0;
  };

  static void taskEntry(void* context);
  void closeSockets();
  void run();
  void handleClientDatagram();
  void handleUpstreamDatagram();
  void expirePending(uint32_t now);
  void replyServfail(const uint8_t* query, size_t length, const sockaddr_in& client);
  bool upstreamAddress(sockaddr_in& out) const;

  uint32_t listenIp_ = 0;
  int clientSocket_ = -1;
  int upstreamSocket_ = -1;
  uint16_t nextSequence_ = 0;
  Pending pending_[kMaxPending];

  // Fora da pilha da task de proposito: 1500 bytes de buffer numa stack de 3 KB nao
  // sobram para mais nada.
  uint8_t datagram_[kMaxDatagram];
  uint8_t response_[512];

  TaskHandle_t task_ = nullptr;
};
