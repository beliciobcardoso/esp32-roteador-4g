#pragma once
// SPIKE (tmp/spike-wireguard) — copiar para wg_spike_secrets.h, que o git ignora, e preencher.
// A chave privada da placa NUNCA entra em arquivo versionado.
#define WG_SPIKE_PRIVATE_KEY      "base64 da chave privada da placa (wg genkey)"
#define WG_SPIKE_PEER_PUBLIC_KEY  "base64 da chave publica do servidor"
#define WG_SPIKE_ADDRESS          "10.8.0.2"       // IP da placa dentro do tunel
#define WG_SPIKE_NETMASK          "255.255.252.0"
#define WG_SPIKE_ENDPOINT         "168.138.238.120"
#define WG_SPIKE_PORT             51820
