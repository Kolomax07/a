// Coordina los cuatro controladores, los relés y los pulsos de las bombas.
#pragma once

#include "cultivo_controladores.h"

namespace cultivo {

// Lo que el controlador necesita del hardware: encender y apagar relés.
class Salidas {
 public:
  virtual ~Salidas() = default;
  virtual void fijar(Actuador actuador, bool encendido) = 0;
};

// Cola de pulsos de bomba (riego, A, pausa, B...) que avanza sin bloquear el programa:
// el panel web y el resto del control siguen funcionando mientras una bomba trabaja.
class Secuenciador {
 public:
  static constexpr uint8_t CAPACIDAD = 4;
  bool encolar(Actuador actuador, uint32_t msEncendido, uint32_t pausaDespuesMs);
  void atender(uint64_t ms, Salidas& salidas);
  bool ocupado() const { return cantidad_ > 0; }
  void cancelar(Salidas& salidas);  // apaga la bomba en marcha y vacía la cola

 private:
  struct Paso {
    Actuador actuador;
    uint32_t msEncendido;
    uint32_t pausaDespuesMs;
  };
  Paso pasos_[CAPACIDAD] = {};
  uint8_t primero_ = 0;
  uint8_t cantidad_ = 0;
  bool enMarcha_ = false;
  uint64_t finPaso_ = 0;
  uint64_t libreDesde_ = 0;
};

// Últimos eventos (riegos, dosis, alarmas...) para mostrarlos en el panel web.
class Bitacora {
 public:
  static constexpr uint8_t CAPACIDAD = 20;
  static constexpr uint8_t LARGO = 160;
  struct Entrada {
    bool horaValida;
    int16_t minutoDelDia;
    char texto[LARGO];
  };
  void anotar(const Lecturas& l, const char* texto);
  uint8_t cantidad() const { return cantidad_; }
  const Entrada& entrada(uint8_t i) const;  // 0 = la más reciente

 private:
  Entrada entradas_[CAPACIDAD] = {};
  uint8_t siguiente_ = 0;
  uint8_t cantidad_ = 0;
};

class ControladorCultivo {
 public:
  using AlEvento = void (*)(const char* texto);

  ControladorCultivo(const ConfigCultivo& config, Etapa etapa, Salidas& salidas);

  // Un ciclo de control completo con unas lecturas recién tomadas.
  void ciclo(const Lecturas& l);
  // Avanza los pulsos de bomba: llamar en cada vuelta del bucle principal.
  void atender(uint64_t ms);
  void cambiarEtapa(Etapa etapa);
  // Pulso manual para comprobar el cableado. Solo si el equipo está apagado y no hay
  // otra bomba en marcha; devuelve false si no se pudo.
  bool probar(Actuador a, uint32_t ms);
  void apagarTodo();
  void alEvento(AlEvento funcion) { alEvento_ = funcion; }

  const ConfigCultivo& config() const { return config_; }
  Etapa etapa() const { return etapa_; }
  const ObjetivosEtapa& objetivos() const { return config_.etapas[static_cast<uint8_t>(etapa_)]; }
  RangoPh rangoPh() const { return config_.rangoPh; }
  bool estado(Actuador a) const { return estados_[static_cast<uint8_t>(a)]; }
  Alarmas alarmas() const { return alarmas_; }
  Alarmas historialAlarmas() const { return historialAlarmas_; }
  uint32_t pulsos(Actuador a) const { return pulsos_[static_cast<uint8_t>(a)]; }
  uint8_t riegosHoy() const { return riego_.riegosHoy(); }
  uint8_t dosisNutrientesHoy() const { return nutrientes_.dosisNutrientesHoy(); }
  uint8_t dosisPhHoy() const { return nutrientes_.dosisPhHoy(); }
  const Bitacora& bitacora() const { return bitacora_; }

 private:
  // Envuelve las salidas reales para saber en todo momento qué está encendido.
  class Reles : public Salidas {
   public:
    Reles(Salidas& salidas, bool* estados) : salidas_(salidas), estados_(estados) {}
    void fijar(Actuador a, bool encendido) override;

   private:
    Salidas& salidas_;
    bool* estados_;
  };

  void evento(const Lecturas& l, const char* texto);
  void pulso(Actuador a, uint32_t ms, uint32_t pausaDespuesMs);

  ConfigCultivo config_;
  Etapa etapa_;
  bool estados_[NUM_ACTUADORES] = {};
  Reles reles_;
  ControladorLuz luz_;
  ControladorHumedad humedad_;
  ControladorRiego riego_;
  ControladorNutrientes nutrientes_;
  Secuenciador secuenciador_;
  Bitacora bitacora_;
  Alarmas alarmas_ = 0;
  Alarmas historialAlarmas_ = 0;
  uint32_t pulsos_[NUM_ACTUADORES] = {};
  Lecturas ultimas_;
  AlEvento alEvento_ = nullptr;
};

}  // namespace cultivo
