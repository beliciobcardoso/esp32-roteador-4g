# Fork local do esp_wireguard

Base: `trombik/esp_wireguard` **0.9.0** (ESP-IDF Component Registry), copiado de
`managed_components/` em 03/10/2026. Substitui a dependência do registry: o componente saiu do
`src/idf_component.yml`, e este diretório é a única cópia que o build enxerga.

O código de criptografia (`src/crypto*`, `src/nacl`, `src/wireguard.c`) não foi tocado. As
mudanças estão só na cola com o lwIP, marcadas com `FORK (esp32-roteador-4g)` no código:

1. **Interface de saída vem do chamador.** O upstream ligava o socket UDP à netif
   `WIFI_STA_DEF`, fixa no código. Nesta placa o uplink é PPP; a STA existe (o core Arduino a
   cria) mas nunca associa, e todo handshake era enviado nela e perdido sem erro. Agora
   `esp_wireguard_connect()` recebe a netif do uplink e a passa em `bind_netif`
   (`wireguardif.c`, `esp_wireguard.c`).
2. **Resolução separada da conexão.** O upstream fazia `getaddrinfo()` e as chamadas cruas
   do lwIP na mesma função. Com `CONFIG_LWIP_TCPIP_CORE_LOCKING` uma exige estar fora do lock
   e a outra dentro — deadlock medido em placa em 29/09/2026. Agora são
   `esp_wireguard_resolve()` (fora do lock) e `esp_wireguard_connect()` (dentro).
3. **`esp_wireguard_disconnect()` não restaura mais a rota padrão.** O upstream guardava o
   `netif_default` no init e o restaurava no disconnect; como o PPP é recriado a cada sessão,
   o ponteiro guardado vira memória liberada.

4. **Troca da netif de saída sem derrubar o túnel.** O device guarda ponteiro cru da netif do
   uplink e chama a função de saída dela em todo envio, inclusive no keepalive do timer. O PPP é
   destruído e recriado a cada sessão, e o envio pela netif liberada saltava para uma função
   nula (`InstrFetchProhibited`, PC `0x0`, medido em placa em 03/10/2026, duas vezes, a cada
   queda do PPP). `esp_wireguard_set_uplink()` solta (NULL) e religa a netif, mantendo chaves,
   peer e sessão; sem netif, envio volta `ERR_RTE` (`wireguardif.c`, `esp_wireguard.c`).

O contrato de lock de cada função está no topo de `include/esp_wireguard.h`.

Ao atualizar a partir do upstream: refazer os quatro pontos acima e conferir se alguma função
nova chama API crua do lwIP ou de socket.
