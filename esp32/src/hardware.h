// Sensores y relés del ESP32.
#pragma once

#include <cultivo.h>

enum class Sonda : uint8_t { Suelo, Ec, Ph };

class Hardware : public cultivo::Salidas {
 public:
  // Deja todos los relés apagados (lo primero, antes de nada) e inicia los sensores.
  void iniciar();
  // Rellena los campos de sensores de `l`. Un sensor que falla queda en NAN.
  void leer(cultivo::Lecturas& l);
  void fijar(cultivo::Actuador actuador, bool encendido) override;
  // Voltaje en bruto de una sonda analógica (NAN si no está instalada o falla).
  float voltaje(Sonda sonda);

 private:
  void iniciarSensores();

  bool sht31Ok_ = false;
  bool bh1750Ok_ = false;
  bool ads1115Ok_ = false;
};
