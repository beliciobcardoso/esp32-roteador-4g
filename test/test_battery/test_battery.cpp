#include <unity.h>

#include "domain/battery.h"

// A curva vive em battery.cpp e nao e exposta de proposito — o teste amarra o
// comportamento observavel, nao a tabela. Os pontos usados aqui sao os das pontas e um do
// meio; se a curva for recalibrada, estes testes falham e e isso mesmo que se quer: a
// mudanca passa a ser deliberada em vez de silenciosa.

void test_above_the_curve_saturates_at_full() {
  TEST_ASSERT_EQUAL_INT(100, voltageToPercent(4.50f));
}

void test_first_point_of_the_curve_is_full() {
  TEST_ASSERT_EQUAL_INT(100, voltageToPercent(4.20f));
}

void test_below_the_curve_saturates_at_empty() {
  TEST_ASSERT_EQUAL_INT(0, voltageToPercent(2.80f));
}

void test_last_point_of_the_curve_is_empty() {
  TEST_ASSERT_EQUAL_INT(0, voltageToPercent(3.27f));
}

// Tensao que cai exatamente num ponto tabelado tem que devolver o percentual daquele
// ponto, sem interpolacao arrastando o valor pro vizinho.
void test_exact_curve_point_returns_its_percent() {
  TEST_ASSERT_EQUAL_INT(50, voltageToPercent(3.84f));
  TEST_ASSERT_EQUAL_INT(75, voltageToPercent(3.98f));
}

// Entre 4.11 V (90%) e 4.15 V (95%), 4.1324 V da 92.8%. Truncar devolveria 92 — era o que
// o codigo antigo fazia, devolvendo float em funcao int sem aviso (debito 2). Arredondar
// devolve 93. O caso foi escolhido longe de .5 de proposito: exatamente no meio o
// resultado dependeria do erro do float e o teste ficaria instavel.
void test_interpolation_rounds_instead_of_truncating() {
  TEST_ASSERT_EQUAL_INT(93, voltageToPercent(4.1324f));
}

// Percentual nunca pode cair quando a tensao sobe. Pega troca de sinal na interpolacao,
// ponto fora de ordem na tabela e buraco entre faixas — que devolveria 0 no meio da curva.
void test_percent_never_decreases_as_voltage_rises() {
  int previous = -1;
  for (int millivolts = 2800; millivolts <= 4400; ++millivolts) {
    const int percent = voltageToPercent(millivolts / 1000.0f);
    TEST_ASSERT_TRUE_MESSAGE(percent >= previous, "percentual caiu com tensao maior");
    TEST_ASSERT_TRUE_MESSAGE(percent >= 0 && percent <= 100, "percentual fora de 0..100");
    previous = percent;
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_above_the_curve_saturates_at_full);
  RUN_TEST(test_first_point_of_the_curve_is_full);
  RUN_TEST(test_below_the_curve_saturates_at_empty);
  RUN_TEST(test_last_point_of_the_curve_is_empty);
  RUN_TEST(test_exact_curve_point_returns_its_percent);
  RUN_TEST(test_interpolation_rounds_instead_of_truncating);
  RUN_TEST(test_percent_never_decreases_as_voltage_rises);
  return UNITY_END();
}
