#include "hardware.h"

#include <Adafruit_ADS1X15.h>
#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <BH1750.h>
#include <DHT.h>
#include <Wire.h>

#include "config.h"

using cultivo::Actuador;
using cultivo::Lecturas;
using cultivo::Nivel;

namespace {

constexpr uint8_t DIRECCION_SHT31 = 0x44;
constexpr int MUESTRAS_ADC_INTERNO = 16;

Adafruit_SHT31 sht31;
DHT dht22(PIN_DHT22, DHT22);
BH1750 bh1750;
Adafruit_ADS1115 ads1115;
const cultivo::CalibracionSondas CALIBRACION = calibracionSondas();

uint8_t nivelRele(bool encendido) { return encendido != RELES_ACTIVOS_EN_BAJO ? HIGH : LOW; }

int8_t entrada(Sonda sonda) {
  switch (sonda) {
    case Sonda::Suelo: return ENTRADA_SUELO;
    case Sonda::Ec: return ENTRADA_EC;
    case Sonda::Ph: return ENTRADA_PH;
  }
  return -1;
}

}  // namespace

void Hardware::iniciar() {
  // Se fija el nivel de "apagado" antes de configurar el pin como salida para que
  // ningún relé dé un pulso al arrancar.
  for (uint8_t i = 0; i < cultivo::NUM_ACTUADORES; i++) {
    if (PINES_RELES[i] < 0) continue;
    digitalWrite(PINES_RELES[i], nivelRele(false));
    pinMode(PINES_RELES[i], OUTPUT);
  }
  if (PIN_FLOTADOR >= 0) pinMode(PIN_FLOTADOR, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (SENSOR_CLIMA == SensorClima::Dht22) dht22.begin();
  iniciarSensores();
}

void Hardware::iniciarSensores() {
  if (SENSOR_CLIMA == SensorClima::Sht31 && !sht31Ok_) sht31Ok_ = sht31.begin(DIRECCION_SHT31);
  if (USAR_BH1750 && !bh1750Ok_) bh1750Ok_ = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  if (USAR_ADS1115 && !ads1115Ok_) {
    ads1115Ok_ = ads1115.begin(DIRECCION_ADS1115);
    if (ads1115Ok_) ads1115.setGain(GAIN_ONE);  // ±4,096 V: cubre sondas alimentadas a 3,3 V
  }
}

void Hardware::leer(Lecturas& l) {
  iniciarSensores();  // reintenta los que fallaron (cable suelto, sensor recién conectado)

  if (SENSOR_CLIMA == SensorClima::Sht31 && sht31Ok_) {
    float temperatura, humedad;
    if (sht31.readBoth(&temperatura, &humedad)) {
      l.temperatura = temperatura;
      l.humedadRelativa = humedad;
    } else {
      sht31Ok_ = false;
    }
  } else if (SENSOR_CLIMA == SensorClima::Dht22) {
    l.temperatura = dht22.readTemperature();  // NAN si falla
    l.humedadRelativa = dht22.readHumidity();
  }

  if (USAR_BH1750 && bh1750Ok_) {
    const float lux = bh1750.readLightLevel();  // negativo si falla
    if (lux >= 0) {
      l.lux = lux;
    } else {
      bh1750Ok_ = false;
    }
  }

  const float vSuelo = voltaje(Sonda::Suelo);
  const float vEc = voltaje(Sonda::Ec);
  const float vPh = voltaje(Sonda::Ph);
  if (!std::isnan(vSuelo)) l.humedadSuelo = cultivo::humedadSuelo(vSuelo, CALIBRACION);
  // Sin sonda de temperatura en el agua se usa la del aire para compensar la EC.
  if (!std::isnan(vEc)) l.ec = cultivo::ec(vEc, CALIBRACION, l.temperatura);
  if (!std::isnan(vPh)) l.ph = cultivo::ph(vPh, CALIBRACION);

  if (PIN_FLOTADOR >= 0) {
    const bool cerrado = digitalRead(PIN_FLOTADOR) == LOW;
    l.deposito = cerrado == FLOTADOR_CERRADO_CON_AGUA ? Nivel::Ok : Nivel::Bajo;
  }
}

void Hardware::fijar(Actuador actuador, bool encendido) {
  const int8_t pin = PINES_RELES[static_cast<uint8_t>(actuador)];
  if (pin >= 0) digitalWrite(pin, nivelRele(encendido));
}

float Hardware::voltaje(Sonda sonda) {
  const int8_t e = entrada(sonda);
  if (e < 0) return NAN;
  if (USAR_ADS1115) {
    if (!ads1115Ok_) return NAN;
    return ads1115.computeVolts(ads1115.readADC_SingleEnded(static_cast<uint8_t>(e)));
  }
  // ADC interno (calibrado de fábrica en mV): se promedian varias lecturas por el ruido.
  uint32_t suma = 0;
  for (int i = 0; i < MUESTRAS_ADC_INTERNO; i++) suma += analogReadMilliVolts(e);
  return suma / static_cast<float>(MUESTRAS_ADC_INTERNO) / 1000.0f;
}
