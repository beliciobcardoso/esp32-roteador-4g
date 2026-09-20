# PRD 10 — Migração de Schema da NVS

Fonte: metade em aberto do débito 1 em [DEBITOS_TECNICOS.md](../DEBITOS_TECNICOS.md), e
pré-requisito da Fase 7 em [PLANO_ROTEADOR.md](../PLANO_ROTEADOR.md).

## Objetivo

Tornar o `kCurrentSchema` uma coisa que se pode subir. Até aqui, acrescentar um campo à
configuração persistida custava a configuração inteira de toda unidade já instalada.

## Por que virou urgente

O corte por versão era simples: `load()` tratava `schema < kCurrentSchema` como registro
ausente, e o dispositivo caía nos defaults de fábrica. Destrutivo, mas com um custo
compreensível enquanto o default de fábrica era uma senha conhecida — a pessoa
reconfigurava e seguia.

O Lote F (débito 10) tirou a senha de fábrica do firmware. Agora o provisionamento decide
sortear exatamente por esse `load()` falhar:

```cpp
if (repository_.load(result.settings)) {
  result.persisted = true;
  return result;        // unidade ja provisionada, nao sorteia
}
// ... sorteia senha de AP e de admin
```

Ou seja, depois do Lote F subir o schema não descarta só a configuração: **re-sorteia a
senha do AP e a do admin**, e a nova só aparece no serial. Uma unidade em campo atualizada
por OTA derrubaria todos os clientes associados e ninguém saberia a senha nova. O que era
"perde a config" virou "perde a config e fica inacessível".

Isso não foi notado na hora porque os dois mecanismos foram escritos em lotes diferentes e
cada um está correto sozinho.

## Desenho

A política mora no domínio (`src/domain/settings_migration.h/.cpp`), não no adaptador, por
dois motivos. Testabilidade: `pio test -e native` só compila `src/domain/`, então política
no adaptador não tem onde ser testada. E porque é regra — decidir que uma unidade em campo
não pode perder o SSID por causa de um campo novo não depende de a persistência ser NVS.

```cpp
enum class SchemaVerdict { Rejected, Migrated, Current };

SchemaVerdict migrateSettings(int storedSchema, const MigrationDefaults& defaults,
                              RouterSettings& settings);
```

O adaptador lê todas as chaves numa `RouterSettings` temporária e entrega ao domínio junto
com o schema lido. Temporária e não o `out` do chamador: registro recusado não pode deixar
quem chamou com meia configuração, porque `load()` falso significa "não há registro".

`MigrationDefaults` chega por parâmetro porque `include/config.h` é configuração de build e
o domínio não depende dela.

### O degrau 1 → 2

O schema 1 não tinha `apn_user`/`apn_pass` e gravava `internet` como APN default, que não
conecta na rede onde estas placas rodam. Era por causa desse APN que o registro inteiro era
descartado.

Guardar o valor antigo permite o contrário: se o APN gravado ainda é `internet`, a pessoa
nunca escolheu um, e repor o default atual é o que faz a unidade voltar a conectar. Se é
outro valor, foi escolha dela e fica. SSID, senha do AP e credenciais de admin passam
intactos nos dois casos.

Quando o APN é preservado, `apn_user` e `apn_password` ficam vazios — inventar o usuário e
a senha da Vivo para o APN de outra operadora seria criar dado que ninguém forneceu, e APN
sem autenticação é caso legítimo.

### Downgrade de firmware

Registro com schema **maior** que o atual é lido como está. Recusar seria coerente — esta
versão não sabe o que os campos novos significam — mas na prática pior: faria o
provisionamento sortear senha nova para proteger de um campo extra que não atrapalha. Os
campos que esta versão conhece continuam onde sempre estiveram.

### A migração não regrava

Acontece em memória e o `load()` não escreve nada. `load()` com efeito colateral surpreende,
e a consolidação vem de graça no primeiro `save()`, que sempre grava o schema atual. Até lá
a unidade migra a cada boot, o que não custa nada porque `migrateSettings()` é puro e
idempotente.

A consequência aceita: uma unidade migrada que nunca é salva pela página fica
indefinidamente com o registro no formato antigo. Isso é seguro — o degrau continua sendo
aplicado — mas significa que o número gravado não indica o formato em uso.

## Fora de escopo

- **Transação.** `save()` continua sem atomicidade: a `Preferences` faz `nvs_commit` por
  chave e não oferece transação, então reboot no meio de uma gravação ainda mistura campos
  novos e velhos. `configured` e `schema` por último é o que existe hoje, e continua.
- **Degrau 2 → 3.** Pertence à Fase 7, junto com o campo que vai justificá-lo.
- **Downgrade com perda de campo.** Salvar por cima de um registro mais novo regrava o
  schema menor e os campos que esta versão não conhece ficam órfãos na NVS.

## Critérios de aceite

- [x] Registro de schema 1 é migrado, não descartado — SSID, senha do AP e credenciais de
      admin sobrevivem
- [x] APN default antigo (`internet`) vira o default atual; APN escolhido pela pessoa é
      preservado
- [x] Schema 0 ou negativo é recusado e não deixa configuração parcial no chamador
- [x] Registro de firmware mais novo é lido em vez de recusado
- [x] Migrar não liga `admin_password_pending`
- [x] `pio test -e native` cobre os degraus e as recusas — 10 testes em
      `test/test_settings_migration/`
- [ ] Unidade real com registro de schema 1 sobe preservando SSID e senha (sem placa nesse
      estado hoje; validar quando aparecer uma, ou forjando o registro pela NVS)
