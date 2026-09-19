#include "battery.h"

namespace {

struct BatteryPoint {
  float voltage;
  int percent;
};

// Curva de descarga Li-ion 1S — nao-linear, por isso tabela e nao formula. Em ordem
// decrescente de tensao; `voltageToPercent` depende dessa ordem.
constexpr BatteryPoint kCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80}, {3.98f, 75},
    {3.95f, 70},  {3.91f, 65}, {3.87f, 60}, {3.85f, 55}, {3.84f, 50}, {3.82f, 45},
    {3.80f, 40},  {3.79f, 35}, {3.77f, 30}, {3.75f, 25}, {3.73f, 20}, {3.71f, 15},
    {3.69f, 10},  {3.61f, 5},  {3.27f, 0},
};

constexpr int kCurveSize = sizeof(kCurve) / sizeof(kCurve[0]);

}  // namespace

int voltageToPercent(float voltage) {
  if (voltage >= kCurve[0].voltage) return kCurve[0].percent;
  if (voltage <= kCurve[kCurveSize - 1].voltage) return kCurve[kCurveSize - 1].percent;

  for (int i = 0; i < kCurveSize - 1; ++i) {
    const float upperVoltage = kCurve[i].voltage;
    const float lowerVoltage = kCurve[i + 1].voltage;
    if (voltage > upperVoltage || voltage < lowerVoltage) continue;

    const int upperPercent = kCurve[i].percent;
    const int lowerPercent = kCurve[i + 1].percent;
    const float fraction = (voltage - lowerVoltage) / (upperVoltage - lowerVoltage);

    return lowerPercent + fraction * (upperPercent - lowerPercent);
  }

  // Inalcancavel: os dois saturadores acima cobrem fora da curva, e a tabela nao tem
  // buraco entre faixas consecutivas. Fica como rede — sem isso um ponto fora de ordem
  // numa recalibracao futura cairia em retorno indefinido.
  return 0;
}
