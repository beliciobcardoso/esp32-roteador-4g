#pragma once

#include <cstddef>
#include <cstdint>

// DOMINIO — o pouco do formato DNS que o roteador precisa entender para repassar consulta
// e para recusar quando nao ha para onde repassar. Nao e um parser de DNS: o forwarder
// nao le nome nenhum, so o cabecalho e o fim da pergunta.
//
// Mora aqui, e nao em infra/, porque e onde ha decisao errada possivel com consequencia
// silenciosa — um limite mal conferido le fora do buffer na placa e nada no comportamento
// denuncia. Aqui tem teste nativo com entrada hostil.

// Cabecalho DNS: 12 bytes fixos (RFC 1035 secao 4.1.1).
const size_t kDnsHeaderLength = 12;

// Le o ID da transacao (bytes 0-1). False quando o datagrama nem tem cabecalho.
bool dnsReadTransactionId(const uint8_t* datagram, size_t length, uint16_t& out);

// True so para pergunta: QR desligado e pelo menos uma entrada em QDCOUNT. Resposta
// encaminhada como pergunta transformaria o forwarder em refletor de trafego.
bool dnsIsQuery(const uint8_t* datagram, size_t length);

// Monta em `out` a resposta SERVFAIL para `query`, preservando ID e secao de pergunta.
// Devolve o tamanho escrito, ou 0 se a pergunta for malformada ou nao couber.
size_t dnsBuildServfail(const uint8_t* query, size_t queryLength, uint8_t* out,
                        size_t outCapacity);
