// Los cuatro controladores. Reciben Lecturas y devuelven decisiones; no tocan el hardware.
#pragma once

#include "cultivo_tipos.h"

namespace cultivo {

// Estado on/off que respeta un tiempo mínimo entre cambios para no castigar los equipos.
class Interruptor {
 public:
  explicit Interruptor(uint32_t tiempoMinimoMs) : tiempoMinimoMs_(tiempoMinimoMs) {}
  bool pedir(uint64_t ms, bool encender);
  void apagar(uint64_t ms);  // apagado inmediato por seguridad: ignora el tiempo mínimo
  bool encendido() const { return encendido_; }

 private:
  uint32_t tiempoMinimoMs_;
  bool encendido_ = false;
  bool haCambiado_ = false;
  uint64_t ultimoCambio_ = 0;
};

// Enciende la lámpara `horasLuz` horas seguidas desde `minutoEncendido` (puede cruzar la
// medianoche). Con sensor de luz, avisa si la lámpara no responde o si entra luz de noche.
class ControladorLuz {
 public:
  ControladorLuz(const ConfigLuz& config, float horasLuz) : config_(config), horasLuz_(horasLuz) {}
  void fijarHorasLuz(float horas) { horasLuz_ = horas; }
  bool debeEstarEncendida(int minutoDelDia) const;
  bool actualizar(const Lecturas& l);
  Alarmas alarmas() const { return alarmas_; }

 private:
  void vigilarSensor(const Lecturas& l, bool encendida);

  ConfigLuz config_;
  float horasLuz_;
  bool hayDiscrepancia_ = false;
  uint64_t discrepanciaDesde_ = 0;
  Alarmas alarmas_ = 0;
};

struct EstadoClima {
  bool humidificador = false;
  bool deshumidificador = false;
  bool extractor = false;
};

// Mantiene la HR entre hrMin y hrMax con banda muerta. Humidificador y deshumidificador
// nunca a la vez. El extractor ayuda con calor o humedad y, si fallan los sensores, queda
// encendido: es el estado más seguro para las plantas.
class ControladorHumedad {
 public:
  ControladorHumedad(const ConfigHumedad& config, const ObjetivosEtapa& objetivos);
  void fijarObjetivos(const ObjetivosEtapa& objetivos) { objetivos_ = objetivos; }
  EstadoClima actualizar(const Lecturas& l);
  Alarmas alarmas() const { return alarmas_; }

 private:
  ConfigHumedad config_;
  ObjetivosEtapa objetivos_;
  Interruptor humidificador_;
  Interruptor deshumidificador_;
  Interruptor extractor_;
  Alarmas alarmas_ = 0;
};

// Decide cuántos ms debe funcionar la bomba (0 = no regar). Deja un tiempo de absorción
// antes de volver a regar y nunca supera maxRiegosDia, para no encharcar las raíces si
// el sensor se estropea.
class ControladorRiego {
 public:
  explicit ControladorRiego(const ConfigRiego& config) : config_(config) {}
  uint32_t actualizar(const Lecturas& l, bool luzEncendida);
  uint8_t riegosHoy() const { return riegosHoy_; }
  Alarmas alarmas() const { return alarmas_; }

 private:
  ConfigRiego config_;
  bool haRegado_ = false;
  uint64_t ultimoRiego_ = 0;
  bool hayDia_ = false;
  int32_t dia_ = 0;
  uint8_t riegosHoy_ = 0;
  Alarmas alarmas_ = 0;
};

struct Dosis {
  uint8_t cantidad = 0;  // 0 = nada, 1 = corrección de pH, 2 = partes A y B (en ese orden)
  Actuador actuadores[2] = {Actuador::DosificadorA, Actuador::DosificadorB};
  uint32_t ms = 0;       // tiempo de bomba de cada actuador
};

// Mantiene EC y pH del depósito con dosis pequeñas y esperas de mezcla. En cada ciclo hace
// como mucho UNA corrección: primero sube la EC (A y luego B) hasta el objetivo y después
// lleva el pH al centro del rango. Los límites diarios evitan vaciar los bidones si una
// sonda se estropea.
class ControladorNutrientes {
 public:
  ControladorNutrientes(const ConfigNutrientes& config, float ecObjetivo, RangoPh rangoPh)
      : config_(config), ecObjetivo_(ecObjetivo), rangoPh_(rangoPh) {}
  void fijarEcObjetivo(float ec) { ecObjetivo_ = ec; }
  Dosis actualizar(const Lecturas& l);
  uint8_t dosisNutrientesHoy() const { return dosisNutrientesHoy_; }
  uint8_t dosisPhHoy() const { return dosisPhHoy_; }
  Alarmas alarmas() const { return alarmas_; }

 private:
  enum class CorreccionPh : uint8_t { Ninguna, Bajar, Subir };

  ConfigNutrientes config_;
  float ecObjetivo_;
  RangoPh rangoPh_;
  bool subiendoEc_ = false;
  CorreccionPh correccionPh_ = CorreccionPh::Ninguna;
  bool haDosificado_ = false;
  uint64_t ultimaDosis_ = 0;
  bool hayDia_ = false;
  int32_t dia_ = 0;
  uint8_t dosisNutrientesHoy_ = 0;
  uint8_t dosisPhHoy_ = 0;
  Alarmas alarmas_ = 0;
};

}  // namespace cultivo
