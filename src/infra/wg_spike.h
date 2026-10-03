#pragma once

#include <esp_netif.h>

// SPIKE (tmp/spike-wireguard) — descartavel, nao mergear. Prova esp_wireguard junto com PPP e
// NAT na bancada antes de desenhar a Fase 10 (PRD 13, risco numero um).
namespace WgSpike {
// Chamar do loop(). Sobe o tunel uma vez, quando uplink online e relogio sincronizado
// acontecem juntos, sobre a netif PPP `uplink`, e imprime uma linha `WG:` a cada 30 s.
// Spike: nao refaz o tunel quando o PPP e recriado (o fork exige disconnect antes).
void tick(bool uplinkOnline, bool clockSynced, esp_netif_t* uplink);
}
