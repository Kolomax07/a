// Tipos compartidos por la lógica de control. Sin dependencias de Arduino:
// se compila igual en el ESP32 que en el PC para los tests.
#pragma once

#include <cmath>
#include <cstdint>

namespace cultivo {

enum class Actuador : uint8_t {
  Luz,
  BombaRiego,
  Humidificador,
  Deshumidificador,
  Extractor,
  DosificadorA,
  DosificadorB,
  PhUp,
  PhDown,
};
constexpr uint8_t NUM_ACTUADORES = 9;
const char* nombre(Actuador a);

enum class Etapa : uint8_t { Plantula, Vegetativo, Floracion, FloracionTardia, Lavado };
constexpr uint8_t NUM_ETAPAS = 5;
const char* nombre(Etapa e);

enum class Medio : uint8_t { Tierra, Coco, Hidro };

// Interruptor de nivel del depósito.
enum class Nivel : uint8_t { Desconocido, Bajo, Ok };

struct ObjetivosEtapa {
  float horasLuz;  // horas de luz por cada 24 h
  float hrMin;     // % humedad relativa mínima
  float hrMax;     // % humedad relativa máxima
  float tempMax;   // °C a partir de los cuales se enciende el extractor
  float ec;        // mS/cm objetivo del depósito (0 = no dosificar nutrientes)
};

struct RangoPh {
  float min;
  float max;
};

// Rango de pH de la solución de riego según el medio de cultivo.
constexpr RangoPh rangoPhPorDefecto(Medio medio) {
  return medio == Medio::Tierra ? RangoPh{6.2f, 6.8f}
       : medio == Medio::Coco   ? RangoPh{5.7f, 6.3f}
                                : RangoPh{5.6f, 6.2f};
}

// Una lectura de todos los sensores. NAN = sensor ausente o averiado.
struct Lecturas {
  uint64_t ms = 0;            // reloj monotónico (ms desde el arranque), para medir esperas
  int32_t dia = 0;            // cambia a medianoche: reinicia los límites diarios
  int16_t minutoDelDia = 0;   // 0–1439, hora local
  bool horaValida = true;     // false hasta sincronizar por NTP o RTC
  float temperatura = NAN;    // °C del aire
  float humedadRelativa = NAN;
  float humedadSuelo = NAN;   // % (0 = seco, 100 = saturado)
  float lux = NAN;
  float ec = NAN;             // mS/cm compensada a 25 °C
  float ph = NAN;
  Nivel deposito = Nivel::Desconocido;
};

// Alarmas como máscara de bits: cada controlador activa las suyas en cada ciclo.
using Alarmas = uint32_t;
namespace alarma {
constexpr Alarmas LAMPARA_NO_ENCIENDE = 1u << 0;
constexpr Alarmas FUGA_DE_LUZ = 1u << 1;
constexpr Alarmas SIN_HUMEDAD_RELATIVA = 1u << 2;
constexpr Alarmas SIN_TEMPERATURA = 1u << 3;
constexpr Alarmas TEMPERATURA_CRITICA = 1u << 4;
constexpr Alarmas SIN_HUMEDAD_SUSTRATO = 1u << 5;
constexpr Alarmas LIMITE_RIEGOS = 1u << 6;
constexpr Alarmas EC_NO_VALIDA = 1u << 7;
constexpr Alarmas PH_NO_VALIDO = 1u << 8;
constexpr Alarmas EC_ALTA = 1u << 9;
constexpr Alarmas LIMITE_NUTRIENTES = 1u << 10;
constexpr Alarmas LIMITE_PH = 1u << 11;
constexpr Alarmas DEPOSITO_BAJO = 1u << 12;
constexpr Alarmas HORA_NO_VALIDA = 1u << 13;
constexpr uint8_t TOTAL = 14;
}  // namespace alarma
const char* textoAlarma(uint8_t bit);

struct ConfigLuz {
  int16_t minutoEncendido = 6 * 60;           // inicio del periodo de luz (06:00)
  float umbralLux = 500;                      // por debajo se considera "oscuro"
  uint32_t toleranciaMs = 10 * 60 * 1000UL;   // retardo antes de avisar de lámpara fundida o fuga de luz
};

struct ConfigHumedad {
  float histeresis = 3;                       // puntos de % HR de banda muerta
  float histeresisTemp = 1;                   // °C de banda muerta para el extractor
  uint32_t tiempoMinimoCicloMs = 120 * 1000UL;  // protege compresores y relés
};

struct ConfigRiego {
  float humedadSueloMin = 35;                 // % por debajo del cual se riega
  uint32_t duracionMs = 20 * 1000UL;          // tiempo de bomba por riego
  uint32_t esperaAbsorcionMs = 45 * 60 * 1000UL;  // para que el agua se reparta antes de volver a medir
  uint8_t maxRiegosDia = 6;                   // límite de seguridad ante sensor o bomba averiados
  bool soloConLuz = true;                     // regar solo durante el periodo de luz
};

struct ConfigNutrientes {
  float toleranciaEc = 0.15f;                 // mS/cm por debajo del objetivo antes de dosificar
  float margenEcAlta = 0.4f;                  // mS/cm por encima del objetivo para avisar
  float mlPorDosis = 2.0f;                    // ml de cada parte (A y B) por dosis
  float mlPorDosisPh = 0.5f;                  // ml de regulador de pH por dosis
  float caudalMlS = 1.0f;                     // caudal calibrado de las bombas peristálticas
  uint32_t esperaMezclaMs = 15 * 60 * 1000UL; // entre dosis, para que la solución se mezcle
  uint32_t pausaEntrePartesMs = 30 * 1000UL;  // entre la parte A y la B (nunca mezclarlas puras)
  uint8_t maxDosisNutrientesDia = 6;
  uint8_t maxDosisPhDia = 8;
};

struct ConfigCultivo {
  ConfigLuz luz;
  ConfigHumedad humedad;
  ConfigRiego riego;
  ConfigNutrientes nutrientes;
  RangoPh rangoPh = rangoPhPorDefecto(Medio::Coco);
  ObjetivosEtapa etapas[NUM_ETAPAS] = {};     // indexado por Etapa
};

}  // namespace cultivo
