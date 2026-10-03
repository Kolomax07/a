#include "cultivo_calibracion.h"

#include <cmath>

namespace cultivo {

namespace {
constexpr float COEF_TEMPERATURA_EC = 0.02f;  // la EC sube ~2 % por °C
}

float humedadSuelo(float voltaje, const CalibracionSondas& cal) {
  float porcentaje = 100 * (cal.sueloVSeco - voltaje) / (cal.sueloVSeco - cal.sueloVMojado);
  return std::fmin(100.0f, std::fmax(0.0f, porcentaje));
}

float ph(float voltaje, const CalibracionSondas& cal) {
  float pendiente = (7.0f - 4.0f) / (cal.phV7 - cal.phV4);
  return 7.0f + (voltaje - cal.phV7) * pendiente;
}

float ec(float voltaje, const CalibracionSondas& cal, float temperatura) {
  float medida = 1.413f * (voltaje - cal.ecV0) / (cal.ecV1413 - cal.ecV0);
  if (!std::isnan(temperatura)) medida /= 1 + COEF_TEMPERATURA_EC * (temperatura - 25.0f);
  return std::fmax(0.0f, medida);
}

float vpd(float temperatura, float humedadRelativa) {
  if (std::isnan(temperatura) || std::isnan(humedadRelativa)) return NAN;
  float presionSaturacion = 0.6108f * std::exp(17.27f * temperatura / (temperatura + 237.3f));
  return presionSaturacion * (1 - humedadRelativa / 100);
}

}  // namespace cultivo
