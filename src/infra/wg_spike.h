#pragma once

#include <esp_netif.h>

// SPIKE (tmp/spike-wireguard) — descartavel, nao mergear. Prova esp_wireguard junto com PPP e
// NAT na bancada antes de desenhar a Fase 10 (PRD 13, risco numero um).
namespace WgSpike {
// Chamar do loop(). Sobe o tunel uma vez, quando uplink online e relogio sincronizado
// acontecem juntos, sobre a netif PPP `uplink`, e imprime uma linha `WG:` a cada 30 s.
void tick(bool uplinkOnline, bool clockSynced, esp_netif_t* uplink);

// Soltar e religar a netif de saida a cada sessao PPP (FORK.md, ponto 4). Os dois rodam na task
// do LinkSupervisor — detach pelo ModemPpp::onBeforeStop, attach pelo onUplinkOnline —, entao
// nao correm entre si. Antes do primeiro tick que sobe o tunel, os dois nao fazem nada.
void detachUplink();
void attachUplink(esp_netif_t* uplink);
}
