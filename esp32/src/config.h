// ============================================================================
//  CONFIGURACIÓN DEL CONTROLADOR — edita este archivo y vuelve a compilar.
//  La etapa del cultivo se cambia después desde el panel web (http://cultivo.local).
// ============================================================================
#pragma once

#include <cultivo.h>

// --- Red ----------------------------------------------------------------------
// Copia secretos.ejemplo.h como secretos.h y pon tu WiFi (secretos.h no se sube a git).
#if __has_include("secretos.h")
#include "secretos.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define PANEL_PASSWORD ""
#endif

// Zona horaria en formato POSIX (fotoperiodo y cambio de hora automáticos).
//   España peninsular: "CET-1CEST,M3.5.0,M10.5.0/3"   Canarias: "WET0WEST,M3.5.0/1,M10.5.0"
//   México (centro): "CST6"   Argentina: "<-03>3"   Colombia: "<-05>5"   Chile: "<-04>4<-03>,M9.1.6/24,M4.1.6/24"
constexpr const char* ZONA_HORARIA = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* NOMBRE_HOST = "cultivo";  // panel en http://cultivo.local

// --- Cultivo ------------------------------------------------------------------
constexpr cultivo::Etapa ETAPA_INICIAL = cultivo::Etapa::Vegetativo;  // solo en el primer arranque
constexpr cultivo::Medio MEDIO = cultivo::Medio::Coco;  // Tierra (pH 6,2–6,8), Coco (5,7–6,3), Hidro (5,6–6,2)
constexpr uint32_t INTERVALO_CONTROL_MS = 60 * 1000UL;

// Objetivos por etapa: {horas de luz, HR mín %, HR máx %, temp. máx °C (extractor), EC mS/cm}.
// Valores de partida habituales: ajústalos a tu genética y a la tabla de tu marca de abonos.
constexpr cultivo::ObjetivosEtapa OBJETIVOS[cultivo::NUM_ETAPAS] = {
    /* Plántula         */ {18, 65, 75, 27, 0.6f},
    /* Vegetativo       */ {18, 50, 65, 28, 1.2f},
    /* Floración        */ {12, 40, 50, 26, 1.6f},
    /* Floración tardía */ {12, 35, 45, 25, 1.4f},
    /* Lavado           */ {12, 35, 45, 25, 0.0f},  // solo agua: corrige pH, no añade nutrientes
};

inline cultivo::ConfigCultivo configCultivo() {
  cultivo::ConfigCultivo c;
  for (uint8_t i = 0; i < cultivo::NUM_ETAPAS; i++) c.etapas[i] = OBJETIVOS[i];
  c.rangoPh = cultivo::rangoPhPorDefecto(MEDIO);  // o un rango propio: {5.8f, 6.2f}

  c.luz.minutoEncendido = 6 * 60;               // la luz se enciende a las 06:00
  c.luz.umbralLux = 500;                        // por debajo se considera oscuro
  c.luz.toleranciaMs = 10 * 60 * 1000UL;        // espera antes de avisar de lámpara/fuga de luz

  c.humedad.histeresis = 3;                     // % HR de banda muerta
  c.humedad.histeresisTemp = 1;                 // °C de banda muerta del extractor
  c.humedad.tiempoMinimoCicloMs = 120 * 1000UL; // entre encendidos/apagados de cada equipo

  c.riego.humedadSueloMin = 35;                 // % de sustrato por debajo del cual se riega
  c.riego.duracionMs = 20 * 1000UL;             // tiempo de bomba por riego
  c.riego.esperaAbsorcionMs = 45 * 60 * 1000UL; // espera tras regar antes de volver a evaluar
  c.riego.maxRiegosDia = 6;
  c.riego.soloConLuz = true;

  c.nutrientes.toleranciaEc = 0.15f;            // mS/cm bajo el objetivo antes de dosificar
  c.nutrientes.margenEcAlta = 0.4f;             // mS/cm sobre el objetivo para avisar
  c.nutrientes.mlPorDosis = 2.0f;               // ml de cada parte (A y B) por dosis
  c.nutrientes.mlPorDosisPh = 0.5f;             // ml de pH+/pH- por dosis
  c.nutrientes.caudalMlS = 1.0f;                // calibra tus peristálticas: ml que echan en 1 s
  c.nutrientes.esperaMezclaMs = 15 * 60 * 1000UL;
  c.nutrientes.pausaEntrePartesMs = 30 * 1000UL;
  c.nutrientes.maxDosisNutrientesDia = 6;
  c.nutrientes.maxDosisPhDia = 8;
  return c;
}

// --- Calibración de sondas (usa la página /calibrar del panel) ---------------
inline cultivo::CalibracionSondas calibracionSondas() {
  cultivo::CalibracionSondas cal;
  cal.sueloVSeco = 2.80f;    // sensor de sustrato al aire
  cal.sueloVMojado = 1.30f;  // sensor de sustrato sumergido en agua
  cal.phV7 = 1.50f;          // sonda de pH en tampón pH 7
  cal.phV4 = 2.03f;          // sonda de pH en tampón pH 4
  cal.ecV0 = 0.00f;          // sonda de EC en agua destilada
  cal.ecV1413 = 1.00f;       // sonda de EC en patrón de 1,413 mS/cm
  return cal;
}

// --- Hardware -----------------------------------------------------------------
enum class SensorClima { Ninguno, Sht31, Dht22 };
constexpr SensorClima SENSOR_CLIMA = SensorClima::Sht31;
constexpr int PIN_DHT22 = 13;

constexpr int PIN_SDA = 21;  // bus I2C: SHT31, BH1750, ADS1115, DS3231
constexpr int PIN_SCL = 22;
constexpr bool USAR_BH1750 = true;     // sensor de luz (opcional)
constexpr bool USAR_RTC_DS3231 = false;  // reloj con pila: mantiene la hora sin WiFi

// Sondas analógicas: ADS1115 (recomendado, más preciso) o ADC interno del ESP32.
constexpr bool USAR_ADS1115 = true;
constexpr uint8_t DIRECCION_ADS1115 = 0x48;
// Con ADS1115: canal 0–3. Con ADC interno: GPIO del ADC1 (32–39). -1 = no instalado.
constexpr int8_t ENTRADA_SUELO = 0;
constexpr int8_t ENTRADA_EC = 1;
constexpr int8_t ENTRADA_PH = 2;

constexpr int8_t PIN_FLOTADOR = 4;  // interruptor de nivel del depósito a GND (-1 = no instalado)
constexpr bool FLOTADOR_CERRADO_CON_AGUA = true;

// Relés, en el orden de cultivo::Actuador. -1 = equipo no instalado.
// Evita los GPIO 0, 2, 5, 12 y 15 (condicionan el arranque) y 34–39 (solo entrada).
// En placas WROVER (con PSRAM) tampoco uses 16 y 17.
constexpr bool RELES_ACTIVOS_EN_BAJO = true;  // la mayoría de módulos de relés
constexpr int8_t PINES_RELES[cultivo::NUM_ACTUADORES] = {
    16,  // luz
    17,  // bomba de riego
    18,  // humidificador
    19,  // deshumidificador
    23,  // extractor
    25,  // nutriente A
    26,  // nutriente B
    27,  // pH+
    33,  // pH-
};
