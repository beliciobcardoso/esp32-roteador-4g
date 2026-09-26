#include "nat_bridge.h"

#include <Arduino.h>
#include <lwip/lwip_napt.h>

#include "timestamped_serial.h"

namespace {

// Chave do esp_netif que o core Arduino cria quando o SoftAP sobe. Buscar o handle
// por aqui evita ter que propagar o ponteiro desde o WifiAp, que nao o expoe.
constexpr const char* kApNetifKey = "WIFI_AP_DEF";

// Roda no contexto da tarefa TCP/IP (via esp_netif_tcpip_exec). ip_napt_enable
// percorre netif_list e escreve em netif->napt — estrutura que so a tarefa lwIP
// pode tocar com seguranca.
esp_err_t enableNaptInTcpipContext(void* ctx) {
  ip_napt_enable(*static_cast<uint32_t*>(ctx), 1);
  return ESP_OK;
}

// Contraintuitivo, entao vale registrar: a flag `napt` marca a interface INTERNA,
// nao o uplink. O comentario do proprio lwIP em ip4.c:385 e explicito — "if the
// output netif uses NAPT, we will not perform NAPT forwarding". No caminho de saida
// (ip4.c:387) a traducao acontece quando o netif de SAIDA nao tem a flag; no caminho
// de entrada (ip4.c:599) a destraducao acontece quando o netif de ENTRADA nao tem.
// Ligar no PPP inverteria os dois sentidos e nada sairia da placa.
bool enableNaptOnAp(const esp_netif_ip_info_t& apIp) {
  uint32_t apAddress = apIp.ip.addr;
  esp_err_t result = esp_netif_tcpip_exec(&enableNaptInTcpipContext, &apAddress);
  if (result != ESP_OK) {
    logSerial.printf("NAT: ip_napt_enable falhou [%s]\n", esp_err_to_name(result));
    return false;
  }
  return true;
}

}  // namespace

bool NatBridge::enable(esp_netif_t* uplink) {
  if (uplink == nullptr) {
    logSerial.println("NAT: uplink PPP nulo — o modem nao chegou a inicializar");
    return false;
  }

  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey(kApNetifKey);
  if (apNetif == nullptr) {
    logSerial.printf("NAT: interface \"%s\" nao existe — o SoftAP nao subiu\n", kApNetifKey);
    return false;
  }

  esp_netif_ip_info_t apIp = {};
  esp_err_t ipResult = esp_netif_get_ip_info(apNetif, &apIp);
  if (ipResult != ESP_OK) {
    logSerial.printf("NAT: nao consegui ler o IP do AP [%s]\n", esp_err_to_name(ipResult));
    return false;
  }

  if (!enableNaptOnAp(apIp)) {
    return false;
  }

  // Aqui nao se mexe mais no DHCP do AP. O que os clientes recebem na opcao 6 e o IP do
  // proprio AP — o dhcpserver preenche assim quando a opcao nao esta configurada
  // (dhcpserver.c:383-395) — e quem responde nesse endereco e o DnsForwarder, que le o
  // DNS da operadora direto do global do lwIP a cada pergunta. Enquanto o DHCP do AP era
  // reconfigurado a cada sessao PPP, todo cliente associado durante a janela de
  // dhcps_stop/start ficava sem endereco, e quem ja tinha lease seguia apontando para um
  // DNS que podia ter mudado ate renovar (debitos 9 e 12).
  esp_netif_dns_info_t dns = {};
  esp_err_t dnsResult = esp_netif_get_dns_info(uplink, ESP_NETIF_DNS_MAIN, &dns);
  if (dnsResult != ESP_OK) {
    // Nao e mais fatal: o NAT roteia IP do mesmo jeito, e o forwarder responde SERVFAIL
    // enquanto nao houver DNS — falha explicita em vez de sessao que parece ok e nao e.
    logSerial.printf("NAT: ativo | AP " IPSTR " -> PPP | a operadora nao informou DNS [%s]\n",
                  IP2STR(&apIp.ip), esp_err_to_name(dnsResult));
    return true;
  }

  // A rota default nao precisa de chamada nenhuma: o PPP tem route_prio 20 contra 10
  // do SoftAP (esp_netif_defaults.h), e o esp_netif_update_default_netif() promove o
  // PPP a interface default sozinho quando o GOT_IP chega. esp_netif_set_default_netif()
  // e static em esp_netif_lwip.c:202 — nem da pra chamar de fora.
  logSerial.printf("NAT: ativo | AP " IPSTR " -> PPP | DNS da operadora: " IPSTR "\n",
                IP2STR(&apIp.ip), IP2STR(&dns.ip.u_addr.ip4));
  return true;
}
