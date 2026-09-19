#include "entropy.h"

#include <bootloader_random.h>
#include <esp_random.h>

void fillRandomBytes(uint8_t* buffer, size_t length) {
  // esp_random() so devolve numero aleatorio de verdade com Wi-Fi ou Bluetooth ligado —
  // esta no contrato da funcao (esp_random.h). A senha do AP e necessaria PARA subir o AP,
  // entao esta chamada e obrigatoriamente anterior ao radio, justo na janela em que a RNG
  // e fraca. Sem o par abaixo o firmware geraria senha com cara de aleatoria e entropia
  // baixa, e nada no comportamento denunciaria: e defeito que nao aparece em teste.
  //
  // bootloader_random_enable() usa o SAR ADC como fonte de ruido enquanto o RF esta
  // desligado. O contrato exige desligar antes de inicializar RF, ADC ou I2S — por isso o
  // disable fecha a janela aqui dentro, e nao em quem chama: deixar ligado quebraria a
  // leitura de bateria e a subida do Wi-Fi.
  bootloader_random_enable();
  esp_fill_random(buffer, length);
  bootloader_random_disable();
}
