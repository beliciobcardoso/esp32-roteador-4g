#include "nat_bridge.h"

#include <Arduino.h>
#include <lwip/lwip_napt.h>

namespace {

// Chave do esp_netif que o core Arduino cria quando o SoftAP sobe. Buscar o handle
// por aqui evita ter que propagar o ponteiro desde o WifiAp, que nao o expoe.
constexpr const char* kApNetifKey = "WIFI_AP_DEF";

// Valor da opcao 6 do DHCP no esp_netif: 1 liga OFFER_DNS, 0 desliga.
constexpr uint8_t kDhcpsOfferDns = 1;

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
    Serial.printf("NAT: ip_napt_enable falhou [%s]\n", esp_err_to_name(result));
    return false;
  }
  return true;
}

// Sem isso o dhcpserver ainda emite a opcao 6, mas preenchida com o IP do proprio AP
// (dhcpserver.c:383-395) — e nao existe resolvedor escutando em 192.168.4.1. O cliente
// pegaria IP, rotearia por NAT e mesmo assim nao resolveria nome nenhum.
// A ordem importa: esp_netif_dhcps_option recusa com ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED
// enquanto o servidor estiver de pe (esp_netif_lwip.c:1929), entao para, reconfigura e sobe.
bool offerDnsToApClients(esp_netif_t* apNetif, const esp_netif_dns_info_t& dns) {
  esp_err_t stopResult = esp_netif_dhcps_stop(apNetif);
  if (stopResult != ESP_OK && stopResult != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    Serial.printf("NAT: nao consegui parar o DHCP do AP [%s]\n", esp_err_to_name(stopResult));
    return false;
  }

  // Num netif com flag DHCP_SERVER, esp_netif_set_dns_info cai em dhcps_dns_setserver()
  // (esp_netif_lwip.c:1585) — e o servidor DHCP que guarda o endereco, nao o resolvedor
  // local. So o tipo MAIN e propagado; DNS secundario nao tem representacao no dhcpserver.
  esp_netif_dns_info_t mutableDns = dns;
  esp_err_t dnsResult = esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &mutableDns);
  if (dnsResult != ESP_OK) {
    Serial.printf("NAT: nao consegui gravar o DNS no DHCP do AP [%s]\n", esp_err_to_name(dnsResult));
    return false;
  }

  uint8_t offerDns = kDhcpsOfferDns;
  esp_err_t optionResult = esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET,
                                                  ESP_NETIF_DOMAIN_NAME_SERVER,
                                                  &offerDns, sizeof(offerDns));
  if (optionResult != ESP_OK) {
    Serial.printf("NAT: nao consegui ligar a opcao 6 do DHCP [%s]\n", esp_err_to_name(optionResult));
    return false;
  }

  esp_err_t startResult = esp_netif_dhcps_start(apNetif);
  if (startResult != ESP_OK) {
    // Estado ruim de verdade: o AP fica no ar sem distribuir endereco nenhum.
    Serial.printf("NAT: DHCP do AP nao voltou a subir [%s]\n", esp_err_to_name(startResult));
    return false;
  }

  return true;
}

}  // namespace

bool NatBridge::enable(esp_netif_t* uplink) {
  if (uplink == nullptr) {
    Serial.println("NAT: uplink PPP nulo — o modem nao chegou a inicializar");
    return false;
  }

  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey(kApNetifKey);
  if (apNetif == nullptr) {
    Serial.printf("NAT: interface \"%s\" nao existe — o SoftAP nao subiu\n", kApNetifKey);
    return false;
  }

  // O PPP e point-to-point, entao esp_netif_get_dns_info nem consulta o netif: le o
  // dns_getserver() global do lwIP, que o proprio PPP preencheu no IPCP. Devolve
  // ESP_ERR_ESP_NETIF_DNS_NOT_CONFIGURED se a operadora nao mandou nada.
  esp_netif_dns_info_t dns = {};
  esp_err_t dnsResult = esp_netif_get_dns_info(uplink, ESP_NETIF_DNS_MAIN, &dns);
  if (dnsResult != ESP_OK) {
    Serial.printf("NAT: a operadora nao informou DNS [%s]\n", esp_err_to_name(dnsResult));
    return false;
  }

  esp_netif_ip_info_t apIp = {};
  esp_err_t ipResult = esp_netif_get_ip_info(apNetif, &apIp);
  if (ipResult != ESP_OK) {
    Serial.printf("NAT: nao consegui ler o IP do AP [%s]\n", esp_err_to_name(ipResult));
    return false;
  }

  if (!offerDnsToApClients(apNetif, dns)) {
    return false;
  }

  if (!enableNaptOnAp(apIp)) {
    return false;
  }

  // A rota default nao precisa de chamada nenhuma: o PPP tem route_prio 20 contra 10
  // do SoftAP (esp_netif_defaults.h), e o esp_netif_update_default_netif() promove o
  // PPP a interface default sozinho quando o GOT_IP chega. esp_netif_set_default_netif()
  // e static em esp_netif_lwip.c:202 — nem da pra chamar de fora.
  Serial.printf("NAT: ativo | AP " IPSTR " -> PPP | DNS entregue aos clientes: " IPSTR "\n",
                IP2STR(&apIp.ip), IP2STR(&dns.ip.u_addr.ip4));
  return true;
}
