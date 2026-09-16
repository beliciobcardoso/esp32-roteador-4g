# AGENTS.md

Firmware roteador 4G — LilyGO T-A7670E R2 (ESP32-WROVER-E + modem A7670E).

## Stack

- PlatformIO, framework híbrido `espidf, arduino` (não é Arduino puro)
- Board declarado: `esp-wrover-kit` (mais próximo no catálogo — placa real é LilyGO, diferenças de flash são esperadas)
- C++ (ESP-IDF/Arduino), sem sistema de build CMake de terceiros além do gerado pelo PlatformIO

## Comandos

```bash
pio run                    # build
pio run --target upload    # flash (fecha o monitor serial antes)
pio device monitor         # monitor serial
```

Se `sdkconfig.defaults` mudar e não refletir:
```bash
rm -f sdkconfig.esp-wrover-kit && rm -rf .pio && pio run
```

Ver [docs/SETUP.md](docs/SETUP.md) pra setup de ambiente do zero.

## Arquitetura alvo

Clean Architecture — ver [docs/PLANO_ROTEADOR.md](docs/PLANO_ROTEADOR.md) pra estrutura completa de pastas e fases de implementação. Resumo da regra de dependência:

- `domain/` — regras puras, não importa `infra` nem `adapters`
- `usecases/` — depende só de interfaces (`adapters/*_repository.h`), nunca de implementação concreta
- `adapters/` — implementam as interfaces, fazem parsing HTTP/HTML
- `infra/` — wrappers finos sobre APIs ESP-IDF/Arduino (WiFi AP, PPP, NAT)
- `main.cpp` — só orquestração/injeção, zero lógica de negócio

**Estado atual:** só protótipo de leitura de bateria em `src/main.cpp`. Nenhuma fase do plano (NVS, WiFi AP, HTTP config, PPP, NAT) foi implementada ainda.

## Decisões já fechadas (não reabrir sem motivo)

- PPP via `esp_modem` (componente oficial ESP-IDF), não TinyGSM — TinyGSM não expõe interface IP roteável
- Config web: HTTP Basic Auth, IP fixo (sem portal cativo)
- Persistência: NVS sem criptografia
- Até 20 clientes WiFi simultâneos, WPA2/WPA3 misto (`WIFI_AUTH_WPA2_WPA3_PSK`)

## Hardware — cuidados obrigatórios

- `BOARD_POWERON_PIN` (GPIO12) tem que ir `HIGH` no `setup()` — sem isso a placa desliga sozinha rodando só na bateria
- `VOLTAGE_DIVIDER_RATIO` em `main.cpp` é calibrado por multímetro numa placa específica — não é universal (ver [docs/DEBITOS_TECNICOS.md](docs/DEBITOS_TECNICOS.md))
- Só um processo por vez na porta serial — upload falha com `Device or resource busy` se o monitor estiver aberto

## Débitos técnicos conhecidos

Ver [docs/DEBITOS_TECNICOS.md](docs/DEBITOS_TECNICOS.md).

## Regras de código

- Migration destrutiva/irreversível: sinalizar antes (não aplicável ainda — sem NVS implementado)
- Toda rota HTTP sensível (config): Basic Auth obrigatório, erro sem vazar detalhe interno
- Seguir padrão do arquivo existente ao editar, não impor estilo novo
- Não criar abstração antes de duas ocorrências reais a justificarem
