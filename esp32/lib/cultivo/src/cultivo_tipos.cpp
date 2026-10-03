#include "cultivo_tipos.h"

namespace cultivo {

const char* nombre(Actuador a) {
  static const char* const NOMBRES[NUM_ACTUADORES] = {
      "luz", "bomba_riego", "humidificador", "deshumidificador", "extractor",
      "dosificador_a", "dosificador_b", "ph_up", "ph_down",
  };
  return NOMBRES[static_cast<uint8_t>(a)];
}

const char* nombre(Etapa e) {
  static const char* const NOMBRES[NUM_ETAPAS] = {
      "plantula", "vegetativo", "floracion", "floracion_tardia", "lavado",
  };
  return NOMBRES[static_cast<uint8_t>(e)];
}

const char* textoAlarma(uint8_t bit) {
  static const char* const TEXTOS[alarma::TOTAL] = {
      "La lámpara debería estar encendida pero no se detecta luz: revisa lámpara, driver y relé",
      "Entra luz durante el periodo de oscuridad: revisa fugas de luz (en floración pueden "
      "provocar plantas hermafroditas)",
      "Sin lectura de humedad relativa: humidificador y deshumidificador apagados",
      "Sin lectura de temperatura: extractor encendido por seguridad",
      "Temperatura crítica: el extractor no da abasto, revisa la ventilación",
      "Sin lectura de humedad del sustrato: riego en pausa",
      "Límite diario de riegos alcanzado y el sustrato sigue seco: revisa bomba, goteros y sensor",
      "Lectura de EC no válida (¿sonda fuera del agua o depósito sin nutrientes?): dosificación en pausa",
      "Lectura de pH no válida: dosificación en pausa, revisa la sonda",
      "EC alta en el depósito: añade agua sin nutrientes",
      "Límite diario de dosis de nutrientes alcanzado y la EC sigue baja: revisa la sonda de EC y los bidones",
      "Límite diario de correcciones de pH alcanzado y sigue fuera de rango: revisa la sonda de pH "
      "y los reguladores",
      "Nivel bajo en el depósito: riego y dosificación bloqueados hasta rellenar",
      "Hora sin sincronizar (sin WiFi/NTP ni RTC): luz apagada y riego sin horario",
  };
  return bit < alarma::TOTAL ? TEXTOS[bit] : "";
}

}  // namespace cultivo
