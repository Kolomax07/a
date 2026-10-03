// Conversión de voltajes de las sondas analógicas a unidades físicas.
#pragma once

namespace cultivo {

struct CalibracionSondas {
  // Sensor capacitivo de humedad del sustrato: voltaje en seco (aire) y en agua.
  float sueloVSeco = 2.80f;
  float sueloVMojado = 1.30f;
  // Sonda de pH: voltaje en las soluciones tampón de pH 7 y pH 4.
  float phV7 = 1.50f;
  float phV4 = 2.03f;
  // Sonda de EC: voltaje en agua destilada y en la solución patrón de 1,413 mS/cm.
  float ecV0 = 0.0f;
  float ecV1413 = 1.0f;
};

// Más agua = menos voltaje. Devuelve 0–100 %.
float humedadSuelo(float voltaje, const CalibracionSondas& cal);
// Recta por los dos puntos de calibración (pH 7 y pH 4).
float ph(float voltaje, const CalibracionSondas& cal);
// mS/cm compensada a 25 °C (si temperatura es NAN no se compensa).
float ec(float voltaje, const CalibracionSondas& cal, float temperatura);
// Déficit de presión de vapor del aire en kPa (fórmula de Tetens). NAN si falta un dato.
float vpd(float temperatura, float humedadRelativa);

}  // namespace cultivo
