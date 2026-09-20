#pragma once

#include <cstddef>

#include "../domain/firmware_update.h"

// Interface abstrata — o handler HTTP depende disso, nunca do `Update` ou do esp_ota
// direto. Mesmo motivo do settings_repository.h: gravar firmware e detalhe de infra, e o
// adaptador so precisa saber que existe um lugar para onde mandar os blocos e alguem que
// saiba dizer o que esta rodando hoje.
//
// Confirmar a imagem nao esta aqui de proposito: quem confirma e o loop(), depois da
// janela de verificacao, e a pagina nunca decide isso.
class FirmwareWriter {
 public:
  virtual ~FirmwareWriter() = default;

  virtual bool begin() = 0;
  virtual bool write(unsigned char* data, size_t length) = 0;
  virtual bool finish() = 0;
  virtual void abort() = 0;

  // Erro da camada de baixo, para o log. A resposta HTTP usa as frases do dominio.
  virtual String lastErrorText() const = 0;

  // Teto para o inspectImageSize(). Zero significa que nao deu para consultar a particao.
  virtual size_t targetSlotSize() const = 0;

  virtual String runningSlotLabel() const = 0;
  virtual String runningVersionText() const = 0;
  virtual FirmwareImageState runningImageState() const = 0;
};
